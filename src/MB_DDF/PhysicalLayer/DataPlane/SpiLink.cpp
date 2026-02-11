/**
 * @file SpiLink.cpp
 * @brief SPI 数据面实现
 */
#include "MB_DDF/PhysicalLayer/DataPlane/SpiLink.h"
#include "MB_DDF/PhysicalLayer/Support/Log.h"

#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/ioctl.h>

namespace MB_DDF {
namespace PhysicalLayer {
namespace DataPlane {

namespace {
    // 简单的字符串分割辅助函数
    std::vector<std::string> split(const std::string& s, char delim) {
        std::vector<std::string> tokens;
        size_t start = 0;
        size_t end = s.find(delim);
        while (end != std::string::npos) {
            tokens.push_back(s.substr(start, end - start));
            start = end + 1;
            end = s.find(delim, start);
        }
        tokens.push_back(s.substr(start));
        return tokens;
    }
}

int SpiLink::set_nonblock(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return -1;
    return 0;
}

SpiLink::SpiLink() = default;

SpiLink::~SpiLink() {
    close();
}

bool SpiLink::parseConfig(const std::string& name) {
    if (name.empty()) {
        // 使用默认值
        device_path_ = "/dev/spidev0.0";
        spi_speed_ = 1'000'000;
        spi_mode_ = SPI_MODE_0;
        spi_bits_ = 8;
        return true;
    }

    // 格式: device_path[,speed_hz[,mode[,bits]]]
    auto tokens = split(name, ',');

    // 第一个token是设备路径
    device_path_ = tokens[0];

    // 解析 speed_hz (可选)
    if (tokens.size() > 1 && !tokens[1].empty()) {
        try {
            spi_speed_ = static_cast<uint32_t>(std::stoul(tokens[1]));
        } catch (...) {
            LOGW("spilink", "parseConfig", 0, "invalid speed, use default %u", spi_speed_);
        }
    }

    // 解析 mode (可选): 0=SPI_MODE_0, 1=SPI_MODE_1, 2=SPI_MODE_2, 3=SPI_MODE_3
    if (tokens.size() > 2 && !tokens[2].empty()) {
        try {
            uint8_t mode = static_cast<uint8_t>(std::stoul(tokens[2]));
            switch (mode) {
                case 0: spi_mode_ = SPI_MODE_0; break;
                case 1: spi_mode_ = SPI_MODE_1; break;
                case 2: spi_mode_ = SPI_MODE_2; break;
                case 3: spi_mode_ = SPI_MODE_3; break;
                default: spi_mode_ = SPI_MODE_0; break;
            }
        } catch (...) {
            LOGW("spilink", "parseConfig", 0, "invalid mode, use default 0");
            spi_mode_ = SPI_MODE_0;
        }
    }

    // 解析 bits (可选): 通常 8
    if (tokens.size() > 3 && !tokens[3].empty()) {
        try {
            spi_bits_ = static_cast<uint8_t>(std::stoul(tokens[3]));
        } catch (...) {
            LOGW("spilink", "parseConfig", 0, "invalid bits, use default 8");
            spi_bits_ = 8;
        }
    }

    return true;
}

bool SpiLink::open(const LinkConfig& cfg) {
    if (spi_fd_ >= 0) return true;

    cfg_ = cfg;

    if (!parseConfig(cfg_.name)) {
        LOGE("spilink", "open", 0, "parse config failed: %s", cfg_.name.c_str());
        return false;
    }

    // 打开 SPI 设备
    spi_fd_ = ::open(device_path_.c_str(), O_RDWR | O_CLOEXEC);
    if (spi_fd_ < 0) {
        LOGE("spilink", "open", errno, "device=%s", device_path_.c_str());
        return false;
    }

    // 配置 SPI 参数
    if (::ioctl(spi_fd_, SPI_IOC_WR_MODE, &spi_mode_) < 0) {
        LOGW("spilink", "SPI_IOC_WR_MODE", errno, "mode=%u", spi_mode_);
    }
    if (::ioctl(spi_fd_, SPI_IOC_WR_BITS_PER_WORD, &spi_bits_) < 0) {
        LOGW("spilink", "SPI_IOC_WR_BITS_PER_WORD", errno, "bits=%u", spi_bits_);
    }
    if (::ioctl(spi_fd_, SPI_IOC_WR_MAX_SPEED_HZ, &spi_speed_) < 0) {
        LOGW("spilink", "SPI_IOC_WR_MAX_SPEED_HZ", errno, "speed=%u", spi_speed_);
    }

    // 读取实际配置用于日志
    uint8_t rd_mode = 0, rd_bits = 0;
    uint32_t rd_speed = 0;
    (void)::ioctl(spi_fd_, SPI_IOC_RD_MODE, &rd_mode);
    (void)::ioctl(spi_fd_, SPI_IOC_RD_BITS_PER_WORD, &rd_bits);
    (void)::ioctl(spi_fd_, SPI_IOC_RD_MAX_SPEED_HZ, &rd_speed);

    // 设置非阻塞模式
    if (set_nonblock(spi_fd_) < 0) {
        LOGW("spilink", "set_nonblock", errno, "fd=%d", spi_fd_);
    }

    // 预分配接收缓存
    rx_buffer_.resize(cfg_.mtu);
    rx_data_len_ = 0;

    status_ = LinkStatus::OPEN;
    LOGI("spilink", "open", 0, "fd=%d device=%s mode=%u/%u bits=%u/%u speed=%u/%u mtu=%u",
         spi_fd_, device_path_.c_str(), spi_mode_, rd_mode, spi_bits_, rd_bits, spi_speed_, rd_speed, cfg_.mtu);
    return true;
}

bool SpiLink::close() {
    if (spi_fd_ >= 0) {
        ::close(spi_fd_);
        spi_fd_ = -1;
    }
    status_ = LinkStatus::CLOSED;
    rx_data_len_ = 0;
    LOGI("spilink", "close", 0, "done");
    return true;
}

bool SpiLink::spi_xfer(const uint8_t* tx, uint8_t* rx, size_t len) {
    if (spi_fd_ < 0 || !tx || !rx || len == 0) return false;

    struct spi_ioc_transfer tr{};
    tr.tx_buf = reinterpret_cast<unsigned long>(tx);
    tr.rx_buf = reinterpret_cast<unsigned long>(rx);
    tr.len = static_cast<__u32>(len);
    tr.speed_hz = spi_speed_;
    tr.bits_per_word = spi_bits_;
    tr.delay_usecs = 0;
    tr.cs_change = 0;

    int rc = ::ioctl(spi_fd_, SPI_IOC_MESSAGE(1), &tr);
    if (rc < 0) {
        LOGE("spilink", "SPI_IOC_MESSAGE", errno, "len=%zu", len);
        return false;
    }
    return true;
}

bool SpiLink::send(const uint8_t* data, uint32_t len) {
    if (spi_fd_ < 0 || status_ != LinkStatus::OPEN) return false;
    if (!data || len == 0) return false;
    if (len > cfg_.mtu) {
        LOGW("spilink", "send", 0, "len=%u exceeds mtu=%u", len, cfg_.mtu);
        return false;
    }

    // 确保接收缓存足够大
    if (rx_buffer_.size() < len) {
        rx_buffer_.resize(len);
    }

    // 执行全双工 SPI 传输：发送 data，接收数据保存到 rx_buffer_
    if (!spi_xfer(data, rx_buffer_.data(), len)) {
        rx_data_len_ = 0;
        return false;
    }

    rx_data_len_ = len;
    return true;
}

int32_t SpiLink::receive(uint8_t* buf, uint32_t buf_size) {
    if (spi_fd_ < 0 || status_ != LinkStatus::OPEN) return -1;
    if (!buf || buf_size == 0) return -1;

    // 从上一次的接收缓存中读取数据，不执行实际 SPI 传输
    if (rx_data_len_ == 0) {
        return 0;  // 没有可用的接收数据
    }

    // 返回实际可读取的数据量
    uint32_t copy_len = (buf_size < rx_data_len_) ? buf_size : rx_data_len_;
    std::memcpy(buf, rx_buffer_.data(), copy_len);

    return static_cast<int32_t>(copy_len);
}

int32_t SpiLink::receive(uint8_t* buf, uint32_t buf_size, uint32_t timeout_us) {
    // 忽略超时参数，因为 receive 只是读取缓存，不执行实际传输
    (void)timeout_us;
    return receive(buf, buf_size);
}

LinkStatus SpiLink::getStatus() const {
    return status_;
}

uint16_t SpiLink::getMTU() const {
    return cfg_.mtu;
}

int SpiLink::getEventFd() const {
    // SPI 本身没有事件 FD，除非配合 GPIO 中断
    return -1;
}

int SpiLink::getIOFd() const {
    return spi_fd_;
}

int SpiLink::ioctl(uint32_t opcode, const void* in, size_t in_len, void* out, size_t out_len) {
    (void)opcode;
    (void)in;
    (void)in_len;
    (void)out;
    (void)out_len;
    return -ENOTSUP;
}

} // namespace DataPlane
} // namespace PhysicalLayer
} // namespace MB_DDF
