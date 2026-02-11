/**
 * @file DatalinkDevice.h
 * @brief Datalink 设备数据面适配器
 */
#pragma once

#include "MB_DDF/PhysicalLayer/Device/TransportLinkAdapter.h"
#include "MB_DDF/PhysicalLayer/ControlPlane/IDeviceTransport.h"
#include <cstdint>

namespace MB_DDF {
namespace PhysicalLayer {
namespace Device {

class DatalinkDevice : public MB_DDF::PhysicalLayer::Device::TransportLinkAdapter {
public:
    explicit DatalinkDevice(MB_DDF::PhysicalLayer::ControlPlane::IDeviceTransport& tp, uint16_t mtu)
        : TransportLinkAdapter(tp, mtu) {}

    bool open(const LinkConfig& cfg) override;
    bool close() override;

    bool    send(const uint8_t* data, uint32_t len) override;
    int32_t receive(uint8_t* buf, uint32_t buf_size) override;
    int32_t receive(uint8_t* buf, uint32_t buf_size, uint32_t timeout_us) override;

    int ioctl(uint32_t opcode, const void* in, size_t in_len, void* out = nullptr, size_t out_len = 0) override;
};

} // namespace Device
} // namespace PhysicalLayer
} // namespace MB_DDF
