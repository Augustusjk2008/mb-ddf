# AGENT.md

本文件为 Codex/代码代理在本仓库工作的中文说明。

## 项目概述

`MB_DDF` 是一个基于 C++20 的数据分发框架（DDS），用于嵌入式/实时 Linux（AArch64）。
主要能力包括：

- 共享内存 + 无锁环形缓冲区进程间通信
- 硬件抽象（CAN / RS422 / Helm / DMA / UDP）
- 定时器与日志工具

开发环境特点：

- 本项目在 `Windows` 下开发与维护
- 构建/调试脚本是 `PowerShell`
- 目标程序是交叉编译到 `AArch64 Linux`

## 重要约束（请优先遵守）

- 命令统一使用 `PowerShell` 风格（例如 `Get-ChildItem`、`Select-String`、`.\build.bat`、`.\tests\test-deploy.ps1`）
- 不要默认使用 `bash`/`WSL` 命令，除非用户明确要求
- 路径示例优先使用 Windows 路径和 PowerShell 调用方式

## 常用构建命令（PowerShell）

```powershell
# 清理构建产物
.\build.bat clean

# Debug 构建（-O0 + 符号）
.\build.bat debug

# Release 构建（优化/LTO）
.\build.bat release

# 自定义工程名/并行数
.\build.bat release -ProjectName MyApp -Jobs 4
```

说明：

- 所有构建均为 Windows 下交叉编译到 AArch64 Linux
- 产物目录通常位于 `build/aarch64/<Debug|Release>/<ProjectName>`

## 调试/部署命令（PowerShell）

```powershell
# 默认部署并启动 gdbserver（目标板默认 192.168.1.29:1234）
.\debug.bat

# 构建并在目标板上直接运行
.\debug.bat -Run

# 指定目标板与目录
.\debug.bat -RemoteHost 192.168.1.50 -RemoteDir /home/root/tmp
```

`debug.bat` 会完成：

1. 构建项目
2. 检查 SSH 免密
3. 上传 `gdbserver` 和目标程序
4. 启动远程 `gdbserver`
5. 更新 `.vscode/launch.json`

## 测试命令（PowerShell）

```powershell
# 构建并部署测试到目标板执行
.\tests\test-deploy.ps1

# 按 gtest 过滤执行
.\tests\test-deploy.ps1 -TestFilter "Message*"

# 仅构建测试
.\tests\test-deploy.ps1 -BuildOnly
```

测试说明：

- 单元/组件/集成测试统一编译到 AArch64 测试二进制
- 由 Windows PowerShell 脚本通过 SSH/SCP 部署到目标板运行

## 架构速览

### DDS（`src/MB_DDF/DDS/`）

- `DDSCore`：单例，管理共享内存与 Topic 注册
- `Publisher` / `Subscriber`：发布与订阅接口
- `RingBuffer`：每个 Topic 的无锁环形缓冲区
- `SharedMemory`：POSIX 共享内存管理

Topic 命名格式：`domain://address`

示例：`rt://nav/pose`

### PhysicalLayer（`src/MB_DDF/PhysicalLayer/`）

硬件统一通过 `DDS::Handle` 抽象，`HardwareFactory` 支持：

- `can`
- `helm`
- `imu`
- `dyt`
- `ddr`
- `udp`（纯软件链路，适合测试）

### 其他模块

- `SystemTimer`：高精度周期定时器（支持 `"5ms"`, `"10us"`, `"1s"`）
- `Logger`：流式日志（`LOG_INFO << ...`）

## 代码代理工作建议

- 优先做最小改动，避免影响交叉编译/目标板运行流程
- 涉及脚本时优先保持 `PowerShell` 原生兼容性
- 变更测试脚本时，尽量保留现有参数与输出格式
