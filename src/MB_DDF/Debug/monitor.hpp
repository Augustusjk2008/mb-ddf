#pragma once

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>
#include <typeinfo>
#include <vector>

// Forward declaration for Schema support
namespace MB_DDF::Tools {
class LogSchema;
}

#ifndef ENABLE_MONITOR
#define ENABLE_MONITOR 1
#endif

namespace MB_DDF::monitor_detail {

constexpr std::size_t kCacheLineSize = 64;
constexpr std::size_t kProbeBufferSize = 120;  // 128 - 8 bytes for sequence + initialized (2 cache lines)

#if defined(__GNUC__) || defined(__clang__)
#if defined(MONITOR_ENABLE_RETAIN)
#if defined(__has_attribute)
#if __has_attribute(retain)
#define MONITOR_ATTR_RETAIN [[gnu::retain]]
#else
#define MONITOR_ATTR_RETAIN
#endif
#else
#if defined(__GNUC__) && (__GNUC__ >= 11)
#define MONITOR_ATTR_RETAIN [[gnu::retain]]
#else
#define MONITOR_ATTR_RETAIN
#endif
#endif
#else
#define MONITOR_ATTR_RETAIN
#endif
#define MONITOR_ATTR_USED [[gnu::used]] MONITOR_ATTR_RETAIN
#define MONITOR_ATTR_SECTION(name) [[gnu::section(name)]]
#define MONITOR_ATTR_ALIGN64 [[gnu::aligned(64)]]
#else
#define MONITOR_ATTR_USED
#define MONITOR_ATTR_SECTION(name)
#define MONITOR_ATTR_ALIGN64
#endif

struct alignas(128) ProbeSlot {
    std::byte buffer[kProbeBufferSize];
    std::atomic<std::uint32_t> sequence{0};
    std::atomic<std::uint32_t> initialized{0}; // 0=uninit,1=initing,2=ready
};

static_assert(sizeof(ProbeSlot) == 128, "ProbeSlot must occupy exactly 128 bytes (2 cache lines)");
static_assert(alignof(ProbeSlot) == 128, "ProbeSlot must be 128-byte aligned");

struct TypeMetadata {
    const char* name;
    const char* file;
    std::uint32_t line;
    std::uint32_t size;
    std::uint64_t type_hash;
    void (*to_string)(const void* src, char* dst, std::size_t len);
};

// Schema-aware metadata for SelfDescribingLog integration
struct SchemaMetadata {
    const char* name;           // Variable name
    const char* file;           // Source file
    std::uint32_t line;         // Line number
    std::uint32_t size;         // Struct size in bytes
    std::uint64_t type_hash;    // Schema type marker (kSchemaTypeHash)
    const Tools::LogSchema* schema; // Pointer to LogSchema (nullable)
};

// Type hash marker for schema-based probes
constexpr std::uint64_t kSchemaTypeHash = 0x534348454D410001ULL; // "SCHEMA\0\x01"

struct alignas(128) ProbeNode {
    ProbeSlot slot; // hot: exactly 128 bytes (2 cache lines)
    const TypeMetadata* meta{nullptr}; // cold: metadata pointer
    ProbeSlot* next{nullptr}; // cold: intrusive list (by slot address)
};

// Schema-aware probe node - shares ProbeSlot layout for uniform handling
struct alignas(128) SchemaProbeNode {
    ProbeSlot slot;                 // hot: exactly 128 bytes (must be first)
    const SchemaMetadata* meta{nullptr}; // cold: schema metadata pointer
    ProbeSlot* next{nullptr};       // cold: intrusive list
};

static_assert(offsetof(SchemaProbeNode, slot) == 0, "ProbeSlot must be first member in SchemaProbeNode");
static_assert(offsetof(ProbeNode, slot) == 0, "ProbeSlot must be first member in ProbeNode");

alignas(128) inline std::atomic<ProbeSlot*> g_probe_chain_head{nullptr};
alignas(128) inline std::atomic<ProbeSlot*> g_schema_chain_head{nullptr};

#if defined(__cpp_rtti) || defined(__GXX_RTTI) || defined(_CPPRTTI)
template <typename T>
constexpr std::uint64_t type_hash() {
    return static_cast<std::uint64_t>(typeid(T).hash_code());
}
#else
constexpr std::uint64_t fnv1a_64(const char* str) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t i = 0; str[i] != '\0'; ++i) {
        hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(str[i]));
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename T>
constexpr std::uint64_t type_fingerprint() {
#if defined(__GNUC__) || defined(__clang__)
    return fnv1a_64(__PRETTY_FUNCTION__);
#elif defined(_MSC_VER)
    return fnv1a_64(__FUNCSIG__);
#else
    return 0ULL;
#endif
}

template <typename T>
constexpr std::uint64_t type_hash() {
    return type_fingerprint<T>();
}
#endif

inline void hex_dump(const void* src, std::size_t len, char* dst, std::size_t dst_len) {
    const auto* bytes = static_cast<const std::uint8_t*>(src);
    std::size_t out = 0;
    for (std::size_t i = 0; i < len && (out + 3) < dst_len; ++i) {
        const int wrote = std::snprintf(dst + out, dst_len - out, "%02X ", bytes[i]);
        if (wrote <= 0) {
            break;
        }
        out += static_cast<std::size_t>(wrote);
    }
    if (dst_len > 0) {
        dst[(out < dst_len) ? out : (dst_len - 1)] = '\0';
    }
}

template <typename T>
inline bool append_scalar(char* dst, std::size_t len, std::size_t& out, const T& value) {
    if (len == 0 || out >= len) {
        return false;
    }
    auto write = [&](const char* fmt, auto v) -> bool {
        const int wrote = std::snprintf(dst + out, len - out, fmt, v);
        if (wrote <= 0) {
            return false;
        }
        if (static_cast<std::size_t>(wrote) >= (len - out)) {
            out = len - 1;
            return false;
        }
        out += static_cast<std::size_t>(wrote);
        return true;
    };

    if constexpr (std::is_same_v<T, bool>) {
        return write("%s", value ? "true" : "false");
    } else if constexpr (std::is_floating_point_v<T>) {
        return write("%.6g", static_cast<double>(value));
    } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
        return write("%lld", static_cast<long long>(value));
    } else if constexpr (std::is_integral_v<T>) {
        return write("%llu", static_cast<unsigned long long>(value));
    } else {
        return false;
    }
}

template <std::size_t N>
struct HexDumpPrinter {
    static void print(const void* src, char* dst, std::size_t len) {
        hex_dump(src, N, dst, len);
    }
};

template <typename T, std::size_t N>
struct ArrayPrinter {
    static void print(const void* src, char* dst, std::size_t len) {
        if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, bool>) {
            const auto* elems = static_cast<const T*>(src);
            std::size_t out = 0;
            auto append_str = [&](const char* s) -> bool {
                if (len == 0 || out >= len) {
                    return false;
                }
                const int wrote = std::snprintf(dst + out, len - out, "%s", s);
                if (wrote <= 0) {
                    return false;
                }
                if (static_cast<std::size_t>(wrote) >= (len - out)) {
                    out = len - 1;
                    return false;
                }
                out += static_cast<std::size_t>(wrote);
                return true;
            };

            if (!append_str("[")) {
                return;
            }
            for (std::size_t i = 0; i < N; ++i) {
                if (i > 0) {
                    if (!append_str(", ")) {
                        break;
                    }
                }
                if (!append_scalar(dst, len, out, elems[i])) {
                    break;
                }
            }
            append_str("]");
            if (len > 0) {
                dst[(out < len) ? out : (len - 1)] = '\0';
            }
        } else {
            hex_dump(src, N * sizeof(T), dst, len);
        }
    }
};

inline ProbeNode* node_from_slot(ProbeSlot* slot) {
    return reinterpret_cast<ProbeNode*>(slot);
}
inline const ProbeNode* node_from_slot(const ProbeSlot* slot) {
    return reinterpret_cast<const ProbeNode*>(slot);
}

inline void register_node(ProbeNode* node) {
    ProbeSlot* head = g_probe_chain_head.load(std::memory_order_relaxed);
    do {
        node->next = head;
    } while (!g_probe_chain_head.compare_exchange_weak(
        head, &node->slot, std::memory_order_release, std::memory_order_relaxed));
}

inline void register_schema_node(SchemaProbeNode* node) {
    ProbeSlot* head = g_schema_chain_head.load(std::memory_order_relaxed);
    do {
        node->next = head;
    } while (!g_schema_chain_head.compare_exchange_weak(
        head, &node->slot, std::memory_order_release, std::memory_order_relaxed));
}

inline void ensure_initialized(ProbeNode* node, const TypeMetadata* meta) {
    std::uint32_t state = node->slot.initialized.load(std::memory_order_acquire);
    if (state == 2) {
        return;
    }

    if (state == 0) {
        std::uint32_t expected = 0;
        if (node->slot.initialized.compare_exchange_strong(
                expected, 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            node->meta = meta;
            register_node(node);
            node->slot.initialized.store(2, std::memory_order_release);
            return;
        }
    }

    while (node->slot.initialized.load(std::memory_order_acquire) != 2) {
        // Cold-path spin until another thread completes init.
    }
}

inline void ensure_schema_initialized(SchemaProbeNode* node, const SchemaMetadata* meta) {
    std::uint32_t state = node->slot.initialized.load(std::memory_order_acquire);
    if (state == 2) {
        return;
    }

    if (state == 0) {
        std::uint32_t expected = 0;
        if (node->slot.initialized.compare_exchange_strong(
                expected, 1, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            node->meta = meta;
            register_schema_node(node);
            node->slot.initialized.store(2, std::memory_order_release);
            return;
        }
    }

    while (node->slot.initialized.load(std::memory_order_acquire) != 2) {
        // Cold-path spin until another thread completes init.
    }
}

inline void write_bytes(ProbeSlot* slot, const void* src, std::size_t len) {
    // Single-writer expected. RMW ordering prevents writes moving before the odd mark.
    slot->sequence.fetch_add(1, std::memory_order_acq_rel); // odd: write in progress
    std::memcpy(slot->buffer, src, len);
    slot->sequence.fetch_add(1, std::memory_order_release); // even: write complete
}

template <typename T>
inline void write_value(ProbeSlot* slot, const T& value) {
    write_bytes(slot, &value, sizeof(T));
}

inline bool read_bytes(const ProbeSlot* slot, void* dst, std::size_t len) {
    const std::uint32_t seq_before = slot->sequence.load(std::memory_order_acquire);
    if ((seq_before & 1U) != 0U) {
        return false;
    }

    std::memcpy(dst, slot->buffer, len);
    std::atomic_thread_fence(std::memory_order_acquire);

    const std::uint32_t seq_after = slot->sequence.load(std::memory_order_acquire);
    return seq_after == seq_before;
}

} // namespace monitor_detail

namespace monitor_detail = MB_DDF::monitor_detail;

template <typename T>
struct MonitorPrinter {
    static void print(const void* src, char* dst, std::size_t len) {
        if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, bool>) {
            std::size_t out = 0;
            monitor_detail::append_scalar(dst, len, out, *static_cast<const T*>(src));
            if (len > 0) {
                dst[(out < len) ? out : (len - 1)] = '\0';
            }
        } else {
            monitor_detail::hex_dump(src, sizeof(T), dst, len);
        }
    }
};


namespace MonitorHub {

using MB_DDF::monitor_detail::ProbeSlot;
using MB_DDF::monitor_detail::TypeMetadata;
using MB_DDF::monitor_detail::SchemaMetadata;
using MB_DDF::monitor_detail::SchemaProbeNode;
using MB_DDF::monitor_detail::kSchemaTypeHash;

enum class FormatMode : std::uint8_t {
    Auto,
    Hex
};

struct ProbeSnapshot {
    const TypeMetadata* meta{nullptr};
    const ProbeSlot* slot{nullptr};
    std::array<std::byte, MB_DDF::monitor_detail::kProbeBufferSize> data{};
    std::uint32_t size{0};
    bool valid{false};
};

// Schema-aware snapshot for SelfDescribingLog integration
struct SchemaProbeSnapshot {
    const SchemaMetadata* meta{nullptr};
    const ProbeSlot* slot{nullptr};
    std::array<std::byte, MB_DDF::monitor_detail::kProbeBufferSize> data{};
    std::uint32_t size{0};
    bool valid{false};
};

inline ProbeSlot* get_chain_head() {
    return MB_DDF::monitor_detail::g_probe_chain_head.load(std::memory_order_acquire);
}

inline ProbeSlot* get_schema_chain_head() {
    return MB_DDF::monitor_detail::g_schema_chain_head.load(std::memory_order_acquire);
}

inline ProbeSlot* next_slot(ProbeSlot* slot) {
    return MB_DDF::monitor_detail::node_from_slot(slot)->next;
}

// Get next slot from schema chain (SchemaProbeNode has same layout for slot/next)
inline ProbeSlot* next_schema_slot(ProbeSlot* slot) {
    const auto* node = reinterpret_cast<const SchemaProbeNode*>(slot);
    return node->next;
}

inline bool read_slot(const ProbeSlot* slot, void* output_buffer, std::size_t len) {
    const auto* node = MB_DDF::monitor_detail::node_from_slot(slot);
    if (node->meta == nullptr) {
        return false;
    }
    if (len < node->meta->size) {
        return false;
    }
    return MB_DDF::monitor_detail::read_bytes(slot, output_buffer, node->meta->size);
}

inline std::vector<ProbeSnapshot> snapshot() {
    std::vector<ProbeSnapshot> out;
    for (ProbeSlot* slot = get_chain_head(); slot != nullptr; slot = next_slot(slot)) {
        const auto* node = MB_DDF::monitor_detail::node_from_slot(slot);
        if (node->meta == nullptr) {
            continue;
        }
        ProbeSnapshot snap;
        snap.meta = node->meta;
        snap.slot = slot;
        snap.size = node->meta->size;
        snap.valid = MB_DDF::monitor_detail::read_bytes(slot, snap.data.data(), snap.size);
        if (snap.valid) {
            out.push_back(snap);
        }
    }
    return out;
}

// Snapshot schema probes - returns all schema-aware monitor points
inline std::vector<SchemaProbeSnapshot> snapshot_schema() {
    std::vector<SchemaProbeSnapshot> out;
    for (ProbeSlot* slot = get_schema_chain_head(); slot != nullptr; slot = next_schema_slot(slot)) {
        const auto* node = reinterpret_cast<const SchemaProbeNode*>(slot);
        if (node->meta == nullptr) {
            continue;
        }
        SchemaProbeSnapshot snap;
        snap.meta = node->meta;
        snap.slot = slot;
        snap.size = node->meta->size;
        snap.valid = MB_DDF::monitor_detail::read_bytes(slot, snap.data.data(), snap.size);
        if (snap.valid) {
            out.push_back(std::move(snap));
        }
    }
    return out;
}

inline std::string format_value(const ProbeSnapshot& snap, FormatMode mode = FormatMode::Auto) {
    if (!snap.valid || snap.meta == nullptr) {
        return {};
    }
    char buf[512]{};
    if (mode == FormatMode::Hex) {
        MB_DDF::monitor_detail::hex_dump(snap.data.data(), snap.size, buf, sizeof(buf));
    } else {
        if (snap.meta->to_string == nullptr) {
            return {};
        }
        snap.meta->to_string(snap.data.data(), buf, sizeof(buf));
    }
    return std::string(buf);
}

} // namespace MonitorHub

#if ENABLE_MONITOR

#define MONITOR(var)                                                                 \
    do {                                                                             \
        using _MonT = std::decay_t<decltype(var)>;                                   \
        static_assert(std::is_trivially_copyable_v<_MonT>,                           \
                      "MONITOR requires trivially copyable type.");                 \
        static_assert(!std::is_pointer_v<_MonT>,                                     \
                      "MONITOR forbids pointer types.");                             \
        static_assert(sizeof(_MonT) <= monitor_detail::kProbeBufferSize,             \
                      "MONITOR requires size <= kProbeBufferSize bytes.");                        \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitorslot") MONITOR_ATTR_ALIGN64 \
        static monitor_detail::ProbeNode _mon_node;                                 \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitormeta")                       \
        static const monitor_detail::TypeMetadata _mon_meta{                        \
            #var, __FILE__, __LINE__, static_cast<std::uint32_t>(sizeof(_MonT)),     \
            monitor_detail::type_hash<_MonT>(),                                      \
            &MonitorPrinter<_MonT>::print};                                         \
        monitor_detail::ensure_initialized(&_mon_node, &_mon_meta);                 \
        const _MonT& _mon_value = (var);                                             \
        monitor_detail::write_value(&_mon_node.slot, _mon_value);                   \
    } while (0)

#define MONITOR_N(var, n)                                                            \
    do {                                                                             \
        using _ArrT = std::remove_reference_t<decltype(var)>;                        \
        static_assert(std::is_array_v<_ArrT>,                                        \
                      "MONITOR_N requires a fixed-size array.");                    \
        using _ElemT = std::remove_extent_t<_ArrT>;                                  \
        constexpr std::size_t _MonN = (n);                                           \
        static_assert(std::is_trivially_copyable_v<_ElemT>,                          \
                      "MONITOR_N requires trivially copyable element type.");       \
        static_assert(!std::is_pointer_v<_ElemT>,                                    \
                      "MONITOR_N forbids pointer element types.");                  \
        static_assert(_MonN <= std::extent_v<_ArrT>,                                 \
                      "MONITOR_N exceeds array extent.");                           \
        static_assert(_MonN * sizeof(_ElemT) <= monitor_detail::kProbeBufferSize,    \
                      "MONITOR_N requires N * sizeof(elem) <= kProbeBufferSize bytes.");          \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitorslot") MONITOR_ATTR_ALIGN64 \
        static monitor_detail::ProbeNode _mon_node;                                 \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitormeta")                       \
        static const monitor_detail::TypeMetadata _mon_meta{                        \
            #var, __FILE__, __LINE__,                                                \
            static_cast<std::uint32_t>(_MonN * sizeof(_ElemT)),                      \
            monitor_detail::type_hash<_ElemT>(),                                     \
            &monitor_detail::ArrayPrinter<_ElemT, _MonN>::print};                    \
        monitor_detail::ensure_initialized(&_mon_node, &_mon_meta);                 \
        const _ElemT* _mon_ptr = &(var)[0];                                          \
        monitor_detail::write_bytes(&_mon_node.slot, _mon_ptr, _MonN * sizeof(_ElemT)); \
    } while (0)

// WARNING: Unsafe raw memory monitoring macros
// These macros bypass all type safety checks and directly read from the provided pointer.
// Caller is responsible for ensuring:
//   1. ptr is valid and non-null
//   2. ptr points to at least N bytes of accessible memory
//   3. N <= kProbeBufferSize
// Misuse will cause undefined behavior (crash, memory corruption, security vulnerabilities).

#define MONITOR_RAW_UNSAFE_DEC(ptr, n)                                               \
    do {                                                                             \
        const void* _mon_ptr = (ptr);                                                \
        constexpr std::size_t _MonN = (n);                                           \
        static_assert(_MonN <= monitor_detail::kProbeBufferSize,                     \
                      "MONITOR_RAW_UNSAFE_DEC requires N <= kProbeBufferSize bytes.");            \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitorslot") MONITOR_ATTR_ALIGN64 \
        static monitor_detail::ProbeNode _mon_raw_node;                             \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitormeta.unsafe")                \
        static const monitor_detail::TypeMetadata _mon_raw_meta{                    \
            #ptr "[DEC]", __FILE__, __LINE__,                                        \
            static_cast<std::uint32_t>(_MonN),                                       \
            0xDEADBEEFULL,                                                           \
            &monitor_detail::ArrayPrinter<std::uint8_t, _MonN>::print};              \
        monitor_detail::ensure_initialized(&_mon_raw_node, &_mon_raw_meta);         \
        monitor_detail::write_bytes(&_mon_raw_node.slot, _mon_ptr, _MonN);          \
    } while (0)

#define MONITOR_RAW_UNSAFE_HEX(ptr, n)                                               \
    do {                                                                             \
        const void* _mon_ptr = (ptr);                                                \
        constexpr std::size_t _MonN = (n);                                           \
        static_assert(_MonN <= monitor_detail::kProbeBufferSize,                     \
                      "MONITOR_RAW_UNSAFE_HEX requires N <= kProbeBufferSize bytes.");            \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitorslot") MONITOR_ATTR_ALIGN64 \
        static monitor_detail::ProbeNode _mon_hex_node;                             \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitormeta.unsafe")                \
        static const monitor_detail::TypeMetadata _mon_hex_meta{                    \
            #ptr "[HEX]", __FILE__, __LINE__,                                        \
            static_cast<std::uint32_t>(_MonN),                                       \
            0xCAFEBABEULL,                                                           \
            &monitor_detail::HexDumpPrinter<_MonN>::print};                          \
        monitor_detail::ensure_initialized(&_mon_hex_node, &_mon_hex_meta);         \
        monitor_detail::write_bytes(&_mon_hex_node.slot, _mon_ptr, _MonN);          \
    } while (0)

// MONITOR_SCHEMA: Schema-aware monitoring for SelfDescribingLog integration
// Usage: MONITOR_SCHEMA(my_struct_instance, my_schema);
// The schema parameter must be a valid LogSchema with isValid() == true
// Note: Large structs are truncated if they exceed kProbeBufferSize
#define MONITOR_SCHEMA(var, schema)                                                  \
    do {                                                                             \
        using _MonT = std::decay_t<decltype(var)>;                                   \
        static_assert(std::is_trivially_copyable_v<_MonT>,                           \
                      "MONITOR_SCHEMA requires trivially copyable type.");          \
        static constexpr std::size_t _MonCopySize =                                  \
            (sizeof(_MonT) <= monitor_detail::kProbeBufferSize)                      \
                ? sizeof(_MonT)                                                      \
                : monitor_detail::kProbeBufferSize;                                  \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitorslot") MONITOR_ATTR_ALIGN64 \
        static monitor_detail::SchemaProbeNode _mon_schema_node;                    \
        MONITOR_ATTR_USED MONITOR_ATTR_SECTION(".monitormeta.schema")                \
        static const monitor_detail::SchemaMetadata _mon_schema_meta{               \
            #var, __FILE__, __LINE__,                                                \
            static_cast<std::uint32_t>(_MonCopySize),                                \
            monitor_detail::kSchemaTypeHash,                                         \
            &(schema)};                                                              \
        monitor_detail::ensure_schema_initialized(&_mon_schema_node, &_mon_schema_meta); \
        monitor_detail::write_bytes(&_mon_schema_node.slot, &(var), _MonCopySize);   \
    } while (0)

#else
#define MONITOR(var) ((void)0)
#define MONITOR_N(var, n) ((void)0)
#define MONITOR_RAW_UNSAFE_DEC(ptr, n) ((void)0)
#define MONITOR_RAW_UNSAFE_HEX(ptr, n) ((void)0)
#define MONITOR_SCHEMA(var, schema) ((void)0)
#endif
