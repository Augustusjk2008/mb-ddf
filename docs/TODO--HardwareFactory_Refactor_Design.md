# HardwareFactory 配置化架构设计方案

## 版本信息
- 版本: v1.0
- 日期: 2026-02-12
- 作者: 架构设计专家团队

---

## 一、现状分析

### 当前实现的问题

当前 `HardwareFactory` 使用硬编码的 if-else 字符串匹配：

```cpp
std::shared_ptr<DDS::Handle> HardwareFactory::create(const std::string& name, void* param) {
    if (name == "can") {
        // 硬编码配置...
        tc.device_offset = 0x50000;
        tc.event_number = 5;
        h->mtu = 40;
        // ...
    } else if (name == "helm") {
        // 硬编码配置...
    } else if (name == "imu") {
        // 硬编码配置...
    }
    // ... 更多硬编码
}
```

**问题列表：**

| 问题 | 影响 | 严重程度 |
|------|------|----------|
| 硬编码配置 | 修改配置需重新编译 | 高 |
| `void*` 参数不透明 | 类型不安全，调用者需了解内部实现 | 高 |
| 难以扩展 | 新增设备类型需修改工厂类，违反开闭原则 | 高 |
| 配置与代码耦合 | 无法动态调整设备参数 | 中 |
| 重复代码 | 各设备类型初始化逻辑高度相似但无法复用 | 中 |

---

## 二、设计目标

1. **类型安全**：使用强类型配置结构替代 `void*`
2. **分层配置**：硬件配置 / 设备配置 / 链路配置 三层分离
3. **零开销抽象**：配置在编译期或初始化期解析，运行时无额外开销
4. **向后兼容**：保留现有 API，支持渐进式迁移
5. **嵌入式友好**：支持静态配置（编译期）和最小化动态内存分配
6. **插件式扩展**：新增设备无需修改工厂源码

---

## 三、核心架构设计

### 3.1 架构概览

```
┌─────────────────────────────────────────────────────────────────┐
│                     HardwareFactory (Facade)                     │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ Preset Configs│ │ Config Builders│ │ Config Parser (JSON)   │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                     Device Registry (可扩展)                      │
│  ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐          │
│  │ CanFactory │ │ HelmFactory│ │Rs422Factory│ │ UdpFactory │ ...      │
│  └──────────┘ └──────────┘ └──────────┘ └──────────┘          │
├─────────────────────────────────────────────────────────────────┤
│                     底层设备实现                                  │
│  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐            │
│  │ XdmaTransport  │ │   UdpLink    │ │   SpiLink    │            │
│  └──────────────┘ └──────────────┘ └──────────────┘            │
└─────────────────────────────────────────────────────────────────┘
```

---

## 四、详细设计方案

### 4.1 配置数据结构

**文件**: `src/MB_DDF/PhysicalLayer/Factory/DeviceConfig.h`

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <variant>
#include <optional>

namespace MB_DDF {
namespace PhysicalLayer {
namespace Factory {

// ============================================
// 基础传输层配置
// ============================================

struct XdmaConfig {
    std::string device_path{"/dev/xdma0"};
    uint32_t device_offset{0};
    int event_number{-1};
    int dma_h2c_channel{-1};
    int dma_c2h_channel{-1};
};

struct UdpConfig {
    std::string local_ip{"0.0.0.0"};
    uint16_t local_port{0};
    std::optional<std::string> remote_ip;
    std::optional<uint16_t> remote_port;
};

struct SpiConfig {
    std::string device_path{"/dev/spidev0.0"};
    uint32_t speed_hz{1'000'000};
    uint8_t mode{0};    // SPI_MODE_0
    uint8_t bits{8};
};

// ============================================
// 设备特定配置
// ============================================

struct CanDeviceConfig {
    XdmaConfig transport;
    uint32_t mtu{40};
    struct BitTiming {
        uint8_t prescaler{1};
        uint8_t ts1{7};
        uint8_t ts2{2};
        uint8_t sjw{0};
    } bit_timing;
    bool loopback{false};
    bool accept_all_filter{true};
};

struct HelmDeviceConfig {
    XdmaConfig transport;
    uint32_t mtu{16};
    uint16_t pwm_freq{8000};
    uint16_t out_enable{0xF};
    uint16_t ad_filter{1};
};

struct Rs422DeviceConfig {
    XdmaConfig transport;
    uint32_t mtu{255};
    uint8_t ucr{0x30};
    uint8_t mcr{0x20};
    uint8_t brsr{0x0A};
    uint8_t icr{0x01};
    uint8_t tx_head_lo{0xAA};
    uint8_t tx_head_hi{0x1A};
    uint8_t rx_head_lo{0xAA};
    uint8_t rx_head_hi{0x1A};
    uint8_t lpb{0x00};
    uint8_t intr{0xAE};
    uint16_t evt{1250};
};

struct DdrDeviceConfig {
    XdmaConfig transport;
    uint32_t mtu{640 * 1024};  // 默认 640KB
};

struct UdpDeviceConfig {
    UdpConfig transport;
    uint32_t mtu{60000};
};

struct SpiDeviceConfig {
    SpiConfig transport;
    uint32_t mtu{4096};
};

// ============================================
// 统一配置 Variant
// ============================================

using DeviceConfigVariant = std::variant<
    CanDeviceConfig,
    HelmDeviceConfig,
    Rs422DeviceConfig,
    DdrDeviceConfig,
    UdpDeviceConfig,
    SpiDeviceConfig
>;

// ============================================
// 预定义配置模板（便于快速配置）
// ============================================

namespace Preset {

// IMU 预设配置（基于现有硬编码值）
inline Rs422DeviceConfig IMU() {
    Rs422DeviceConfig cfg;
    cfg.transport.device_offset = 0x10000;
    cfg.transport.event_number = 1;
    cfg.brsr = 0x0A;
    cfg.tx_head_lo = 0xAA;
    cfg.tx_head_hi = 0x1A;
    cfg.rx_head_lo = 0xAA;
    cfg.rx_head_hi = 0x1A;
    return cfg;
}

// 导引头预设配置
inline Rs422DeviceConfig DYT() {
    Rs422DeviceConfig cfg;
    cfg.transport.device_offset = 0x20000;
    cfg.transport.event_number = 2;
    cfg.brsr = 0x0A;
    return cfg;
}

// 引信预设配置
inline Rs422DeviceConfig YX() {
    Rs422DeviceConfig cfg;
    cfg.transport.device_offset = 0x00000;
    cfg.transport.event_number = 0;
    cfg.brsr = 0x09;
    cfg.tx_head_lo = 0xAA;
    cfg.tx_head_hi = 0x56;
    cfg.rx_head_lo = 0x55;
    cfg.rx_head_hi = 0xA6;
    return cfg;
}

// CAN 预设配置
inline CanDeviceConfig CAN() {
    CanDeviceConfig cfg;
    cfg.transport.device_offset = 0x50000;
    cfg.transport.event_number = 5;
    cfg.mtu = 40;
    cfg.bit_timing = {1, 7, 2, 0};  // 500Kbps @ 24MHz
    return cfg;
}

// Helm 预设配置
inline HelmDeviceConfig HELM() {
    HelmDeviceConfig cfg;
    cfg.transport.device_offset = 0x60000;
    cfg.mtu = 16;
    cfg.pwm_freq = 8000;
    cfg.out_enable = 0xF;
    cfg.ad_filter = 1;
    return cfg;
}

// DDR 预设配置
inline DdrDeviceConfig DDR() {
    DdrDeviceConfig cfg;
    cfg.transport.dma_h2c_channel = 0;
    cfg.transport.dma_c2h_channel = 0;
    cfg.transport.device_offset = 0x60000;
    cfg.transport.event_number = 6;
    cfg.mtu = 640 * 1024;
    return cfg;
}

} // namespace Preset

} // namespace Factory
} // namespace PhysicalLayer
} // namespace MB_DDF
```

---

### 4.2 新版 HardwareFactory 接口

**文件**: `src/MB_DDF/PhysicalLayer/Factory/HardwareFactory.h`

```cpp
#pragma once
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include "MB_DDF/DDS/DDSHandle.h"
#include "DeviceConfig.h"

namespace MB_DDF {
namespace PhysicalLayer {
namespace Factory {

// ============================================
// 设备注册表（支持插件扩展）
// ============================================
class DeviceRegistry {
public:
    using CreatorFunc = std::function<std::shared_ptr<DDS::Handle>(const DeviceConfigVariant&)>;

    static DeviceRegistry& instance();

    // 注册设备创建器
    bool registerDevice(const std::string& type_name, CreatorFunc creator);

    // 创建设备
    std::shared_ptr<DDS::Handle> create(const std::string& type_name, const DeviceConfigVariant& config) const;

    // 检查设备类型是否已注册
    bool isRegistered(const std::string& type_name) const;

    // 获取所有已注册类型
    std::vector<std::string> getRegisteredTypes() const;

private:
    DeviceRegistry() = default;
    std::unordered_map<std::string, CreatorFunc> creators_;
    mutable std::shared_mutex mutex_;
};

// ============================================
// HardwareFactory 主类
// ============================================
class HardwareFactory {
public:
    // ============================================
    // 新版类型安全 API（推荐）
    // ============================================

    // 通过具体配置创建 Handle
    static std::shared_ptr<DDS::Handle> create(const CanDeviceConfig& config);
    static std::shared_ptr<DDS::Handle> create(const HelmDeviceConfig& config);
    static std::shared_ptr<DDS::Handle> create(const Rs422DeviceConfig& config);
    static std::shared_ptr<DDS::Handle> create(const DdrDeviceConfig& config);
    static std::shared_ptr<DDS::Handle> create(const UdpDeviceConfig& config);
    static std::shared_ptr<DDS::Handle> create(const SpiDeviceConfig& config);

    // 通过 Variant 创建（适用于配置在运行时决定）
    static std::shared_ptr<DDS::Handle> create(const DeviceConfigVariant& config);

    // ============================================
    // 向后兼容 API（保留）
    // ============================================

    // 传统字符串方式，内部映射到预设配置
    [[deprecated("Use type-safe config API instead")]]
    static std::shared_ptr<DDS::Handle> create(const std::string& name, void* param = nullptr);

    // 注册自定义设备创建器（扩展机制）
    static bool registerDevice(const std::string& type_name,
                               DeviceRegistry::CreatorFunc creator);
};

// ============================================
// 配置构建器（流式 API）
// ============================================

class Rs422ConfigBuilder {
public:
    Rs422ConfigBuilder& devicePath(std::string path) {
        config_.transport.device_path = std::move(path);
        return *this;
    }
    Rs422ConfigBuilder& offset(uint32_t off) {
        config_.transport.device_offset = off;
        return *this;
    }
    Rs422ConfigBuilder& event(int evt) {
        config_.transport.event_number = evt;
        return *this;
    }
    Rs422ConfigBuilder& baudRateDiv(uint8_t brsr) {
        config_.brsr = brsr;
        return *this;
    }
    Rs422ConfigBuilder& txHeader(uint8_t lo, uint8_t hi) {
        config_.tx_head_lo = lo;
        config_.tx_head_hi = hi;
        return *this;
    }
    Rs422ConfigBuilder& rxHeader(uint8_t lo, uint8_t hi) {
        config_.rx_head_lo = lo;
        config_.rx_head_hi = hi;
        return *this;
    }
    Rs422ConfigBuilder& mtu(uint32_t m) {
        config_.mtu = m;
        return *this;
    }

    Rs422DeviceConfig build() const { return config_; }
    std::shared_ptr<DDS::Handle> create() const {
        return HardwareFactory::create(config_);
    }

private:
    Rs422DeviceConfig config_;
};

class CanConfigBuilder {
public:
    CanConfigBuilder& devicePath(std::string path) {
        config_.transport.device_path = std::move(path);
        return *this;
    }
    CanConfigBuilder& offset(uint32_t off) {
        config_.transport.device_offset = off;
        return *this;
    }
    CanConfigBuilder& event(int evt) {
        config_.transport.event_number = evt;
        return *this;
    }
    CanConfigBuilder& bitTiming(uint8_t prescaler, uint8_t ts1, uint8_t ts2, uint8_t sjw) {
        config_.bit_timing = {prescaler, ts1, ts2, sjw};
        return *this;
    }
    CanConfigBuilder& baudRate(uint32_t baud);
    CanConfigBuilder& loopback(bool on) {
        config_.loopback = on;
        return *this;
    }
    CanConfigBuilder& mtu(uint32_t m) {
        config_.mtu = m;
        return *this;
    }

    CanDeviceConfig build() const { return config_; }
    std::shared_ptr<DDS::Handle> create() const {
        return HardwareFactory::create(config_);
    }

private:
    CanDeviceConfig config_;
};

} // namespace Factory
} // namespace PhysicalLayer
} // namespace MB_DDF
```

---

### 4.3 配置文件支持（JSON）

**文件**: `src/MB_DDF/PhysicalLayer/Factory/ConfigParser.h`

```cpp
#pragma once
#include "DeviceConfig.h"
#include <string>
#include <vector>

namespace MB_DDF {
namespace PhysicalLayer {
namespace Factory {

// ============================================
// JSON 配置解析器
// ============================================
class JsonConfigParser {
public:
    // 解析单个设备配置
    static std::optional<DeviceConfigVariant> parseDevice(const std::string& json_str);
    static std::optional<DeviceConfigVariant> parseDeviceFromFile(const std::string& filepath);

    // 解析多设备配置
    static std::vector<std::pair<std::string, DeviceConfigVariant>>
        parseDevices(const std::string& json_str);
    static std::vector<std::pair<std::string, DeviceConfigVariant>>
        parseDevicesFromFile(const std::string& filepath);

    // 生成配置模板
    static std::string generateTemplate(const std::string& device_type);

    // 验证配置
    static bool validate(const std::string& json_str, std::string* error_msg = nullptr);
};

} // namespace Factory
} // namespace PhysicalLayer
} // namespace MB_DDF
```

**配置文件示例** (`configs/devices.json`):

```json
{
    "version": "1.0",
    "devices": [
        {
            "name": "imu",
            "type": "rs422",
            "description": "IMU 串口设备",
            "transport": {
                "device_path": "/dev/xdma0",
                "device_offset": "0x10000",
                "event_number": 1
            },
            "mtu": 255,
            "registers": {
                "ucr": "0x30",
                "mcr": "0x20",
                "brsr": "0x0A",
                "icr": "0x01"
            },
            "frame_header": {
                "tx": ["0xAA", "0x1A"],
                "rx": ["0xAA", "0x1A"]
            },
            "lpb": "0x00",
            "intr": "0xAE",
            "evt": 1250
        },
        {
            "name": "can0",
            "type": "can",
            "description": "CAN 总线设备",
            "transport": {
                "device_path": "/dev/xdma0",
                "device_offset": "0x50000",
                "event_number": 5
            },
            "mtu": 40,
            "baudrate": 500000,
            "loopback": false,
            "accept_all_filter": true
        },
        {
            "name": "helm0",
            "type": "helm",
            "description": "舵机控制设备",
            "transport": {
                "device_path": "/dev/xdma0",
                "device_offset": "0x60000",
                "event_number": 3
            },
            "mtu": 16,
            "pwm_freq": 8000,
            "out_enable": "0xF",
            "ad_filter": 1
        },
        {
            "name": "udp_link",
            "type": "udp",
            "description": "UDP 通信链路",
            "transport": {
                "local_ip": "192.168.1.100",
                "local_port": 8080,
                "remote_ip": "192.168.1.200",
                "remote_port": 8080
            },
            "mtu": 60000
        }
    ]
}
```

---

## 五、使用示例

### 5.1 传统方式（向后兼容）

```cpp
// 保持现有代码不变
auto h_can = MB_DDF::PhysicalLayer::Factory::HardwareFactory::create("can", nullptr);
auto h_imu = MB_DDF::PhysicalLayer::Factory::HardwareFactory::create("imu", nullptr);
```

### 5.2 类型安全配置（推荐）

```cpp
using namespace MB_DDF::PhysicalLayer::Factory;

// 使用预设配置
auto h_imu = HardwareFactory::create(Preset::IMU());
auto h_can = HardwareFactory::create(Preset::CAN());

// 基于预设修改
auto cfg = Preset::IMU();
cfg.brsr = 0x0B;  // 修改波特率
cfg.mtu = 512;    // 修改 MTU
auto h_imu_custom = HardwareFactory::create(cfg);
```

### 5.3 流式构建器

```cpp
// 使用 Builder 模式
auto h_can = CanConfigBuilder()
    .devicePath("/dev/xdma0")
    .offset(0x50000)
    .event(5)
    .baudRate(1000000)  // 1Mbps
    .loopback(false)
    .mtu(40)
    .create();

auto h_rs422 = Rs422ConfigBuilder()
    .devicePath("/dev/xdma0")
    .offset(0x20000)
    .event(2)
    .baudRateDiv(0x0A)
    .txHeader(0xAA, 0x1A)
    .rxHeader(0xAA, 0x1A)
    .mtu(255)
    .create();
```

### 5.4 配置文件方式

```cpp
// 从文件加载配置
auto configs = JsonConfigParser::parseDevicesFromFile("/etc/mb_ddf/devices.json");
for (const auto& [name, cfg] : configs) {
    auto handle = HardwareFactory::create(cfg);
    device_manager.registerDevice(name, handle);
}
```

### 5.5 插件式扩展

```cpp
// 注册自定义设备类型
class CustomDeviceFactory {
public:
    static std::shared_ptr<DDS::Handle> create(const DeviceConfigVariant& cfg) {
        // 自定义创建设备逻辑
    }
};

// 在程序初始化时注册
HardwareFactory::registerDevice("custom", CustomDeviceFactory::create);

// 之后就可以通过配置文件使用
auto handle = HardwareFactory::create(loadConfigFromFile("custom.json"));
```

---

## 六、迁移策略

| 阶段 | 工作 | 时间估计 |
|------|------|----------|
| **Phase 1** | 添加 `DeviceConfig.h` 和新的类型安全 API，保留旧 API | 1-2 天 |
| **Phase 2** | 实现 `ConfigBuilder` 类，提供流式配置接口 | 1 天 |
| **Phase 3** | 添加 JSON 配置解析器（可选，依赖 json 库） | 2-3 天 |
| **Phase 4** | 逐步将现有代码迁移到新 API | 持续 |
| **Phase 5** | 标记旧 API 为 deprecated，最终移除 | 未来版本 |

---

## 七、关键文件变更

| 文件 | 操作 | 说明 |
|------|------|------|
| `src/MB_DDF/PhysicalLayer/Factory/DeviceConfig.h` | 新增 | 配置数据结构定义 |
| `src/MB_DDF/PhysicalLayer/Factory/ConfigParser.h` | 新增 | JSON 配置解析 |
| `src/MB_DDF/PhysicalLayer/Factory/HardwareFactory.h` | 修改 | 添加新 API，保留旧 API |
| `src/MB_DDF/PhysicalLayer/Factory/HardwareFactory.cpp` | 修改 | 实现新 API，重构内部逻辑 |
| `configs/devices.json` | 新增 | 示例配置文件 |

---

## 八、优势总结

1. **类型安全**：编译期检查配置参数，消除 `void*` 的类型风险
2. **可维护性**：配置与代码分离，修改配置无需重新编译
3. **可测试性**：易于注入 mock 配置进行单元测试
4. **可扩展性**：通过注册机制支持新设备类型，无需修改工厂类
5. **向后兼容**：现有代码无需修改即可继续使用
6. **嵌入式友好**：支持静态配置，无运行时开销；可选动态配置解析
