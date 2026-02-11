#pragma once

/**
 * @file mb_ddf_demo_08_spi_flash.h
 * @brief W25Q256JV SPI Flash 读写测试 Demo
 * @date 2026-02-09
 *
 * 测试流程：
 * 1. 使用 SpiLink 连接 SPI Flash 设备
 * 2. 读取 JEDEC ID 验证通信 (0x9F)
 * 3. 读取状态寄存器确认设备空闲 (0x05)
 * 4. 扇区擦除 (0x20)
 * 5. 页编程写入测试数据 (0x02)
 * 6. 读取数据并验证 (0x03)
 *
 * SPI 通信说明：
 * - SpiLink 的 send() 执行全双工传输，接收数据保存到内部缓存
 * - SpiLink 的 receive() 从缓存读取上次传输的接收数据
 * - 对于读操作：先 send(命令+dummy)，再 receive() 获取数据
 */

#include "MB_DDF/PhysicalLayer/Factory/HardwareFactory.h"
#include "MB_DDF/PhysicalLayer/ControlPlane/XdmaTransport.h"
#include "MB_DDF/Debug/Logger.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

namespace Demo {
namespace MB_DDF_Demos {

// W25Q256JV 指令定义
namespace W25Q256JV {
    // 读指令
    constexpr uint8_t CMD_READ_JEDEC_ID   = 0x9F;  // 读 JEDEC ID
    constexpr uint8_t CMD_READ_STATUS_REG1 = 0x05; // 读状态寄存器1
    constexpr uint8_t CMD_READ_DATA       = 0x03;  // 读数据

    // 写指令
    constexpr uint8_t CMD_WRITE_ENABLE    = 0x06;  // 写使能
    constexpr uint8_t CMD_PAGE_PROGRAM    = 0x02;  // 页编程
    constexpr uint8_t CMD_SECTOR_ERASE    = 0x20;  // 扇区擦除 (4KB)

    // JEDEC ID 期望值 (Winbond W25Q256JV)
    constexpr uint8_t JEDEC_MANUFACTURER  = 0xEF;  // Winbond
    constexpr uint8_t JEDEC_MEMORY_TYPE   = 0x40;  // SPI Flash
    constexpr uint8_t JEDEC_CAPACITY      = 0x15;  // 16M-bit (2MB)

    // 状态寄存器位定义
    constexpr uint8_t STATUS_BUSY_MASK    = 0x01;  // BUSY 位 (S0)
    constexpr uint8_t STATUS_WEL_MASK     = 0x02;  // 写使能锁存位 (S1)

    // 操作参数
    constexpr uint32_t SECTOR_SIZE        = 4096;  // 扇区大小 4KB
    constexpr uint32_t PAGE_SIZE          = 256;   // 页大小 256字节
    constexpr uint32_t DEFAULT_TEST_ADDR  = 0x000000; // 测试地址 (扇区0起始)
}

inline void RunDemo08_SPIFlashTest(bool switch_spi_device) {
    using namespace W25Q256JV;
    // ============================================================
    // 步骤0: 通过 XDMA 切换 SPI 设备
    // 操作 XDMA 0xA0000 基地址 + 0x44 偏移，写入 0xAA 切换 SPI 设备
    // ============================================================
    if (switch_spi_device) {
        LOG_INFO << "Step 0: Switching SPI device via XDMA...";
        MB_DDF::PhysicalLayer::ControlPlane::XdmaTransport xdma;
        MB_DDF::PhysicalLayer::TransportConfig xdma_cfg;
        xdma_cfg.device_path = "/dev/xdma0";
        xdma_cfg.device_offset = 0xA0000;  // 基地址

        if (xdma.open(xdma_cfg)) {
            uint32_t reg_value = 0xAA;
            if (xdma.writeReg32(0x44, reg_value)) {
                LOG_INFO << "XDMA: Write 0xAA to offset 0x44 (base 0xA0000) succeeded";
            } else {
                LOG_ERROR << "XDMA: Failed to write register";
            }
            xdma.close();
        } else {
            LOG_WARN << "XDMA: Failed to open device, skipping SPI switch";
        }

        // 等待设备切换稳定
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // ============================================================
    // 步骤1: 创建 SpiLink 设备连接
    // 配置格式: "device_path,speed_hz,mode,bits"
    // 使用 10MHz 读取速度，SPI Mode 0，8位数据
    // ============================================================
    static char spi_cfg[] = "/dev/spidev1.0,1000000,0,8";
    LOG_INFO << "Creating SPI device with config: " << spi_cfg;

    auto handle = MB_DDF::PhysicalLayer::Factory::HardwareFactory::create("spi", spi_cfg);
    if (!handle) {
        LOG_ERROR << "Failed to create SPI device!";
        return;
    }
    LOG_INFO << "SPI device created, MTU=" << handle->getMTU();

    // 读取 JEDEC ID
    // 发送: [0x9F, 0xFF, 0xFF, 0xFF] (1字节指令 + 3字节 dummy)
    // 接收缓存会保存 [XX, Manufacturer, MemoryType, Capacity]
    auto read_jedec_id = [&]() -> std::array<uint8_t, 3> {
        std::array<uint8_t, 4> tx_buf = {CMD_READ_JEDEC_ID, 0xFF, 0xFF, 0xFF};
        std::array<uint8_t, 4> rx_buf = {};

        // send 执行全双工传输
        if (!handle->send(tx_buf.data(), 4)) {
            LOG_ERROR << "Failed to send JEDEC ID command";
            return {};
        }

        // receive 从缓存读取接收数据
        int32_t n = handle->receive(rx_buf.data(), 4);
        if (n != 4) {
            LOG_ERROR << "Failed to receive JEDEC ID, ret=" << n;
            return {};
        }

        // rx_buf[0] 是发送 0x9F 时接收到的（未定义值）
        // rx_buf[1-3] 是实际的 JEDEC ID
        return {rx_buf[1], rx_buf[2], rx_buf[3]};
    };

    // 读取状态寄存器
    // 发送: [0x05, 0xFF] (1字节指令 + 1字节 dummy)
    auto read_status_reg1 = [&]() -> uint8_t {
        std::array<uint8_t, 2> tx_buf = {CMD_READ_STATUS_REG1, 0xFF};
        std::array<uint8_t, 2> rx_buf = {};

        if (!handle->send(tx_buf.data(), 2)) {
            LOG_ERROR << "Failed to send READ STATUS command";
            return 0xFF;
        }

        int32_t n = handle->receive(rx_buf.data(), 2);
        if (n != 2) {
            LOG_ERROR << "Failed to receive status register";
            return 0xFF;
        }

        return rx_buf[1]; // 第二个字节是状态值
    };

    // 写使能（只发送，不接收）
    auto write_enable = [&]() -> bool {
        std::array<uint8_t, 1> tx_buf = {CMD_WRITE_ENABLE};
        if (!handle->send(tx_buf.data(), 1)) {
            LOG_ERROR << "Write enable failed";
            return false;
        }
        LOG_INFO << "Write enable executed";
        return true;
    };

    // 等待设备空闲
    auto wait_busy_clear = [&]() -> bool {
        int timeout_ms = 5000; // 5秒超时
        auto start = std::chrono::steady_clock::now();

        while (true) {
            uint8_t status = read_status_reg1();
            if (status == 0xFF) {
                LOG_ERROR << "Failed to read status during wait";
                return false;
            }

            if ((status & STATUS_BUSY_MASK) == 0) {
                LOG_INFO << "Device is idle (BUSY=0)";
                return true;
            }

            // 检查超时
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed > timeout_ms) {
                LOG_ERROR << "Wait for BUSY clear timeout!";
                return false;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    // 扇区擦除（只发送，不接收）
    auto erase_sector = [&](uint32_t addr) -> bool {
        // 1. 写使能
        if (!write_enable()) return false;

        // 2. 发送扇区擦除命令（4字节：指令 + 24位地址）
        std::array<uint8_t, 4> tx_buf = {
            CMD_SECTOR_ERASE,
            static_cast<uint8_t>((addr >> 16) & 0xFF),  // A23-A16
            static_cast<uint8_t>((addr >> 8) & 0xFF),   // A15-A8
            static_cast<uint8_t>(addr & 0xFF)           // A7-A0
        };

        if (!handle->send(tx_buf.data(), 4)) {
            LOG_ERROR << "Sector erase command failed";
            return false;
        }

        LOG_INFO << "Sector erase started at address 0x" << std::hex << addr << std::dec;

        // 3. 等待擦除完成
        if (!wait_busy_clear()) {
            LOG_ERROR << "Sector erase wait failed";
            return false;
        }

        LOG_INFO << "Sector erase completed";
        return true;
    };

    // 页编程（只发送，不接收）
    auto page_program = [&](uint32_t addr, const std::vector<uint8_t>& data) -> bool {
        if (data.size() > PAGE_SIZE) {
            LOG_ERROR << "Data size exceeds page size (256 bytes)";
            return false;
        }

        // 1. 写使能
        if (!write_enable()) return false;

        // 2. 构建页编程命令缓冲区
        // 指令(1) + 地址(3) + 数据(N)
        std::vector<uint8_t> tx_buf;
        tx_buf.reserve(4 + data.size());
        tx_buf.push_back(CMD_PAGE_PROGRAM);
        tx_buf.push_back(static_cast<uint8_t>((addr >> 16) & 0xFF));
        tx_buf.push_back(static_cast<uint8_t>((addr >> 8) & 0xFF));
        tx_buf.push_back(static_cast<uint8_t>(addr & 0xFF));
        tx_buf.insert(tx_buf.end(), data.begin(), data.end());

        if (!handle->send(tx_buf.data(), static_cast<uint32_t>(tx_buf.size()))) {
            LOG_ERROR << "Page program command failed";
            return false;
        }

        LOG_INFO << "Page program started at address 0x" << std::hex << addr
                 << ", size=" << std::dec << data.size() << " bytes";

        // 3. 等待编程完成
        if (!wait_busy_clear()) {
            LOG_ERROR << "Page program wait failed";
            return false;
        }

        LOG_INFO << "Page program completed";
        return true;
    };

    // 读数据
    // 发送: [0x03, A23-A16, A15-A8, A7-A0, dummy x N]
    auto read_data = [&](uint32_t addr, std::vector<uint8_t>& data, size_t len) -> bool {
        // 构建发送缓冲区: 指令(1) + 地址(3) + dummy(len)
        std::vector<uint8_t> tx_buf;
        tx_buf.reserve(4 + len);
        tx_buf.push_back(CMD_READ_DATA);
        tx_buf.push_back(static_cast<uint8_t>((addr >> 16) & 0xFF));
        tx_buf.push_back(static_cast<uint8_t>((addr >> 8) & 0xFF));
        tx_buf.push_back(static_cast<uint8_t>(addr & 0xFF));
        tx_buf.insert(tx_buf.end(), len, 0xFF); // dummy bytes

        if (!handle->send(tx_buf.data(), static_cast<uint32_t>(tx_buf.size()))) {
            LOG_ERROR << "Read data command failed";
            return false;
        }

        // 从缓存读取接收数据
        std::vector<uint8_t> rx_buf(4 + len, 0);
        int32_t n = handle->receive(rx_buf.data(), static_cast<uint32_t>(rx_buf.size()));
        if (n < 0 || static_cast<size_t>(n) != rx_buf.size()) {
            LOG_ERROR << "Read data receive failed, ret=" << n;
            return false;
        }

        // 提取接收到的数据（跳过前4个字节的指令+dummy响应）
        data.resize(len);
        std::memcpy(data.data(), rx_buf.data() + 4, len);

        return true;
    };

    // ============================================================
    // 步骤1: 读取 JEDEC ID 验证通信
    // ============================================================
    LOG_INFO << "Step 1: Reading JEDEC ID...";
    auto jedec_id = read_jedec_id();
    LOG_INFO << "JEDEC ID: 0x" << std::hex << static_cast<int>(jedec_id[0])
             << " 0x" << static_cast<int>(jedec_id[1])
             << " 0x" << static_cast<int>(jedec_id[2]) << std::dec;

    // 验证 ID
    if (jedec_id[0] == JEDEC_MANUFACTURER &&
        jedec_id[1] == JEDEC_MEMORY_TYPE &&
        jedec_id[2] == JEDEC_CAPACITY) {
        LOG_INFO << "JEDEC ID verification PASSED - W25Q256JV detected";
    } else {
        LOG_WARN << "JEDEC ID mismatch! Expected: EF 40 15, Got: "
                 << std::hex << static_cast<int>(jedec_id[0]) << " "
                 << static_cast<int>(jedec_id[1]) << " "
                 << static_cast<int>(jedec_id[2]) << std::dec;
        LOG_WARN << "Continuing with test anyway...";
    }

    // ============================================================
    // 步骤2: 检查状态寄存器，确认设备空闲
    // ============================================================
    LOG_INFO << "Step 2: Checking status register...";
    uint8_t status = read_status_reg1();
    LOG_INFO << "Status Register-1: 0x" << std::hex << static_cast<int>(status) << std::dec
             << " (BUSY=" << (status & STATUS_BUSY_MASK)
             << ", WEL=" << ((status >> 1) & 1) << ")";

    if (status & STATUS_BUSY_MASK) {
        LOG_INFO << "Device is busy, waiting...";
        if (!wait_busy_clear()) {
            LOG_ERROR << "Device busy wait failed";
            return;
        }
    }

    // ============================================================
    // 步骤3: 扇区擦除
    // ============================================================
    LOG_INFO << "Step 3: Erasing sector at address 0x" << std::hex << DEFAULT_TEST_ADDR << std::dec;
    if (!erase_sector(DEFAULT_TEST_ADDR)) {
        LOG_ERROR << "Sector erase failed";
        return;
    }

    // ============================================================
    // 步骤4: 页编程写入测试数据
    // ============================================================
    std::vector<uint8_t> test_data = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88,
                                       0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x99};
    LOG_INFO << "Step 4: Programming page at address 0x" << std::hex << DEFAULT_TEST_ADDR
             << ", data size=" << std::dec << test_data.size() << " bytes";

    if (!page_program(DEFAULT_TEST_ADDR, test_data)) {
        LOG_ERROR << "Page program failed";
        return;
    }

    // ============================================================
    // 步骤5: 读数据并验证
    // ============================================================
    LOG_INFO << "Step 5: Reading back data...";
    std::vector<uint8_t> read_back;
    if (!read_data(DEFAULT_TEST_ADDR, read_back, test_data.size())) {
        LOG_ERROR << "Read data failed";
        return;
    }

    // 打印读取的数据
    std::string read_str = "Read data: ";
    for (auto b : read_back) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", b);
        read_str += buf;
    }
    LOG_INFO << read_str;

    // 验证数据
    bool match = true;
    for (size_t i = 0; i < test_data.size(); ++i) {
        if (test_data[i] != read_back[i]) {
            match = false;
            LOG_ERROR << "Data mismatch at offset " << i << ": expected 0x" << std::hex
                      << static_cast<int>(test_data[i]) << ", got 0x" << static_cast<int>(read_back[i])
                      << std::dec;
            break;
        }
    }

    if (match) {
        LOG_INFO << "===== SPI Flash Test PASSED =====";
        LOG_INFO << "Write data matches read data perfectly!";
    } else {
        LOG_ERROR << "===== SPI Flash Test FAILED =====";
        LOG_ERROR << "Data verification failed!";
    }

    // 打印写入的数据
    std::string write_str = "Write data: ";
    for (auto b : test_data) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", b);
        write_str += buf;
    }
    LOG_INFO << write_str;

    LOG_INFO << "===== SPI Flash Test End =====";
}

} // namespace MB_DDF_Demos
} // namespace Demo
