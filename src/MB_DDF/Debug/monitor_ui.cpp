#include "monitor_ui.hpp"

#include "monitor.hpp"
#include "../Tools/SelfDescribingLog.h"

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <deque>

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/terminal.hpp>

namespace {

constexpr std::size_t kLogCapacity = 16;
std::mutex g_log_mutex;
std::deque<std::string> g_log_lines;

void push_log_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    if (g_log_lines.size() >= kLogCapacity) {
        g_log_lines.pop_front();
    }
    g_log_lines.push_back(line);
}


std::vector<std::string> snapshot_logs() {
    std::lock_guard<std::mutex> lock(g_log_mutex);
    return std::vector<std::string>(g_log_lines.begin(), g_log_lines.end());
}

std::string file_basename(const char* path) {
    if (!path) {
        return {};
    }
    const char* last = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            last = p + 1;
        }
    }
    return std::string(last);
}

std::string ptr_hex(const void* ptr) {
    std::uintptr_t value = reinterpret_cast<std::uintptr_t>(ptr);
    char buf[24]{};
    std::snprintf(buf, sizeof(buf), "0x%016llX", static_cast<unsigned long long>(value));
    return std::string(buf);
}

// Format a single field value
std::string format_field_value(const uint8_t* base, uint32_t size, const MB_DDF::Tools::LogField& field) {
    if (!base) {
        return "<null>";
    }
    if (field.byte_offset == MB_DDF::Tools::LogSchema::kInvalidOffset) {
        return "<no offset>";
    }
    uint32_t field_size = MB_DDF::Tools::logFieldTypeSize(field.type);
    if (field_size == 0) {
        return "<unknown type>";
    }
    // Check if field is fully within the available data
    if (field.byte_offset + field_size > size) {
        return "<truncated>";
    }

    const void* field_data = base + field.byte_offset;
    char buf[64]{};

    switch (field.type) {
        case MB_DDF::Tools::LogFieldType::UInt8:
            std::snprintf(buf, sizeof(buf), "%u", *static_cast<const uint8_t*>(field_data));
            break;
        case MB_DDF::Tools::LogFieldType::Int8:
            std::snprintf(buf, sizeof(buf), "%d", *static_cast<const int8_t*>(field_data));
            break;
        case MB_DDF::Tools::LogFieldType::UInt16: {
            uint16_t v; std::memcpy(&v, field_data, 2);
            std::snprintf(buf, sizeof(buf), "%u", v);
            break;
        }
        case MB_DDF::Tools::LogFieldType::Int16: {
            int16_t v; std::memcpy(&v, field_data, 2);
            std::snprintf(buf, sizeof(buf), "%d", v);
            break;
        }
        case MB_DDF::Tools::LogFieldType::UInt32: {
            uint32_t v; std::memcpy(&v, field_data, 4);
            std::snprintf(buf, sizeof(buf), "%u", v);
            break;
        }
        case MB_DDF::Tools::LogFieldType::Int32: {
            int32_t v; std::memcpy(&v, field_data, 4);
            std::snprintf(buf, sizeof(buf), "%d", v);
            break;
        }
        case MB_DDF::Tools::LogFieldType::Float32: {
            float v; std::memcpy(&v, field_data, 4);
            std::snprintf(buf, sizeof(buf), "%.6g", v);
            break;
        }
        case MB_DDF::Tools::LogFieldType::Float64: {
            double v; std::memcpy(&v, field_data, 8);
            std::snprintf(buf, sizeof(buf), "%.6g", v);
            break;
        }
        case MB_DDF::Tools::LogFieldType::UInt64: {
            uint64_t v; std::memcpy(&v, field_data, 8);
            std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(v));
            break;
        }
        case MB_DDF::Tools::LogFieldType::Int64: {
            int64_t v; std::memcpy(&v, field_data, 8);
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
            break;
        }
        default:
            std::snprintf(buf, sizeof(buf), "?");
            break;
    }
    return std::string(buf);
}

// Format schema value according to LogSchema field definitions (single line summary)
// Format: "field1=value1, field2=value2, ..."
std::string format_schema_value(const void* data, uint32_t size, const MB_DDF::Tools::LogSchema* schema) {
    if (!data || !schema || !schema->isValid()) {
        return "<invalid schema>";
    }

    const auto& fields = schema->fields();
    if (fields.empty()) {
        return "<empty schema>";
    }

    std::string result;
    const uint8_t* base = static_cast<const uint8_t*>(data);

    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];
        if (i > 0) {
            result += ", ";
        }
        result += field.name;
        result += "=";

        std::string value = format_field_value(base, size, field);
        result += value;
    }

    return result;
}

struct AnsiStyle {
    bool bold{false};
    bool has_fg{false};
    bool has_bg{false};
    ftxui::Color fg{ftxui::Color::Default};
    ftxui::Color bg{ftxui::Color::Default};
};

ftxui::Color ansi_color(bool bright, int code) {
    struct RGB { int r; int g; int b; };
    auto base = [&](int c) -> RGB {
        switch (c) {
        case 0: return {0, 0, 0};
        case 1: return {200, 0, 0};
        case 2: return {0, 180, 0};
        case 3: return {200, 180, 0};
        case 4: return {60, 120, 220};
        case 5: return {200, 0, 200};
        case 6: return {0, 180, 180};
        case 7: return {200, 200, 200};
        default: return {200, 200, 200};
        }
    };
    RGB rgb = base(code);
    if (bright) {
        rgb.r = std::min(255, rgb.r + 55);
        rgb.g = std::min(255, rgb.g + 55);
        rgb.b = std::min(255, rgb.b + 55);
    }
    return ftxui::Color::RGB(rgb.r, rgb.g, rgb.b);
}

void apply_sgr_code(AnsiStyle& style, int code) {
    if (code == 0) {
        style = AnsiStyle{};
        return;
    }
    if (code == 1) {
        style.bold = true;
        return;
    }
    if (code == 22) {
        style.bold = false;
        return;
    }
    if (code == 39) {
        style.has_fg = false;
        style.fg = ftxui::Color::Default;
        return;
    }
    if (code == 49) {
        style.has_bg = false;
        style.bg = ftxui::Color::Default;
        return;
    }
    if (code >= 30 && code <= 37) {
        style.has_fg = true;
        style.fg = ansi_color(false, code - 30);
        return;
    }
    if (code >= 90 && code <= 97) {
        style.has_fg = true;
        style.fg = ansi_color(true, code - 90);
        return;
    }
    if (code >= 40 && code <= 47) {
        style.has_bg = true;
        style.bg = ansi_color(false, code - 40);
        return;
    }
    if (code >= 100 && code <= 107) {
        style.has_bg = true;
        style.bg = ansi_color(true, code - 100);
        return;
    }
}

ftxui::Element styled_text(const std::string& text, const AnsiStyle& style) {
    using namespace ftxui;
    Element elem = ftxui::text(text);
    if (style.bold) {
        elem = elem | bold;
    }
    if (style.has_fg) {
        elem = elem | color(style.fg);
    }
    if (style.has_bg) {
        elem = elem | bgcolor(style.bg);
    }
    return elem;
}

ftxui::Element parse_ansi_line(const std::string& line) {
    using namespace ftxui;
    std::vector<Element> parts;
    std::string buffer;
    AnsiStyle style;

    auto flush = [&]() {
        if (!buffer.empty()) {
            parts.push_back(styled_text(buffer, style));
            buffer.clear();
        }
    };

    std::size_t i = 0;
    while (i < line.size()) {
        if (line[i] == '\x1b' && i + 1 < line.size() && line[i + 1] == '[') {
            std::size_t mpos = line.find('m', i + 2);
            if (mpos == std::string::npos) {
                buffer.push_back(line[i]);
                ++i;
                continue;
            }
            flush();
            std::string seq = line.substr(i + 2, mpos - (i + 2));
            if (seq.empty()) {
                apply_sgr_code(style, 0);
            } else {
                std::size_t start = 0;
                while (start <= seq.size()) {
                    std::size_t end = seq.find(';', start);
                    if (end == std::string::npos) {
                        end = seq.size();
                    }
                    std::string token = seq.substr(start, end - start);
                    int code = token.empty() ? 0 : std::atoi(token.c_str());
                    apply_sgr_code(style, code);
                    if (end == seq.size()) {
                        break;
                    }
                    start = end + 1;
                }
            }
            i = mpos + 1;
        } else {
            buffer.push_back(line[i]);
            ++i;
        }
    }
    flush();

    if (parts.empty()) {
        return text("");
    }
    if (parts.size() == 1) {
        return parts.front();
    }
    return hbox(std::move(parts));
}

// Unified row type for both regular and schema probes
struct Row {
    std::string location;      // file:line
    std::string schema_name;   // empty for regular probes, schema-based name for schema probes
    std::string name;          // variable name or field name
    std::string value;         // formatted value
    bool changed{false};
    bool is_schema{false};     // true if this is a schema probe
    bool is_schema_field{false}; // true if this is a field row of a schema probe
    std::string parent_name;   // for schema fields: the parent variable name
};

// Collect rows from both regular probes and schema probes
// Uses slot pointer as key for change detection
// Schema probes are expanded to multiple rows (one per field)
std::vector<Row> collect_rows(
    std::unordered_map<const MonitorHub::ProbeSlot*, std::string>& last_values) {
    std::vector<Row> new_rows;

    // Collect regular probes
    auto snaps = MonitorHub::snapshot();
    new_rows.reserve(snaps.size());

    for (const auto& snap : snaps) {
        if (!snap.meta || !snap.slot) {
            continue;
        }
        std::string value = MonitorHub::format_value(snap);
        std::string location = file_basename(snap.meta->file) + ":" + std::to_string(snap.meta->line);

        bool changed = true;
        auto it = last_values.find(snap.slot);
        if (it != last_values.end() && it->second == value) {
            changed = false;
        }
        last_values[snap.slot] = value;

        new_rows.push_back(Row{location, "", snap.meta->name, value, changed, false, false, ""});
    }

    // Collect schema probes - expand each field to a separate row
    auto schema_snaps = MonitorHub::snapshot_schema();

    for (const auto& snap : schema_snaps) {
        if (!snap.meta || !snap.slot) {
            continue;
        }

        std::string location = file_basename(snap.meta->file) + ":" + std::to_string(snap.meta->line);
        std::string parent_name = snap.meta->name ? snap.meta->name : "unknown";

        // Build full value string for change detection
        std::string full_value;
        if (snap.meta->schema && snap.meta->schema->isValid()) {
            full_value = format_schema_value(snap.data.data(), snap.size, snap.meta->schema);
        } else {
            full_value = "<invalid schema>";
        }

        bool parent_changed = true;
        auto it = last_values.find(snap.slot);
        if (it != last_values.end() && it->second == full_value) {
            parent_changed = false;
        }
        last_values[snap.slot] = full_value;

        // Add header row for schema probe - show struct name and field count
        std::string header_value;
        if (snap.meta->schema && snap.meta->schema->isValid()) {
            header_value = "[" + std::to_string(snap.meta->schema->fields().size()) + " fields]";
        } else {
            header_value = "<invalid>";
        }
        new_rows.push_back(Row{location, "", parent_name, header_value, parent_changed, true, false, ""});

        // Add field rows - field name in 'property' column, name column shows parent name
        if (snap.meta->schema && snap.meta->schema->isValid()) {
            const auto& fields = snap.meta->schema->fields();
            const uint8_t* base = reinterpret_cast<const uint8_t*>(snap.data.data());
            uint32_t data_size = snap.size;

            for (const auto& field : fields) {
                std::string field_value = format_field_value(base, data_size, field);
                // Field name goes to 'property' column, name column shows parent variable name
                new_rows.push_back(Row{"", field.name, parent_name, field_value, parent_changed, true, true, parent_name});
            }
        }
    }

    return new_rows;
}

} // namespace

namespace MB_DDF::MonitorUI {

void run_monitor_table(std::function<void()> update_fn, std::chrono::milliseconds interval) {
    run_monitor_table(false, std::move(update_fn), interval);
}

void run_monitor_table(bool blocking, std::function<void()> update_fn, std::chrono::milliseconds interval) {
    using namespace ftxui;
    std::thread worker([update_fn = std::move(update_fn), interval]() mutable {
        std::mutex rows_mutex;
        std::vector<Row> rows;
        std::unordered_map<const MonitorHub::ProbeSlot*, std::string> last_values;
        std::atomic<bool> running{true};
        std::size_t selected_index = 0;
        int page_rows = 1;
        ScreenInteractive screen = ScreenInteractive::Fullscreen();

        std::thread updater([&]() {
            while (running.load()) {
                if (update_fn) {
                    update_fn();
                }
                {
                    std::lock_guard<std::mutex> lock(rows_mutex);
                    rows = collect_rows(last_values);
                }
                screen.PostEvent(Event::Custom);
                std::this_thread::sleep_for(interval);
            }
        });

        auto renderer = Renderer([&]() {
            std::vector<Row> snapshot_rows;
            {
                std::lock_guard<std::mutex> lock(rows_mutex);
                snapshot_rows = rows;
            }
            auto logs = snapshot_logs();

            auto pad = [](const std::string& s) { return " " + s + " "; };
            auto term = ftxui::Terminal::Size();
            int log_panel_height = static_cast<int>(kLogCapacity + 1 + 2);
            int fixed_height = 2 + 1 + log_panel_height;
            int max_table_rows = std::max(3, term.dimy - fixed_height);
            page_rows = std::max(1, max_table_rows - 3);
            std::vector<std::vector<Element>> table_data;
            table_data.push_back({
                text(pad("位置")) | bold | color(Color::RGB(120, 220, 255)),
                text(pad("名字")) | bold | color(Color::RGB(120, 220, 255)),
                text(pad("属性")) | bold | color(Color::RGB(120, 220, 255)),
                text("数值") | bold | color(Color::RGB(120, 220, 255)),
            });
            if (snapshot_rows.empty()) {
                selected_index = 0;
            } else if (selected_index >= snapshot_rows.size()) {
                selected_index = snapshot_rows.size() - 1;
            }
            for (std::size_t i = 0; i < snapshot_rows.size(); ++i) {
                const auto& row = snapshot_rows[i];
                Element location_cell = text(pad(row.location));
                Element schema_cell = text(pad(row.schema_name));
                Element name_cell = text(pad(row.name));
                Element value_cell = text(row.value);

                // Only parent rows highlight on change; field rows don't highlight individually
                if (!row.is_schema_field && row.changed) {
                    value_cell = value_cell | bgcolor(Color::Yellow) | color(Color::Black);
                }

                if (i == selected_index) {
                    if (row.is_schema_field) {
                        // Field rows: focus on property column (schema_name)
                        schema_cell = focus(schema_cell);
                    } else {
                        location_cell = focus(location_cell);
                    }
                }
                table_data.push_back({
                    location_cell,
                    name_cell,
                    schema_cell,
                    value_cell,
                });
            }

            Table table(table_data);
            table.SelectAll().Border();
            table.SelectAll().Separator();
            std::vector<Element> log_elems;
            log_elems.reserve(kLogCapacity + 1);
            log_elems.push_back(text("Logs") | bold | color(Color::RGB(120, 220, 255)));
            for (const auto& line : logs) {
                log_elems.push_back(parse_ansi_line(line));
            }
            while (log_elems.size() < kLogCapacity + 1) {
                log_elems.push_back(text(""));
            }

            auto log_panel = vbox(log_elems) | border;

            auto table_view = table.Render() | yframe | vscroll_indicator | yflex;
            return vbox({
                text("Monitor Snapshot (1s refresh, q to quit, \u2191\u2193 scroll)") | bold,
                separator(),
                table_view,
                separator(),
                log_panel,
            });
        });

        auto app = CatchEvent(renderer, [&](Event e) {
            auto rows_count = [&]() {
                std::lock_guard<std::mutex> lock(rows_mutex);
                return rows.size();
            };
            if (e == Event::Character('q') || e == Event::Escape) {
                running.store(false);
                screen.Exit();
                return true;
            }
            if (e == Event::ArrowUp) {
                if (selected_index > 0) {
                    selected_index -= 1;
                }
                return true;
            }
            if (e == Event::ArrowDown) {
                auto size = rows_count();
                if (selected_index + 1 < size) {
                    selected_index += 1;
                }
                return true;
            }
            if (e == Event::PageUp) {
                std::size_t delta = static_cast<std::size_t>(page_rows);
                selected_index = (selected_index > delta) ? (selected_index - delta) : 0;
                return true;
            }
            if (e == Event::PageDown) {
                auto size = rows_count();
                if (size == 0) {
                    return true;
                }
                std::size_t delta = static_cast<std::size_t>(page_rows);
                selected_index = std::min(selected_index + delta, size - 1);
                return true;
            }
            if (e == Event::Home) {
                selected_index = 0;
                return true;
            }
            if (e == Event::End) {
                auto size = rows_count();
                selected_index = size > 0 ? size - 1 : 0;
                return true;
            }
            return false;
        });

        screen.Loop(app);
        running.store(false);
        if (updater.joinable()) {
            updater.join();
        }
    });
    if (blocking) {
        worker.join();
    } else {
        worker.detach();
    }
}

void log_line(const std::string& line) {
    push_log_line(line);
}

} // namespace MonitorUI
