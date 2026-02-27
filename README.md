# MB-DDF

**Message Bus Data Distribution Framework** - A C++ embedded data distribution framework for real-time systems.

[![Build Status](https://img.shields.io/badge/build-passing-brightgreen)]()
[![Language](https://img.shields.io/badge/language-C%2B%2B20-blue)]()
[![Platform](https://img.shields.io/badge/platform-ARM64%20Linux-orange)]()

## Overview

MB-DDF is a lightweight, high-performance data distribution framework designed for embedded real-time systems. It provides a DDS (Data Distribution Service) implementation with configurable hardware abstraction layers for various communication interfaces.

## Features

- **DDS Implementation**: Publisher-Subscriber pattern with QoS support
- **Hardware Abstraction**: Unified interface for CAN, RS422, UDP, SPI devices
- **Cross-Platform**: Designed for ARM64 Linux with Windows cross-compilation support
- **Configurable Factory**: JSON-based device configuration
- **Modern C++**: C++20 standard with zero-overhead abstractions
- **Unit Testing**: Google Test integration

## Supported Interfaces

| Interface | Status | Description |
|-----------|--------|-------------|
| CAN | ✅ | Controller Area Network via XDMA |
| RS422 | ✅ | Serial communication |
| UDP | ✅ | Network sockets |
| SPI | ✅ | Serial Peripheral Interface |
| Helm | ✅ | PWM motor control |
| DDR | ✅ | Direct memory access |

## Quick Start

### Prerequisites

- CMake 3.10+
- Cross-compiler (aarch64-linux-gnu-g++)
- Windows with WSL or MSYS2 (for cross-compilation)

### Build

```powershell
# Using PowerShell script
.\build.ps1

# Or manual CMake
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=your-toolchain.cmake
make -j$(nproc)
```

### Configuration

Create a JSON configuration file:

```json
{
  "devices": [
    {
      "name": "can0",
      "type": "can",
      "transport": {
        "device_path": "/dev/xdma0",
        "device_offset": "0x50000"
      },
      "mtu": 40
    }
  ]
}
```

## Architecture

```
src/
├── MB_DDF/
│   ├── DDS/              # DDS core implementation
│   ├── PhysicalLayer/    # Hardware abstraction
│   │   ├── Factory/      # Device factory & config
│   │   └── Transport/    # XDMA, UDP, SPI implementations
│   └── Utils/            # Utilities
├── tests/                # Unit tests
└── docs/                 # Documentation
```

## Usage Example

```cpp
#include "MB_DDF/DDS/DDSHandle.h"
#include "MB_DDF/PhysicalLayer/Factory/HardwareFactory.h"

using namespace MB_DDF;

// Create device using preset configuration
auto h_can = PhysicalLayer::Factory::HardwareFactory::create(
    PhysicalLayer::Factory::Preset::CAN()
);

// Or use builder pattern
auto h_rs422 = PhysicalLayer::Factory::Rs422ConfigBuilder()
    .devicePath("/dev/xdma0")
    .offset(0x10000)
    .baudRateDiv(0x0A)
    .create();
```

## Documentation

- [硬件工厂重构设计](docs/TODO--HardwareFactory_Refactor_Design.md)
- [单元测试方案](docs/MB_DDF_单元测试方案_v1.0.md)
- [使用说明](docs/)

## License

MIT License - See LICENSE file for details

## Contributing

This is a personal project. For questions or suggestions, please open an issue.
