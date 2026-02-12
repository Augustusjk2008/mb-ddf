# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MB_DDF is a C++20 Data Distribution Framework (DDS) for embedded/real-time Linux on AArch64. It provides shared memory IPC with lock-free ring buffers, hardware abstraction for various devices (CAN, RS422, Helm, DMA, UDP), and utilities for timing and logging.

Target platform: ARM64 Linux (cross-compiled from Windows)

## Build Commands

Build scripts are PowerShell with batch wrappers. All builds are cross-compiled for AArch64 Linux.

```powershell
# Clean build artifacts
.\build.bat clean

# Debug build (with symbols, -O0)
.\build.bat debug

# Release build (optimized, -O3, LTO, stripped)
.\build.bat release

# Custom project name
.\build.bat release -ProjectName MyApp -Jobs 4
```

Environment variables (preferred for CI/automation):
- `CROSS_CXX_COMPILER` / `CXX` - Path to aarch64-none-linux-gnu-g++
- `TOOLCHAIN_BIN` - Toolchain bin directory
- `MAKE_PATH` / `CMAKE_MAKE_PROGRAM` - Path to make.exe

Build output: `build/aarch64/<Debug|Release>/<ProjectName>`

## Debug/Deploy Commands

```powershell
# Deploy binary and start gdbserver on target (default: 192.168.1.29:1234)
.\debug.bat

# Build release and run directly on target via SSH
.\debug.bat -Run

# Custom target
.\debug.bat -RemoteHost 192.168.1.50 -RemoteDir /home/root/tmp
```

The debug script:
1. Builds the project
2. Ensures SSH key authentication to target
3. Uploads gdbserver (from `tools/gdbserver`) and the binary
4. Starts gdbserver on target
5. Updates `.vscode/launch.json` with correct paths

Then press F5 in VS Code to start remote debugging.

## Architecture

### DDS (Data Distribution Service)

Located in `src/MB_DDF/DDS/`. Provides shared memory IPC between processes.

Key components:
- `DDSCore` - Singleton managing shared memory and topic registry
- `Publisher` - Writes messages to topics
- `Subscriber` - Reads messages via callback (async) or polling (sync)
- `RingBuffer` - Lock-free circular buffer for each topic
- `SharedMemory` - POSIX shared memory management

Topic naming: Must follow `domain://address` format (e.g., `rt://nav/pose`, `shm://camera/frame`)

Two usage modes:
1. **Shared memory mode** - Data flows through ring buffers in shared memory (`/MB_DDF_SHM` by default)
2. **Handle mode** - Publisher/Subscriber bound to a `DDS::Handle` for hardware abstraction

### PhysicalLayer / Hardware Factory

Located in `src/MB_DDF/PhysicalLayer/`. Abstracts hardware devices behind `DDS::Handle` interface.

Factory names (from `HardwareFactory.cpp`):
- `"can"` - XDMA + CAN device (offset 0x50000, event 5)
- `"helm"` - XDMA + servo/Helm device (offset 0x60000)
- `"imu"` - XDMA + RS422 IMU channel (offset 0x10000, event 1)
- `"dyt"` - XDMA + RS422 alternate channel (offset 0x20000, event 2)
- `"ddr"` - XDMA + DDR DMA channel (h2c=0/c2h=0, event 6)
- `"udp"` - UDP socket link (software-only, for testing)

Factory `create()` accepts a `void* param` for device-specific configuration:
- CAN: `CanDevice::BitTiming*` (or nullptr for 500K default)
- Helm: `HelmDevice::Config*` (or nullptr for defaults)
- IMU/DYT: `Rs422Device::Config*` (or nullptr for defaults)
- UDP: `char*` string like `"local_ip:port|remote_ip:port"`

### PubAndSub

Convenience wrapper in `DDSCore.h` that creates both a Publisher and Subscriber bound to the same Handle, exposing them through DDS interface.

### Timer System

- `SystemTimer` - High-precision periodic timer using POSIX timers with `SIGRTMIN`. Thread runs with `SCHED_FIFO` by default. Supports period strings like `"5ms"`, `"10us"`, `"1s"`.
- `ChronoHelper` - Timing utilities for measuring execution time, averaging, and period jitter statistics.

### Logging

`Logger` singleton in `src/MB_DDF/Debug/Logger.h`. Stream-based logging with severity levels (TRACE to FATAL).

```cpp
LOG_INFO << "Message";  // Auto-includes function and line
LOG_SET_LEVEL_DEBUG();  // Set minimum level
LOG_ENABLE_TIMESTAMP(); // Enable timestamps
```

## Demo Structure

Demo code is in `src/Demo/` as header files included by `main.cpp`:

1. `mb_ddf_demo_01_dds_init_pubsub.h` - DDS initialization and pub/sub
2. `mb_ddf_demo_02_sub_read_modes.h` - Callback vs polling read modes
3. `mb_ddf_demo_03_hardware_factory.h` - All hardware factory devices
4. `mb_ddf_demo_04_pubandsub_factory_dds.h` - PubAndSub with factory handles
5. `mb_ddf_demo_05_system_timer.h` - SystemTimer usage
6. `mb_ddf_demo_06_chrono_helper.h` - ChronoHelper timing utilities
7. `mb_ddf_demo_07_logger.h` - Logger configuration and usage

## Dependencies

Required:
- CMake 3.10+
- aarch64-none-linux-gnu toolchain (GCC 11.3+)
- make.exe or mingw32-make.exe
- sysroot for AArch64 Linux target

Optional (auto-detected):
- liburing (io_uring support)
- libaio (asynchronous I/O)
- libgpiod (GPIO control)

Local libraries in `libs/` are auto-linked if present:
- Headers: `libs/include/`
- Libraries: `libs/lib/*.a` and `*.so`

## VS Code Configuration

`.vscode/launch.json` contains remote GDB debug configuration. The `debug.bat` script automatically updates:
- `miDebuggerPath` - Path to aarch64-none-linux-gnu-gdb.exe
- `program` - Path to built binary
- `setupCommands` text for `set sysroot`

Default target: `192.168.1.29:1234`

## Target Requirements

The target ARM64 Linux board needs:
- SSH server for deployment
- `/dev/xdma0*` device nodes (for XDMA hardware access)
- gdbserver (automatically uploaded by debug script)
- Writable directory at `/home/sast8/tmp` (default remote dir)

## Testing

Unit and integration tests using Google Test. All tests run on the AArch64 target board.

```powershell
# Build and run all tests
.\tests\test-deploy.ps1

# Run specific test category
.\tests\test-deploy.ps1 -TestFilter "Message*"

# Build only (no deploy)
.\tests\test-deploy.ps1 -BuildOnly
```

Test categories:
- `tests/unit/` - Pure logic tests (CRC32, Message, RingBuffer)
- `tests/component/` - Module tests with mocked hardware
- `tests/integration/` - Real hardware tests (XDMA, CAN, etc.)

Tests are cross-compiled to AArch64 and executed on target via SSH.
