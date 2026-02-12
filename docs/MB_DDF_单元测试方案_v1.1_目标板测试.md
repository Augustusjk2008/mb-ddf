# MB_DDF 单元测试方案 v1.1 (目标板测试)

> **适用环境**：x86_64 Windows 主机 → AArch64 Linux 目标硬件
> **策略**：全部测试在目标板执行，Windows仅作为构建/部署主机
> **文档日期**：2026-02-11

---

## 1. 方案概述

放弃分层测试架构，**所有测试统一在AArch64目标板执行**。

```
Windows Host (x86_64)
    │
    │ 交叉编译
    ▼
AArch64 Target (Linux)
    │
    ├─► 单元测试 (gtest)
    ├─► 组件测试 (Mock硬件)
    └─► 集成测试 (真实硬件)
```

### 1.1 方案优势

| 优势 | 说明 |
|------|------|
| **环境一致** | 测试即生产，无平台差异 |
| **架构简单** | 单构建系统，单执行环境 |
| **无Mock陷阱** | 不会因为Mock行为与真实硬件差异导致漏测 |
| **真机验证** | 实时性、内存对齐、缓存行为完全真实 |

### 1.2 方案劣势与应对

| 劣势 | 应对方案 |
|------|----------|
| 反馈速度慢 | 增量编译 + 热部署脚本（3-5秒完成部署） |
| 目标板不可用影响开发 | 保持SSH连接，开发机仅编译不上板 |
| 资源限制 | gtest静态链接，测试二进制单独裁剪 |
| 调试困难 | 配合gdbserver远程调试 |

---

## 2. 技术栈

| 组件 | 选择 | 说明 |
|------|------|------|
| 测试框架 | Google Test (静态链接) | 不依赖目标板动态库 |
| Mock框架 | Google Mock | 用于隔离硬件依赖 |
| 部署方式 | SCP + SSH | 每次构建后自动上传执行 |
| 调试 | gdbserver + VS Code | 保持现有debug.bat能力 |

---

## 3. 项目结构

```
tests/
├── CMakeLists.txt              # 测试构建配置
├── main.cpp                    # 测试入口
├── mocks/                      # Mock实现
│   ├── MockXdmaTransport.h     # XDMA传输层Mock
│   ├── MockCanDevice.h         # CAN设备Mock
│   ├── MockRs422Device.h       # RS422设备Mock
│   └── MockHelmDevice.h        # Helm设备Mock
├── unit/                       # 单元测试（纯内存，无硬件）
│   ├── test_message.cpp        # CRC、序列化
│   ├── test_ringbuffer.cpp     # 环形缓冲区
│   ├── test_topic_registry.cpp # Topic管理
│   └── test_chrono_helper.cpp  # 时间工具
├── component/                  # 组件测试（Mock硬件）
│   ├── test_publisher.cpp      # Publisher + Mock Handle
│   ├── test_subscriber.cpp     # Subscriber回调/轮询
│   ├── test_dds_core.cpp       # DDSCore + Mock SHM
│   └── test_hardware_factory_mock.cpp  # Factory + Mock Transport
└── integration/                # 集成测试（真实硬件）
    ├── test_xdma_can.cpp       # XDMA + CAN设备
    ├── test_xdma_helm.cpp      # XDMA + Helm设备
    ├── test_xdma_imu.cpp       # XDMA + RS422 IMU
    ├── test_system_timer.cpp   # 定时器精度
    └── test_dds_ipc.cpp        # 多进程IPC
```

---

## 4. 测试分类

### 4.1 单元测试 (Unit Tests)

**特点**：纯内存运算，无硬件依赖，执行快（毫秒级）

```cpp
// tests/unit/test_ringbuffer.cpp
TEST(RingBufferTest, PublishAndRead) {
    // 使用堆内存，不涉及真实设备
    std::vector<uint8_t> buffer(64*1024);
    sem_t sem;
    sem_init(&sem, 0, 1);

    RingBuffer rb(buffer.data(), buffer.size(), &sem);

    const char* msg = "hello";
    EXPECT_TRUE(rb.publish_message(msg, 6));

    auto* sub = rb.register_subscriber(1, "test");
    Message* out = nullptr;
    EXPECT_TRUE(rb.read_next(sub, out));
    EXPECT_STREQ((char*)out->get_data(), msg);
}
```

### 4.2 组件测试 (Component Tests)

**特点**：使用Mock隔离硬件，测试模块间协作

```cpp
// tests/component/test_publisher.cpp
TEST_F(PublisherTest, PublishViaMockHandle) {
    auto mockHandle = std::make_shared<MockDDSHandle>();

    // 期望send被调用
    EXPECT_CALL(*mockHandle, send(_, 5))
        .WillOnce(Return(true));

    Publisher pub(mockHandle);
    EXPECT_TRUE(pub.publish("hello", 5));
}
```

### 4.3 集成测试 (Integration Tests)

**特点**：真实硬件，验证端到端功能

```cpp
// tests/integration/test_xdma_can.cpp
TEST(XdmaCanTest, Loopback) {
    // 真实CAN设备初始化
    auto handle = HardwareFactory::create("can", nullptr);
    ASSERT_NE(handle, nullptr);

    // 发送帧
    CanFrame tx{0x123, {0x01, 0x02, 0x03, 0x04}};
    EXPECT_TRUE(handle->send((uint8_t*)&tx, sizeof(tx)));

    // 接收回环帧
    CanFrame rx;
    int32_t len = handle->receive((uint8_t*)&rx, sizeof(rx), 100000); // 100ms timeout
    EXPECT_EQ(len, sizeof(rx));
    EXPECT_EQ(rx.id, 0x123);
}
```

---

## 5. 构建与部署

### 5.1 修改 build.bat

```powershell
# build.ps1 添加测试构建选项
param(
    [Parameter(Position=0)]
    [ValidateSet("clean", "debug", "release", "test")]
    [string]$Command = "debug",
    ...
)

# test命令：交叉编译测试二进制
if ($Command -eq "test") {
    $CMAKE_BUILD_TYPE = "Debug"
    $EXTRA_CMAKE_ARGS += "-DENABLE_TESTS=ON"
    $TARGET_NAME = "MB_DDF_Tests"
}
```

### 5.2 测试部署脚本 test-deploy.ps1

```powershell
# tests/test-deploy.ps1
param(
    [string]$RemoteHost = "192.168.1.29",
    [string]$RemoteDir = "/home/root/tests",
    [string]$TestFilter = "*"
)

$ErrorActionPreference = "Stop"

# 1. 构建测试
Write-Host "Building tests..." -ForegroundColor Green
& ..\build.bat test

# 2. 确保目录存在
ssh root@$RemoteHost "mkdir -p $RemoteDir"

# 3. 上传测试二进制和gtest库
Write-Host "Deploying to target..." -ForegroundColor Green
scp build/aarch64/Debug/MB_DDF_Tests root@${RemoteHost}:${RemoteDir}/

# 4. 执行测试并实时输出
Write-Host "Running tests on target..." -ForegroundColor Green
ssh root@$RemoteHost "cd $RemoteDir && ./MB_DDF_Tests --gtest_filter='$TestFilter' --gtest_color=yes"

# 5. 可选：取回覆盖率数据（如启用）
# scp root@${RemoteHost}:${RemoteDir}/*.gcna ./
```

### 5.3 CMakeLists.txt 测试配置

```cmake
# tests/CMakeLists.txt
option(ENABLE_TESTS "Enable test build" OFF)

if(ENABLE_TESTS)
    enable_testing()

    # gtest静态编译（避免目标板依赖）
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    FetchContent_MakeAvailable(googletest)

    # 测试可执行文件
    file(GLOB_RECURSE TEST_SOURCES
        "unit/*.cpp"
        "component/*.cpp"
        "integration/*.cpp"
    )

    add_executable(MB_DDF_Tests ${TEST_SOURCES})

    target_include_directories(MB_DDF_Tests PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}
    )

    target_link_libraries(MB_DDF_Tests PRIVATE
        gtest_main
        gmock
        pthread
        rt
    )

    # 静态链接C++库，减少目标板依赖
    target_link_options(MB_DDF_Tests PRIVATE -static-libstdc++ -static-libgcc)
endif()
```

---

## 6. 执行流程

### 6.1 全量测试

```powershell
# PowerShell
.\tests\test-deploy.ps1

# 输出示例：
# [==========] Running 45 tests from 12 test suites
# [----------] Global test environment set-up
# [ RUN      ] RingBufferTest.PublishAndRead
# [       OK ] RingBufferTest.PublishAndRead (2 ms)
# ...
# [==========] 45 tests ran (245 ms total)
# [  PASSED  ] 45 tests
```

### 6.2 指定测试类别

```powershell
# 仅运行单元测试
.\tests\test-deploy.ps1 -TestFilter "Unit*"

# 仅运行组件测试
.\tests\test-deploy.ps1 -TestFilter "Component*"

# 仅运行集成测试
.\tests\test-deploy.ps1 -TestFilter "Integration*"

# 排除慢速测试
.\tests\test-deploy.ps1 -TestFilter "*-Integration*Slow*"
```

### 6.3 开发迭代流程

```powershell
# 修改代码后快速验证（假设目标板已连接）
.\build.bat test && .\tests\test-deploy.ps1 -TestFilter "Unit*"

# 验证通过后完整测试
.\tests\test-deploy.ps1
```

---

## 7. Mock设计要点

### 7.1 Mock Transport（用于组件测试）

```cpp
// tests/mocks/MockXdmaTransport.h
#pragma once
#include <gmock/gmock.h>
#include "MB_DDF/PhysicalLayer/ControlPlane/IDeviceTransport.h"

class MockXdmaTransport : public IDeviceTransport {
public:
    MOCK_METHOD(bool, open, (const TransportConfig&), (override));
    MOCK_METHOD(void, close, (), (override));
    MOCK_METHOD(bool, readReg32, (uint64_t, uint32_t&), (const, override));
    MOCK_METHOD(bool, writeReg32, (uint64_t, uint32_t), (override));

    // 预置寄存器值
    void SetRegister(uint64_t offset, uint32_t value) {
        registers_[offset] = value;
    }

private:
    std::map<uint64_t, uint32_t> registers_;
};
```

### 7.2 何时使用Mock

| 场景 | 是否Mock | 原因 |
|------|----------|------|
| 测试Publisher逻辑 | Mock Handle | 验证数据流，不依赖真实CAN |
| 测试RingBuffer | 不Mock | 纯内存组件，直接测试 |
| 测试CAN帧解析 | Mock Transport | 模拟各种CAN错误状态 |
| 验证CAN波特率 | 真实硬件 | 时序必须真实验证 |

---

## 8. 持续集成（可选）

### 8.1 自动测试服务器

如果目标板长期在线，可配置自动测试：

```powershell
# Jenkins/GitHub Actions调用
.\tests\test-deploy.ps1 -RemoteHost $env:TARGET_IP

# 失败时发送通知
if ($LASTEXITCODE -ne 0) {
    Send-Notification "Tests failed on $env:TARGET_IP"
}
```

### 8.2 测试报告

```xml
<!-- gtest生成JUnit格式 -->
ssh root@$RemoteHost "./MB_DDF_Tests --gtest_output=xml:report.xml"
scp root@$RemoteHost:report.xml ./
```

---

## 9. 快速开始

### 9.1 初始化测试环境

```powershell
# 1. 克隆gtest（首次）
# FetchContent自动处理

# 2. 确保目标板可达
ping 192.168.1.29
ssh root@192.168.1.29 "echo OK"

# 3. 创建测试目录
ssh root@192.168.1.29 "mkdir -p /home/sast/user_tests"
```

### 9.2 第一个测试

```cpp
// tests/unit/test_message.cpp
#include <gtest/gtest.h>
#include "MB_DDF/DDS/Message.h"

TEST(Message, CRC32Verify) {
    // 验证文档中的CRC测试向量
    EXPECT_EQ(MB_DDF::DDS::Message::verify_crc32_algorithms(), 0);
}
```

```powershell
# 编译并执行
.\build.bat test
.\tests\test-deploy.ps1 -TestFilter "Message*"
```

---

## 10. 方案对比

| 特性 | 三层测试 (v1.0) | 全目标板 (v1.1) |
|------|-----------------|-----------------|
| 复杂度 | 高（3套构建环境） | 低（1套构建环境） |
| 反馈速度 | 快（本机毫秒级） | 中等（3-5秒部署） |
| 环境保真 | 低（Mock/模拟） | 高（真实硬件） |
| 维护成本 | 高 | 低 |
| 离线开发 | 支持（Layer 1） | 不支持（需目标板） |
| 推荐场景 | 大型团队/复杂项目 | 小团队/目标板稳定 |

---

**结论**：如果目标板稳定在线（如长期放在实验室），**全目标板测试是更简洁可靠的选择**。
