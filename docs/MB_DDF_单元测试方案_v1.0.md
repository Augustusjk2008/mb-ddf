# MB_DDF 单元测试方案 v1.0

> **适用环境**：x86_64 Windows 主机 → AArch64 Linux 目标硬件
> **文档日期**：2026-02-11
> **版本**：v1.0

---

## 1. 环境约束分析

### 1.1 开发环境特点

| 层级 | 平台 | 限制 |
|------|------|------|
| **Host** | x86_64 Windows | 无法直接运行 Linux 系统调用（futex、POSIX SHM、timerfd、SIGRTMIN） |
| **交叉编译** | Windows → AArch64 | 主要构建流程目标为 ARM64，需额外配置 Host 测试构建 |
| **Target** | AArch64 Linux | 真实硬件环境，需远程部署执行 |

### 1.2 测试分层策略

基于环境约束，采用**三层测试架构**：

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Layer 3: 目标板集成测试                        │
│   (Target Integration Tests - 在AArch64硬件上执行)                    │
│   • SystemTimer 精度验证                                              │
│   • 真实共享内存IPC                                                   │
│   • XDMA硬件通路                                                      │
│   • CAN/RS422/Helm设备通信                                            │
└─────────────────────────────────────────────────────────────────────┘
                              ▲
                              │ SSH部署/执行
┌─────────────────────────────────────────────────────────────────────┐
│                        Layer 2: 主机集成测试                          │
│   (Host Integration Tests - 在WSL2/Docker中执行)                      │
│   • 需要Linux环境但无需硬件                                           │
│   • RingBuffer futex同步机制                                          │
│   • POSIX共享内存生命周期                                             │
│   • 多进程Publisher/Subscriber                                        │
└─────────────────────────────────────────────────────────────────────┘
                              ▲
                              │ 本地执行
┌─────────────────────────────────────────────────────────────────────┐
│                        Layer 1: 主机单元测试                          │
│   (Host Unit Tests - 在Windows Native编译执行)                        │
│   • 纯算法/数据结构测试                                               │
│   • 零Linux依赖                                                       │
│   • 快速反馈（<1秒编译执行）                                          │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 2. 技术栈选择

| 用途 | 选择 | 理由 |
|------|------|------|
| **测试框架** | Google Test (v1.14+) | CMake集成好，Windows/Linux双平台支持 |
| **Mock框架** | Google Mock | 与gtest配套，支持虚接口Mock |
| **覆盖率** | OpenCppCoverage (Win) / gcov (Linux) | Windows原生支持，无需WSL |
| **CI/CD** | 可选 | 可配置GitHub Actions (Windows+WSL+SSH目标板) |

### 2.1 不推荐的技术

- **Catch2/ doctest**：Windows上表现好，但与交叉编译环境集成不如gtest成熟
- **Windows Subsystem for Android**：过于复杂，不适合嵌入式测试
- **纯QEMU模拟**：性能差，与真实硬件行为存在差异

---

## 3. 三层测试详细设计

### 3.1 Layer 1: Host Unit Tests (Windows Native)

**执行环境**：Windows x86_64 Native编译执行

**可测试组件**（零Linux依赖）：
- `Message` - CRC32算法、序列化、边界检查
- `MessageHeader` - 时间戳、校验和计算
- `ChronoHelper` - 时间统计、平均值、方差计算
- `RingBuffer` 核心逻辑（使用堆内存模拟，跳过futex）
- `Tools/md5` - MD5计算
- `Logger` 格式化逻辑（重定向到stringstream）

**禁止测试的内容**（含Linux系统调用）：
- 涉及 `sem_t` 的操作
- 涉及 `futex` 的wait/notify
- 涉及 `timerfd` / `SIGRTMIN` 的SystemTimer
- 涉及 `/dev/shm/` 的POSIX共享内存

**Mock策略**：
```cpp
// 使用Fake替代真实系统调用
class FakeSemaphore {  // 替代sem_t
    std::atomic<int> count_{1};
public:
    void wait() { while(count_.fetch_sub(1) <= 0) count_.fetch_add(1); }
    void post() { count_.fetch_add(1); }
};

// RingBuffer测试使用堆内存+FakeSemaphore
class RingBufferHostTest : public ::testing::Test {
    FakeSemaphore sem_;  // 替代真实的sem_t
    std::vector<uint8_t> heap_buffer_;  // 替代POSIX SHM
};
```

### 3.2 Layer 2: Host Integration Tests (WSL2)

**执行环境**：WSL2 Ubuntu (与Windows文件系统互通)

**可测试组件**：
- `RingBuffer` 完整功能（含futex等待/唤醒）
- `SharedMemoryManager` POSIX共享内存创建/销毁
- `TopicRegistry` 多进程Topic发现
- `DDSCore` 初始化/关闭流程
- `SystemTimer` 定时器精度（非实时性验证）
- `Publisher`/`Subscriber` 回调机制

**环境要求**：
```powershell
# WSL2安装gtest开发库
wsl -d Ubuntu sudo apt-get install libgtest-dev cmake build-essential

# 共享Windows源码目录，在WSL内构建
wsl -d Ubuntu -e bash -c "cd /mnt/h/RTLinux/Demos/MB_DDF && mkdir -p build-wsl && cd build-wsl && cmake .. -DENABLE_TESTS=ON && make -j"
```

**测试隔离**：
- 每个测试用例使用唯一的SHM名称（如 `/MB_DDF_TEST_<timestamp>_<pid>`）
- 使用RAII Guard确保SHM清理（即使测试崩溃）
- 并行测试时避免端口/名称冲突

### 3.3 Layer 3: Target Integration Tests (AArch64)

**执行环境**：真实目标硬件（通过SSH远程执行）

**可测试组件**：
- 完整DDS系统（真实共享内存）
- `SystemTimer` 实时精度（SCHED_FIFO调度）
- `HardwareFactory` 各设备类型（CAN/Helm/IMU/RS422/DDR）
- `XdmaTransport` 寄存器读写、DMA传输
- 端到端延迟测试

**部署执行流程**：
```powershell
# 1. 构建目标测试二进制（交叉编译）
.\build.bat debug -DENABLE_TARGET_TESTS=ON

# 2. 部署到目标板并执行
$TestResults = ssh root@192.168.137.100 "/tmp/MB_DDFTests --gtest_filter=Target.*"

# 3. 收集结果回传
scp root@192.168.137.100:/tmp/test_report.xml ./
```

**测试分类标记**：
```cpp
// 使用gtest标签区分测试类型
TEST(HostAlgo, MessageCRC32) { }           // Layer 1
TEST(HostIntegration, RingBufferFutex) { } // Layer 2
TEST(Target, SystemTimerRealtime) { }      // Layer 3
TEST(Target, XdmaCanLoopback) { }          // Layer 3
```

---

## 4. 项目结构

```
MB_DDF/
├── src/                          # 源码目录（保持现状）
│   └── MB_DDF/
├── tests/                        # 新增：测试根目录
│   ├── CMakeLists.txt            # 测试构建配置（支持三层）
│   ├── common/                   # 测试公共代码
│   │   ├── FakeTypes.h           # Windows可编译的类型替代
│   │   ├── SHMGuard.h            # 共享内存清理RAII
│   │   └── TestUtils.h           # 通用测试辅助
│   ├── layer1_host_unit/         # Windows Native单元测试
│   │   ├── test_message.cpp
│   │   ├── test_ringbuffer_core.cpp  # 无futex版本
│   │   ├── test_chrono_helper.cpp
│   │   └── test_md5.cpp
│   ├── layer2_host_integration/  # WSL集成测试
│   │   ├── test_dds_core.cpp
│   │   ├── test_ringbuffer_full.cpp  # 含futex
│   │   ├── test_system_timer.cpp
│   │   └── test_pub_sub.cpp
│   ├── layer3_target_integration/# AArch64目标测试
│   │   ├── test_hardware_factory.cpp
│   │   ├── test_xdma_transport.cpp
│   │   ├── test_realtime_perf.cpp
│   │   └── test_device_loopback.cpp
│   └── mocks/                    # Mock实现
│       ├── MockDeviceTransport.h
│       ├── MockDDSHandle.h
│       └── MockPhysicalLayer.h
├── docs/                         # 文档目录
│   └── MB_DDF_单元测试方案_v1.0.md  # 本文档
└── CMakeLists.txt                # 主构建配置（添加测试选项）
```

---

## 5. 构建配置

### 5.1 主CMakeLists.txt 添加测试选项

```cmake
# 在根CMakeLists.txt末尾添加
option(ENABLE_TESTS "Enable test targets" OFF)
option(ENABLE_TARGET_TESTS "Build tests for AArch64 target" OFF)

if(ENABLE_TESTS)
    add_subdirectory(tests)
endif()
```

### 5.2 tests/CMakeLists.txt 三层配置

```cmake
# ========== Layer 1: Windows Native单元测试 ==========
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows" AND NOT CMAKE_CROSSCOMPILING)
    message(STATUS "Configuring Layer 1: Host Unit Tests (Windows Native)")

    # 使用FetchContent获取gtest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    file(GLOB LAYER1_SOURCES "layer1_host_unit/*.cpp")
    add_executable(MB_DDF_Layer1_Tests ${LAYER1_SOURCES})

    target_compile_definitions(MB_DDF_Layer1_Tests PRIVATE
        MB_DDF_TEST_LAYER1=1      # 标识Layer 1测试
        MB_DDF_SKIP_LINUX_SYSCALL=1  # 跳过Linux系统调用
    )

    target_link_libraries(MB_DDF_Layer1_Tests PRIVATE
        gtest_main
        gmock
    )
endif()

# ========== Layer 2: WSL集成测试 ==========
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux" AND NOT CMAKE_CROSSCOMPILING)
    message(STATUS "Configuring Layer 2: Host Integration Tests (WSL)")

    find_package(GTest REQUIRED)
    file(GLOB LAYER2_SOURCES "layer2_host_integration/*.cpp")
    add_executable(MB_DDF_Layer2_Tests ${LAYER2_SOURCES})

    target_compile_definitions(MB_DDF_Layer2_Tests PRIVATE
        MB_DDF_TEST_LAYER2=1
    )

    target_link_libraries(MB_DDF_Layer2_Tests PRIVATE
        GTest::gtest_main
        GTest::gmock
        pthread
        rt
    )
endif()

# ========== Layer 3: AArch64目标测试 ==========
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    message(STATUS "Configuring Layer 3: Target Integration Tests (AArch64)")

    # 使用sysroot中的gtest或静态链接
    file(GLOB LAYER3_SOURCES "layer3_target_integration/*.cpp")
    add_executable(MB_DDF_Layer3_Tests ${LAYER3_SOURCES})

    target_compile_definitions(MB_DDF_Layer3_Tests PRIVATE
        MB_DDF_TEST_LAYER3=1
    )

    # 链接主项目源码
    target_sources(MB_DDF_Layer3_Tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src/MB_DDF/**/*.cpp
    )

    target_link_libraries(MB_DDF_Layer3_Tests PRIVATE
        gtest_main
        gmock
        pthread
        rt
    )
endif()
```

---

## 6. 测试执行流程

### 6.1 日常开发流程（快速反馈）

```powershell
# Windows本机快速验证（Layer 1）
mkdir build-win && cd build-win
cmake .. -DENABLE_TESTS=ON
cmake --build . --target MB_DDF_Layer1_Tests
ctest -R Layer1 --output-on-failure
```

### 6.2 功能验证流程（WSL）

```powershell
# 在WSL中验证Linux特性（Layer 2）
wsl -d Ubuntu -e bash -c "
    cd /mnt/h/RTLinux/Demos/MB_DDF
    mkdir -p build-wsl && cd build-wsl
    cmake .. -DENABLE_TESTS=ON
    make MB_DDF_Layer2_Tests -j4
    ./MB_DDF_Layer2_Tests --gtest_filter='*'
"
```

### 6.3 硬件验证流程（目标板）

```powershell
# 构建并部署到目标板（Layer 3）
.\build.bat debug -DENABLE_TESTS=ON -DENABLE_TARGET_TESTS=ON

# 自动部署脚本
$RemoteHost = "192.168.137.100"
$RemoteDir = "/home/root/tmp"

# 上传测试二进制
scp build/aarch64/Debug/MB_DDF_Layer3_Tests root@${RemoteHost}:${RemoteHost}/

# 远程执行并取回结果
ssh root@${RemoteHost} "cd ${RemoteDir} && ./MB_DDF_Layer3_Tests --gtest_output=xml:test_report.xml"
scp root@${RemoteHost}:${RemoteDir}/test_report.xml ./
```

---

## 7. 关键问题与解决方案

### 7.1 Windows vs Linux头文件差异

**问题**：`semaphore.h`、`linux/futex.h` 等头文件Windows不存在

**方案**：
```cpp
// tests/common/PlatformCompat.h
#ifdef MB_DDF_SKIP_LINUX_SYSCALL
    // Windows兼容层
    #include "FakeSemaphore.h"
    #define FAKE_SHM 1
#else
    // 真实Linux头文件
    #include <semaphore.h>
    #include <linux/futex.h>
#endif
```

### 7.2 共享内存路径差异

**问题**：Windows没有 `/dev/shm/`

**方案**：
```cpp
// tests/common/SHMHelper.h
inline std::string GetTestSHMPath(const std::string& name) {
#ifdef _WIN32
    // Windows使用命名文件映射
    return "Local\\MB_DDF_TEST_" + name;
#else
    // Linux使用POSIX SHM
    return "/MB_DDF_TEST_" + name;
#endif
}
```

### 7.3 信号与定时器

**问题**：Windows不支持 `SIGRTMIN` 和 `timerfd`

**方案**：
- Layer 1测试使用 `std::chrono` 模拟定时
- Layer 2/3使用真实Linux定时器

---

## 8. 实施路线图

| 阶段 | 时间 | 内容 |
|------|------|------|
| **Phase 1** | 1-2天 | 搭建测试框架，实现Layer 1基础测试（Message、CRC32、ChronoHelper） |
| **Phase 2** | 2-3天 | 完善Layer 1，添加RingBuffer核心逻辑测试（无futex） |
| **Phase 3** | 3-5天 | 配置WSL环境，实现Layer 2集成测试（完整DDS流程） |
| **Phase 4** | 5-7天 | 配置目标板自动部署，实现Layer 3硬件测试 |
| **Phase 5** | 持续 | 添加覆盖率收集、CI/CD集成 |

---

## 9. 附录

### 9.1 测试命名规范

| 测试类型 | 命名格式 | 示例 |
|----------|----------|------|
| Layer 1 | `HostAlgo_<Component>_<Scenario>` | `HostAlgo_Message_CRC32Validation` |
| Layer 2 | `HostIntegration_<Component>_<Scenario>` | `HostIntegration_RingBuffer_MultiSubscriber` |
| Layer 3 | `Target_<Component>_<Scenario>` | `Target_SystemTimer_1msPeriod` |

### 9.2 快速检查清单

- [ ] Windows本机编译通过（`cmake -DENABLE_TESTS=ON`）
- [ ] Layer 1测试在Windows执行通过
- [ ] WSL环境配置完成（`wsl --install`）
- [ ] Layer 2测试在WSL执行通过
- [ ] 目标板SSH免密登录配置完成
- [ ] Layer 3测试在目标板执行通过
- [ ] 覆盖率报告生成脚本可用

---

**文档维护**：后续迭代根据实际情况调整分层策略和测试范围。
