/**
 * @file SpiLink.h
 * @brief 基于 spidev 的数据面实现，适配 ILink
 * @date 2026-02-09
 *
 * 说明：
 * - 直接操作 SPI 设备 (/dev/spidevX.Y)，不依赖 XDMA
 * - LinkConfig.name 支持配置参数：
 *   格式："<device_path>[,<speed_hz>[,<mode>[,<bits>]]]"
 *   示例："/dev/spidev0.0" 或 "/dev/spidev0.0,1000000,0,8"
 * - 默认参数：speed=1MHz, mode=0 (SPI_MODE_0), bits=8
 *
 * 设计特点：
 * - SPI 是全双工通信，每次发送必然同时接收
 * - send() 执行 xfer 传输，并将接收数据缓存到 rx_buffer_
 * - receive() 从 rx_buffer_ 读取上次 xfer 的接收数据，不执行实际传输
 * - 每次 send() 会覆盖上一次的接收缓存
 */
#pragma once

#include "MB_DDF/PhysicalLayer/DataPlane/ILink.h"
#include <linux/spi/spidev.h>
#include <string>
#include <vector>
#include <cstdint>

namespace MB_DDF {
namespace PhysicalLayer {
namespace DataPlane {

class SpiLink : public ILink {
public:
    SpiLink();
    ~SpiLink() override;

    bool open(const LinkConfig& cfg) override;
    bool close() override;

    // 执行 SPI 传输（发送 tx_data，接收数据保存到内部缓存）
    // 注意：由于 SPI 全双工特性，发送时必然同时接收数据
    bool    send(const uint8_t* data, uint32_t len) override;

    // 从上一次 send() 的接收缓存中读取数据
    // 返回：实际读取的字节数（<= buf_size），<0 表示错误
    int32_t receive(uint8_t* buf, uint32_t buf_size) override;
    int32_t receive(uint8_t* buf, uint32_t buf_size, uint32_t timeout_us) override;

    LinkStatus getStatus() const override;
    uint16_t   getMTU() const override;

    int getEventFd() const override;
    int getIOFd() const override;

    int ioctl(uint32_t opcode, const void* in, size_t in_len, void* out, size_t out_len) override;

private:
    // 解析配置字符串：device_path[,speed_hz[,mode[,bits]]]
    bool parseConfig(const std::string& name);

    // 内部 SPI 全双工传输接口（同时发送和接收）
    // tx: 发送数据缓冲区（必须非 nullptr）
    // rx: 接收数据缓冲区（必须非 nullptr，与 tx 等长）
    // len: 传输字节数
    bool spi_xfer(const uint8_t* tx, uint8_t* rx, size_t len);

    // 设置非阻塞模式
    static int set_nonblock(int fd);

    int         spi_fd_{-1};
    LinkStatus  status_{LinkStatus::CLOSED};
    LinkConfig  cfg_{};

    // SPI 参数
    uint32_t    spi_speed_{1'000'000};  // 默认 1MHz
    uint8_t     spi_mode_{SPI_MODE_0};
    uint8_t     spi_bits_{8};
    std::string device_path_{"/dev/spidev0.0"};

    // 接收缓存：保存上次 xfer 的接收数据
    std::vector<uint8_t> rx_buffer_;
    uint32_t             rx_data_len_{0};  // 上次接收的实际数据长度
};

} // namespace DataPlane
} // namespace PhysicalLayer
} // namespace MB_DDF
