#include "MB_DDF/PhysicalLayer/Factory/HardwareFactory.h"
#include "MB_DDF/PhysicalLayer/ControlPlane/XdmaTransport.h"
#include "MB_DDF/PhysicalLayer/DataPlane/UdpLink.h"
#include "MB_DDF/PhysicalLayer/DataPlane/SpiLink.h"
#include "MB_DDF/PhysicalLayer/Device/CanDevice.h"
#include "MB_DDF/PhysicalLayer/Device/HelmDevice.h"
#include "MB_DDF/PhysicalLayer/Device/Yc7Device.h"
#include "MB_DDF/PhysicalLayer/Device/Rs422Device.h"
#include "MB_DDF/PhysicalLayer/Device/DdrDevice.h"
#include "MB_DDF/PhysicalLayer/Device/DatalinkDevice.h"
#include "MB_DDF/PhysicalLayer/Hardware/io_map.h"
#include <string>

namespace MB_DDF {
namespace PhysicalLayer {
namespace Factory {

namespace {
struct HandleImpl : public DDS::Handle {
    ControlPlane::XdmaTransport tp;
    std::unique_ptr<DataPlane::ILink> dev = nullptr;
    uint32_t mtu{1500};

    bool send(const uint8_t* data, uint32_t len) override { return dev->send(data, len); }
    int32_t receive(uint8_t* buf, uint32_t buf_size) override { return dev->receive(buf, buf_size); }
    int32_t receive(uint8_t* buf, uint32_t buf_size, uint32_t timeout_us) override { return dev->receive(buf, buf_size, timeout_us); }
    uint32_t getMTU() const override { return mtu; }
};
}

std::shared_ptr<DDS::Handle> HardwareFactory::create(const std::string& name, void* param) {
    auto h = std::make_unique<HandleImpl>();
    TransportConfig tc;
    tc.device_path = "/dev/xdma0";

    if (name == "can") {
        tc.device_offset = 0x50000;
        tc.event_number = 5;
        h->tp.open(tc);
        h->mtu = 40;
        h->dev = std::make_unique<Device::CanDevice>(h->tp, h->mtu);
        LinkConfig lc; 
        h->dev->open(lc);
        if (param != nullptr) {
            uint32_t off = 0; 
            h->dev->ioctl(Device::CanDevice::IOCTL_SET_LOOPBACK, &off, sizeof(off));
            h->dev->ioctl(Device::CanDevice::IOCTL_SET_BIT_TIMING, param, sizeof(Device::CanDevice::BitTiming));
        } else {
            uint32_t baud = 500000;
            uint32_t off = 1; 
            h->dev->ioctl(Device::CanDevice::IOCTL_SET_LOOPBACK, &off, sizeof(off));
            h->dev->ioctl(Device::CanDevice::IOCTL_SET_BIT_TIMING, &baud, sizeof(baud));
        }
    } else if (name == "helm") {
        tc.device_offset = 0x60000;
        h->tp.open(tc);
        h->mtu = 16;
        h->dev = std::make_unique<Device::HelmDevice>(h->tp, h->mtu);
        LinkConfig lc; 
        h->dev->open(lc);
        if (param != nullptr) {
            h->dev->ioctl(Device::HelmDevice::IOCTL_HELM, param, sizeof(Device::HelmDevice::Config));
        } else {
            Device::HelmDevice::Config ctl_helm = {
                .pwm_freq = 8000,
                .out_enable = 0xF,
                .ad_filter = 1,
            };
            h->dev->ioctl(Device::HelmDevice::IOCTL_HELM, &ctl_helm, sizeof(ctl_helm));
        }
    } else if (name == "yx") {
        tc.event_number = (int)Hardware::SerialPortNum::YX;
        tc.device_offset = tc.event_number * 0x10000;
        h->tp.open(tc);
        h->mtu = 255;
        h->dev = std::make_unique<Device::Rs422Device>(h->tp, h->mtu);
        LinkConfig lc; 
        h->dev->open(lc);
        Device::Rs422Device::Config cfg_return;
        if (param != nullptr) {
            h->dev->ioctl(Device::Rs422Device::IOCTL_CONFIG, 
                param, sizeof(Device::Rs422Device::Config), 
                &cfg_return, sizeof(Device::Rs422Device::Config)); 
        } else {
            Device::Rs422Device::Config cfg = {
                .ucr = 0x30,
                .mcr = 0x20,
                .brsr = 0x09,
                .icr = 0x01,
                .tx_head_lo = 0xAA,
                .tx_head_hi = 0x56,
                .rx_head_lo = 0x55,
                .rx_head_hi = 0xA6,
                .lpb = 0x00,    // AF = 回环
                .intr = 0xAE,   // AE = 自控中断
                .evt = 1250,    // 125 = 脉冲宽度 1us
            };
            h->dev->ioctl(Device::Rs422Device::IOCTL_CONFIG, 
                &cfg, sizeof(cfg), 
                &cfg_return, sizeof(cfg_return)); 
        }
    } else if (name == "imu") {
        tc.event_number = (int)Hardware::SerialPortNum::IMU;
        tc.device_offset = tc.event_number * 0x10000;
        h->tp.open(tc);
        h->mtu = 255;
        h->dev = std::make_unique<Device::Rs422Device>(h->tp, h->mtu);
        LinkConfig lc; 
        h->dev->open(lc);
        Device::Rs422Device::Config cfg_return;
        if (param != nullptr) {
            h->dev->ioctl(Device::Rs422Device::IOCTL_CONFIG, 
                param, sizeof(Device::Rs422Device::Config), 
                &cfg_return, sizeof(Device::Rs422Device::Config)); 
        } else {
            Device::Rs422Device::Config cfg = {
                .ucr = 0x30,
                .mcr = 0x20,
                .brsr = 0x0A,
                .icr = 0x01,
                .tx_head_lo = 0xAA,
                .tx_head_hi = 0x1A,
                .rx_head_lo = 0xAA,
                .rx_head_hi = 0x1A,
                .lpb = 0x00,    // AF = 回环
                .intr = 0xAE,   // AE = 自控中断
                .evt = 1250,    // 125 = 脉冲宽度 1us
            };
            h->dev->ioctl(Device::Rs422Device::IOCTL_CONFIG, 
                &cfg, sizeof(cfg), 
                &cfg_return, sizeof(cfg_return)); 
        }
    } else if (name == "dyt") {
        tc.event_number = (int)Hardware::SerialPortNum::DYT;
        tc.device_offset = tc.event_number * 0x10000;
        h->tp.open(tc);
        h->mtu = 255;
        h->dev = std::make_unique<Device::Rs422Device>(h->tp, h->mtu);
        LinkConfig lc; 
        h->dev->open(lc);
        Device::Rs422Device::Config cfg_return;
        if (param != nullptr) {
            h->dev->ioctl(Device::Rs422Device::IOCTL_CONFIG, 
                param, sizeof(Device::Rs422Device::Config), 
                &cfg_return, sizeof(Device::Rs422Device::Config)); 
        } else {
            Device::Rs422Device::Config cfg = {
                .ucr = 0x30,
                .mcr = 0x20,
                .brsr = 0x0A,
                .icr = 0x01,
                .tx_head_lo = 0xAA,
                .tx_head_hi = 0x1A,
                .rx_head_lo = 0xAA,
                .rx_head_hi = 0x1A,
                .lpb = 0x00,    // AF = 回环
                .intr = 0xAE,   // AE = 自控中断
                .evt = 1250,    // 125 = 脉冲宽度 1us
            };
            h->dev->ioctl(Device::Rs422Device::IOCTL_CONFIG, 
                &cfg, sizeof(cfg), 
                &cfg_return, sizeof(cfg_return)); 
        }
    } else if (name == "yc_7") {
        tc.event_number = 0xB;
        tc.device_offset = tc.event_number * 0x10000;
        h->tp.open(tc);
        h->mtu = 2500;
        h->dev = std::make_unique<Device::Yc7Device>(h->tp, h->mtu);
        LinkConfig lc; 
        h->dev->open(lc);
        Device::Yc7Device::Config cfg_return;
        if (param != nullptr) {
            h->dev->ioctl(Device::Yc7Device::IOCTL_CONFIG, 
                param, sizeof(Device::Yc7Device::Config), 
                &cfg_return, sizeof(Device::Yc7Device::Config)); 
        } else {
            Device::Yc7Device::Config cfg = {
                .ucr = 0x30,
                .mcr = 0x20,
                .brsr = 0x0C,
                .icr = 0x01,
                .lpb = 0x00,    // AF = 回环
                .intr = 0xAE,   // AE = 自控中断
                .evt = 1250,    // 125 = 脉冲宽度 1us
            };
            h->dev->ioctl(Device::Yc7Device::IOCTL_CONFIG, 
                &cfg, sizeof(cfg), 
                &cfg_return, sizeof(cfg_return)); 
        }
    } else if (name == "ddr") {
        tc.dma_h2c_channel = 0;
        tc.dma_c2h_channel = 0;
        tc.device_offset = 0x60000;
        tc.event_number = 6;
        h->tp.open(tc);
        if (param != nullptr) {
            h->mtu = *((uint32_t*)param);
        } else {
            h->mtu = 640 * 1024;
        }
        h->dev = std::make_unique<Device::DdrDevice>(h->tp, h->mtu);
        LinkConfig lc;
        h->dev->open(lc);
    } else if (name == "sjl") {
        tc.device_offset = 0x70000;
        tc.event_number = 7;
        h->tp.open(tc);
        h->mtu = 100;
        h->dev = std::make_unique<Device::DatalinkDevice>(h->tp, h->mtu);
        LinkConfig lc;
        h->dev->open(lc);
    } else if (name == "spi") {
        // SPI 数据面实现，直接操作 /dev/spidevX.Y
        // param 格式: "device_path[,speed_hz[,mode[,bits]]]"
        // 示例: "/dev/spidev0.0" 或 "/dev/spidev0.0,1000000,0,8"
        h->mtu = 4096;  // SPI 典型最大传输大小
        h->dev = std::make_unique<DataPlane::SpiLink>();
        LinkConfig lc;
        if (param != nullptr) {
            lc.name = std::string((const char*)param);
        } else {
            lc.name = std::string("/dev/spidev0.0");  // 默认设备
        }
        lc.mtu = h->mtu;
        h->dev->open(lc);
    } else if (name == "udp") {
        h->mtu = 60000;
        h->dev = std::make_unique<DataPlane::UdpLink>();
        LinkConfig lc;
        if (param != nullptr) {
            lc.name = std::string((const char*)param);
        } else {
            lc.name = std::string("12345");
        }
        lc.mtu = h->mtu;
        h->dev->open(lc);
    } else {
        return nullptr;
    }
    return h;
}

} // namespace Factory
} // namespace PhysicalLayer
} // namespace MB_DDF