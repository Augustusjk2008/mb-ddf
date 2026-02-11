# MB_DDF 使用手册（含 Demo）

本文档聚焦 MB_DDF 的常用能力与接口含义，并配套本仓库的 Demo 代码讲解。所有 Demo 以“头文件形式”独立存在，最终在 [main.cpp](../src/Demo/main.cpp) 中统一调用。

## 常用命令（开发机 -> 目标设备 192.168.137.100）

以下命令用于在 Windows 开发机上完成编译、连接目标板、清理 SSH 旧指纹、以及拷贝可执行文件到目标板。

```bash
./build.bat clean
./build.bat debug
./build.bat release

ssh root@192.168.137.100
ssh-keygen -R 192.168.137.100
scp .\build\aarch64\Release\DemoProject root@192.168.137.100:/home/sast8/tmp/Demo
```

- `./build.bat clean/debug/release`
  - 用途：调用 `build.ps1` 执行交叉编译（aarch64 Linux），并把可执行文件名固定为 `DemoProject`（见 [build.bat](../build.bat)）。
  - 输出：`.\build\aarch64\<Debug|Release>\DemoProject`
- `ssh root@192.168.137.100`
  - 用途：SSH 连接目标设备（用户名 root，IP 为 192.168.137.100）。
- `ssh-keygen -R 192.168.137.100`
  - 用途：当目标设备重装系统/重刷镜像导致 SSH host key 变化时，删除本机 `known_hosts` 中旧记录，避免 “REMOTE HOST IDENTIFICATION HAS CHANGED” 报错。
- `scp .\build\aarch64\Release\DemoProject root@192.168.137.100:/home/sast8/tmp/Demo`
  - 用途：把 Release 产物拷贝到目标设备目录 `/home/sast8/tmp/Demo`（可按需要改路径）。

## 目录

- 0. 常用命令（开发机 -> 目标设备 192.168.137.100）
- 1. DDS：初始化、发布/订阅、回调与主动读取
  - Demo：DDS 初始化与发布订阅
  - Demo：订阅回调读取 + 主动查询读取（latest/next）
- 2. PhysicalLayer：硬件工厂（HardwareFactory）与 DDS::Handle
  - Demo：全部硬件工厂示例
- 3. DDS 便捷封装：PubAndSub
  - Demo：通过 PubAndSub 快速创建硬件工厂的 DDS 接口
- 4. Timer：SystemTimer（高精度周期定时）
  - Demo：SystemTimer 用法
- 5. Timer：ChronoHelper（计时与周期抖动统计）
  - Demo：ChronoHelper 用法
- 6. Debug：Logger（流式日志）
  - Demo：Logger 各种用法
- 7. 运行入口：main.cpp

---

## 1. DDS（共享内存发布订阅）

### 1.0 DDS 的功能与作用（为什么需要 DDS）

MB_DDF 的 DDS（Data Distribution Service）用于在同一台设备上的多个进程/模块之间高效分发数据，核心作用是“解耦数据生产者与消费者”，并提供可复用的发布/订阅通信范式。

它解决的典型问题：

- 多模块共享数据：例如传感器采集进程发布数据，融合/控制/记录等多个进程订阅同一 Topic。
- 降低耦合：发布者只关心“向 Topic 写”，订阅者只关心“从 Topic 读/回调”，双方无需彼此依赖。
- 高性能 IPC：当前实现基于共享内存 + 无锁环形缓冲区，避免频繁拷贝与内核态切换，适合高频/大带宽数据。
- 多订阅者并发：同一 Topic 可被多个订阅者独立读取，互不干扰。
- 两种消费方式：
  - 回调模式：内部线程驱动，数据到达即处理。
  - 轮询模式：上层主循环主动 `read()`，可按自己的节奏控制。

适用场景（示例）：

- 状态类 Topic：姿态/位置/健康状态等，订阅者更常读取“最新值”（`latest=true`）。
- 队列类 Topic：事件/指令/告警等，订阅者更常逐条读取（`latest=false`）。
- 调试与观测：配合日志/监控模块可定位 Topic 是否有发布者、订阅者是否活跃等。

### 1.1 核心类型与职责

- `MB_DDF::DDS::DDSCore`：DDS 单例入口，负责共享内存初始化、Topic 注册表、RingBuffer 创建与管理。
- `MB_DDF::DDS::Publisher`：发布端，向 Topic 写入消息。
- `MB_DDF::DDS::Subscriber`：订阅端，从 Topic 读取消息；可用回调（异步线程）或主动轮询读取。

相关头文件：

- [DDSCore.h](../src/MB_DDF/DDS/DDSCore.h)
- [Publisher.h](../src/MB_DDF/DDS/Publisher.h)
- [Subscriber.h](../src/MB_DDF/DDS/Subscriber.h)

### 1.2 DDS 的实现要点（当前工程）

当前 DDS 主要由三部分组成：

- 共享内存管理：`SharedMemoryManager` 负责创建/映射共享内存区域（默认名 `/MB_DDF_SHM`）。
- Topic 注册表：`TopicRegistry` 维护 Topic 元信息，用于跨进程发现与复用同一个 Topic 的共享内存布局。
- Topic 数据区：每个 Topic 在共享内存内对应一个 `RingBuffer`，用于保存消息并支持多订阅者读取。

数据流（共享内存 Topic）概念上是：

- Publisher 调用 `publish()` -> 向 RingBuffer 写入一条消息（消息头 + 载荷，可选校验）
- Subscriber：
  - 回调模式：内部线程等待/轮询 RingBuffer，有数据则读取并调用用户回调
  - 轮询模式：上层显式调用 `read()` 拉取数据

### 1.2 初始化：DDSCore::initialize

```cpp
auto& dds = MB_DDF::DDS::DDSCore::instance();
bool ok = dds.initialize(128 * 1024 * 1024);
```

- `shared_memory_size`：共享内存大小（字节），默认 128MB；最小 1MB（小于会失败）。
- 初始化成功后，TopicRegistry 与共享内存布局可被其它进程发现与复用。

### 1.3 Topic 命名规则（必须包含 `://`）

DDS 的 Topic 名称需要满足 `domain://address` 形式（只做最基本校验：域与地址均非空）。

例如：

- `demo://dds/read_modes`
- `rt://nav/pose`
- `shm://camera/frame`

相关实现：

- [TopicRegistry::is_valid_topic_name](../src/MB_DDF/DDS/TopicRegistry.cpp#L212-L240)

### 1.4 发布：DDSCore::create_publisher / Publisher::publish

```cpp
auto pub = dds.create_publisher("demo://dds/init_pubsub");
pub->publish(data_ptr, data_len);
```

- `create_publisher(topic_name, enable_checksum=true)`：创建共享内存 Topic 发布者。
- `enable_checksum`：是否对消息启用校验（用于发现共享内存数据损坏/覆盖等异常）。
- `Publisher::publish(data,size)`：写入一条消息（载荷为原始字节）。

补充：零拷贝写入（当前已提供接口）

- `Publisher::begin_message(max_size)`：预留一块可写区域，写完后提交（适合大 payload）。
- `Publisher::publish_fill(max_size, fill_cb)`：由回调填充 payload 并发布，避免额外拷贝。

### 1.5 订阅：回调模式 vs 主动读取模式

回调模式（异步线程触发）：

```cpp
auto sub = dds.create_subscriber("demo://dds/read_modes", true,
  [](const void* data, size_t size, uint64_t ts_ns) { /* ... */ });
```

- `MessageCallback`：`void(const void*, size_t, uint64_t)`，第三个参数为纳秒时间戳（共享内存消息）。
- 创建订阅者时传入 callback，会在内部启动线程轮询/等待消息，然后回调用户函数。

主动读取（轮询）：

```cpp
auto sub = dds.create_subscriber("demo://dds/read_modes", true, nullptr);
size_t n = sub->read(buf, buf_size, true);
```

- `Subscriber::read(data,size,latest=true)`
  - `latest=true`：读取“最新”消息（更适合状态类 Topic）。
  - `latest=false`：读取“下一条未读”消息（更适合队列类 Topic）。

### 1.6 绑定硬件句柄（DDS::Handle）与共享内存模式的区别

除共享内存 Topic 外，DDS 还支持把 `DDS::Handle` 绑定到 Publisher/Subscriber 上，使得 `publish/read` 直接走 handle 的 `send/receive`，从而把 DDS 接口复用到“硬件/链路收发”场景。

- 共享内存模式：
  - `create_publisher(topic_name, enable_checksum)` / `create_subscriber(topic_name, enable_checksum, ...)`
  - 数据在共享内存 RingBuffer 内流转，适合进程内/同机进程间分发
- 句柄模式：
  - `create_publisher(topic_name, handle)` / `create_subscriber(topic_name, handle, ...)`
  - 数据通过 `handle->send/receive` 走具体后端，适合把硬件/链路抽象为统一的 DDS 接口

`DDS::Handle` 接口定义见：[DDSHandle.h](../src/MB_DDF/DDS/DDSHandle.h)

### 1.7 Demo：DDS 初始化与发布订阅

- Demo 文件：[mb_ddf_demo_01_dds_init_pubsub.h](../src/Demo/mb_ddf_demo_01_dds_init_pubsub.h)
- 要点：显式初始化 DDS、同 Topic 创建发布/订阅、通过回调打印收到的数据

### 1.8 Demo：订阅回调读取 + 主动查询读取（latest/next）

- Demo 文件：[mb_ddf_demo_02_sub_read_modes.h](../src/Demo/mb_ddf_demo_02_sub_read_modes.h)
- 要点：同 Topic 同时展示“回调订阅者”和“轮询订阅者”，并对比 `latest/next` 读取语义
- 参数含义：
  - `create_subscriber(topic, enable_checksum, callback)`
    - `enable_checksum`：是否启用消息校验（与 RingBuffer 校验逻辑相关）
    - `callback=nullptr`：不启用后台线程，适合在主循环中主动调用 `read()`

---

## 2. PhysicalLayer：硬件工厂与 DDS::Handle

### 2.1 关键类型

- `MB_DDF::DDS::Handle`：抽象的“收发接口”，用于让 DDS 发布/订阅直接绑定具体硬件/链路。
  - `send(const uint8_t*, uint32_t)`
  - `receive(uint8_t*, uint32_t)` / `receive(..., timeout_us)`
  - `getMTU()`
- `MB_DDF::PhysicalLayer::Factory::HardwareFactory`：按名称创建一个 `std::shared_ptr<DDS::Handle>`。

相关头文件：

- [DDSHandle.h](../src/MB_DDF/DDS/DDSHandle.h)
- [HardwareFactory.h](../src/MB_DDF/PhysicalLayer/Factory/HardwareFactory.h)

### 2.2 工厂名称与含义（当前实现）

来自 [HardwareFactory.cpp](../src/MB_DDF/PhysicalLayer/Factory/HardwareFactory.cpp)：

- `"can"`：XDMA + CAN 设备适配（`device_offset=0x50000`，`event_number=5`）
- `"helm"`：XDMA + 舵机 Helm 设备适配（`device_offset=0x60000`）
- `"imu"`：XDMA + RS422 设备适配（IMU 通道，`device_offset=0x10000`，`event_number=1`）
- `"dyt"`：XDMA + RS422 设备适配（另一通道，`device_offset=0x20000`，`event_number=2`）
- `"ddr"`：XDMA + DDR 设备适配（DMA 通道 `h2c=0/c2h=0`，`event_number=6`）
- `"udp"`：UDP 链路适配（纯软件，便于本机验证）

注意：

- XDMA 类设备依赖 `/dev/xdma0*` 节点；在无驱动/无硬件环境下会输出错误日志并可能无法实际收发。
- UDP 链路的 `LinkConfig.name` 支持 `"<local_ip>:<local_port>|<remote_ip>:<remote_port>"`，只有指定远端后 `send()` 才会成功。

### 2.3 create(name, param) 第二参数含义与配置方法

`HardwareFactory::create(const std::string& name, void* param)` 的第二个参数 `param` 是一个“按 name 解释的可选配置指针”。当前实现中它只在 `create()` 调用期间被读取/拷贝到 `ioctl/open`，因此可直接传入栈上对象指针。

按 name 的含义如下（以当前实现为准）：

- `can`
  - `param == nullptr`：使用默认 `uint32_t baud = 500000`（500K 波特率）。
  - `param != nullptr`：指向 `MB_DDF::PhysicalLayer::Device::CanDevice::BitTiming`，由调用者给出位时序寄存器参数。
  - 定义见：[CanDevice.h](../src/MB_DDF/PhysicalLayer/Device/CanDevice.h)
- `helm`
  - `param == nullptr`：使用默认 `HelmDevice::Config{ pwm_freq=8000, out_enable=0xF, ad_filter=1 }`。
  - `param != nullptr`：指向 `MB_DDF::PhysicalLayer::Device::HelmDevice::Config`。
  - 定义见：[HelmDevice.h](../src/MB_DDF/PhysicalLayer/Device/HelmDevice.h)
- `imu` / `dyt`
  - `param == nullptr`：使用默认 `Rs422Device::Config`（寄存器值见工厂内默认配置）。
  - `param != nullptr`：指向 `MB_DDF::PhysicalLayer::Device::Rs422Device::Config`，用于配置串口寄存器/头字节/回环/中断/事件等。
  - 定义见：[Rs422Device.h](../src/MB_DDF/PhysicalLayer/Device/Rs422Device.h)
- `udp`
  - `param == nullptr`：使用默认字符串 `"12345"`（仅绑定本地端口，未指定远端，`send()` 将失败）。
  - `param != nullptr`：指向以 `'\0'` 结尾的配置字符串（会被当作 `const char*` 转成 `std::string`），写入 `LinkConfig.name`。
  - 字符串格式见：[UdpLink.h](../src/MB_DDF/PhysicalLayer/DataPlane/UdpLink.h)
- `ddr`
  - 当前实现不读取 `param`（后续可能会扩展配置项）。

示例（仅演示 param 形态）：

```cpp
// CAN：用 BitTiming 配位时序
MB_DDF::PhysicalLayer::Device::CanDevice::BitTiming bt{.prescaler=1, .ts1=7, .ts2=2, .sjw=0};
auto h_can = MB_DDF::PhysicalLayer::Factory::HardwareFactory::create("can", &bt);

// HELM：配置 PWM 频率与输出使能
MB_DDF::PhysicalLayer::Device::HelmDevice::Config helm_cfg{.pwm_freq=8000, .out_enable=0xF, .ad_filter=1};
auto h_helm = MB_DDF::PhysicalLayer::Factory::HardwareFactory::create("helm", &helm_cfg);

// RS422（imu/dyt）：配置寄存器值与帧头
MB_DDF::PhysicalLayer::Device::Rs422Device::Config rs_cfg{};
rs_cfg.ucr = 0x30;
rs_cfg.mcr = 0x20;
rs_cfg.brsr = 0x0A;
rs_cfg.icr = 0x01;
rs_cfg.tx_head_lo = 0xAA;
rs_cfg.tx_head_hi = 0x1A;
rs_cfg.rx_head_lo = 0xAA;
rs_cfg.rx_head_hi = 0x1A;
rs_cfg.lpb = 0x00;
rs_cfg.intr = 0xAE;
rs_cfg.evt = 1250;
auto h_imu = MB_DDF::PhysicalLayer::Factory::HardwareFactory::create("imu", &rs_cfg);

// UDP：配置字符串（推荐显式指定远端，否则 send() 会返回 false）
static char udp_cfg[] = "127.0.0.1:20001|127.0.0.1:20001";
auto h_udp = MB_DDF::PhysicalLayer::Factory::HardwareFactory::create("udp", udp_cfg);
```

补充说明：

- 当前硬件工厂只是“已实现条目”的集合，并不是全部实现；后续会继续新增更多设备/通道条目与更丰富的可选参数。

### 2.4 Demo：全部硬件工厂示例

- Demo 文件：[mb_ddf_demo_03_hardware_factory.h](../src/Demo/mb_ddf_demo_03_hardware_factory.h)
- 要点：对所有工厂名称调用 `HardwareFactory::create`，并对 UDP 做一次 `send/receive` 自环验证

---

## 3. DDS 便捷封装：PubAndSub

`MB_DDF::DDS::PubAndSub` 是一个轻量封装，内部同时创建：

- `publisher = DDSCore::create_publisher("handle://p", handle)`
- `subscriber = DDSCore::create_subscriber("handle://s", handle, callback)`

用于“快速把某个 `DDS::Handle` 暴露成 DDS 的发布/订阅接口”。

定义位置：

- [DDSCore.h:PubAndSub](../src/MB_DDF/DDS/DDSCore.h#L189-L199)

### 3.1 Demo：通过 PubAndSub 快速创建硬件工厂的 DDS 接口

- Demo 文件：[mb_ddf_demo_04_pubandsub_factory_dds.h](../src/Demo/mb_ddf_demo_04_pubandsub_factory_dds.h)
- 要点：用 `HardwareFactory` 创建 `DDS::Handle`，再用 `PubAndSub` 分别展示“回调接收”和“轮询 read()”

---

## 4. Timer：SystemTimer（高精度周期定时器）

头文件：[SystemTimer.h](../src/MB_DDF/Timer/SystemTimer.h)

核心入口：

```cpp
auto timer = MB_DDF::Timer::SystemTimer::start("5ms", callback, opt);
timer->stop();
```

参数说明：

- `period_str`：周期字符串，支持 `s/ms/us/ns`（例如 `"1s"`, `"10ms"`, `"500us"`）。
- `callback`：`std::function<void(void*)>`，在定时信号触发时被调用。
- `SystemTimerOptions`：
  - `sched_policy`：定时线程调度策略（默认 `SCHED_FIFO`）
  - `priority`：线程优先级（默认 50）
  - `cpu`：绑核（默认 -1 不绑核）
  - `signal_no`：使用的实时信号号（默认 `SIGRTMIN`）
  - `user_data`：回调透传指针

### 4.1 Demo：SystemTimer 用法

- Demo 文件：[mb_ddf_demo_05_system_timer.h](../src/Demo/mb_ddf_demo_05_system_timer.h)

---

## 5. Timer：ChronoHelper（时间统计函数）

头文件：[ChronoHelper.h](../src/MB_DDF/Timer/ChronoHelper.h)

常用接口：

- `ChronoHelper::timing(func, args...)`：测一次耗时并打印（us）
- `ChronoHelper::timingAverage(times, func, args...)`：测平均耗时并打印（自动选择单位）
- `ChronoHelper::clockStart(n)` / `clockEnd(n)`：分段计时（us）
- `ChronoHelper::record(counter_id, expected_interval_us)`：周期偏差统计（共享 1s 周期输出）

### 5.1 Demo：ChronoHelper 用法

- Demo 文件：[mb_ddf_demo_06_chrono_helper.h](../src/Demo/mb_ddf_demo_06_chrono_helper.h)

---

## 6. Debug：日志模块（MB_DDF::Debug::Logger）

头文件：[Logger.h](../src/MB_DDF/Debug/Logger.h)

### 6.1 基本能力

- 单例：`Logger::instance()`
- 级别：`TRACE/DEBUG/INFO/WARN/ERROR/FATAL/OFF`
- 输出：控制台、文件、回调（可同时启用）
- 流式写法：`LOG_INFO << "..."`（自动携带函数名与行号）

### 6.2 配置接口与对应宏（重点）

以下配置既可直接调用方法，也可使用等价的宏（宏只是对 `Logger::instance()` 的薄封装）：

- 设置日志级别
  - 方法：`Logger::set_level(LogLevel level)`
  - 宏：`LOG_SET_LEVEL_TRACE()` / `LOG_SET_LEVEL_DEBUG()` / `LOG_SET_LEVEL_INFO()` / `LOG_SET_LEVEL_WARN()` / `LOG_SET_LEVEL_ERROR()` / `LOG_SET_LEVEL_FATAL()` / `LOG_SET_LEVEL_OFF()`
- 时间戳开关
  - 方法：`Logger::set_timestamp_enabled(bool enabled)`
  - 宏：`LOG_ENABLE_TIMESTAMP()` / `LOG_DISABLE_TIMESTAMP()`
- 彩色输出开关
  - 方法：`Logger::set_color_enabled(bool enabled)`
  - 宏：`LOG_ENABLE_COLOR()` / `LOG_DISABLE_COLOR()`
- 函数名与行号开关
  - 方法：`Logger::set_function_line_enabled(bool enabled)`
  - 宏：`LOG_ENABLE_FUNCTION_LINE()` / `LOG_DISABLE_FUNCTION_LINE()`

### 6.3 常用配置示例

```cpp
LOG_SET_LEVEL_DEBUG();
LOG_DISABLE_COLOR();
LOG_ENABLE_TIMESTAMP();
LOG_ENABLE_FUNCTION_LINE();

MB_DDF::Debug::Logger::instance().set_file_output("demo.log");
MB_DDF::Debug::Logger::instance().add_callback(
  [](MB_DDF::Debug::LogLevel, const std::string& line) { /* ... */ });
```

### 6.4 Demo：Logger 各种用法

- Demo 文件：[mb_ddf_demo_07_logger.h](../src/Demo/mb_ddf_demo_07_logger.h)

---

## 7. 运行入口：main.cpp

入口文件：[main.cpp](../src/Demo/main.cpp)

当前会按顺序调用所有 Demo：

1. DDS 初始化与发布订阅
2. 回调读取与轮询读取
3. 硬件工厂示例
4. PubAndSub + 工厂快速接入 DDS
5. SystemTimer
6. ChronoHelper
7. Logger
