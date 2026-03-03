#pragma once

#include "MB_DDF/PhysicalLayer/Device/CanFdDevice.h"
#include "MB_DDF/PhysicalLayer/ControlPlane/XdmaTransport.h"
#include "MB_DDF/Debug/Logger.h"
#include "MB_DDF/PhysicalLayer/Hardware/pl_canfd.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

namespace Demo {
namespace MB_DDF_Demos {

inline void RunDemo09_CanFdDeviceTest() {
    constexpr uint16_t CANFD_MTU = 64;

    MB_DDF::PhysicalLayer::ControlPlane::XdmaTransport transport;

    MB_DDF::PhysicalLayer::TransportConfig tp_cfg;
    tp_cfg.device_path = "/dev/xdma0";
    tp_cfg.device_offset = 0x50000;
    tp_cfg.event_number = 5;

    LOG_INFO << "[1] Opening XdmaTransport...";
    if (!transport.open(tp_cfg)) {
        LOG_ERROR << "XdmaTransport open failed (requires /dev/xdma0 on target)";
        LOG_INFO << "========== CanFdDevice Test Skipped (no hardware) ==========";
        return;
    }
    LOG_INFO << "XdmaTransport opened successfully";

    MB_DDF::PhysicalLayer::Device::CanFDDevice canfd(transport, CANFD_MTU);

    LOG_INFO << "[2] Initializing CanFdDevice...";
    MB_DDF::PhysicalLayer::LinkConfig link_cfg;
    link_cfg.mtu = CANFD_MTU;
    link_cfg.name = "canfd_test";

    if (!canfd.open(link_cfg)) {
        LOG_ERROR << "CanFdDevice open failed";
        transport.close();
        LOG_INFO << "========== CanFdDevice Test Failed ==========";
        return;
    }
    LOG_INFO << "CanFdDevice initialized, MTU=" << canfd.getMTU();

    LOG_INFO << "[3] Configuring baud rate (arbitration: 1Mbps, data: 1Mbps)...";
    {
        uint32_t arb_baud = 1000000;
        uint32_t data_baud = 1000000;

        int ret = canfd.ioctl(CAN_DEV_SET_BAUD, &arb_baud, sizeof(arb_baud), nullptr, 0);
        if (ret < 0) {
            LOG_ERROR << "Failed to set arbitration baud rate, ret=" << ret;
        } else {
            LOG_INFO << "Arbitration baud rate set to " << arb_baud;
        }

        ret = canfd.ioctl(CAN_DEV_SET_DATA_BAUD, &data_baud, sizeof(data_baud), nullptr, 0);
        if (ret < 0) {
            LOG_ERROR << "Failed to set data baud rate, ret=" << ret;
        } else {
            LOG_INFO << "Data baud rate set to " << data_baud;
        }
    }

    LOG_INFO << "[4] Enabling loopback mode...";
    {
        uint32_t enable = 1;
        int ret = canfd.ioctl(CAN_DEV_SET_LOOPBACK, &enable, sizeof(enable), nullptr, 0);
        if (ret < 0) {
            LOG_ERROR << "Failed to enable loopback mode, ret=" << ret;
        } else {
            LOG_INFO << "Loopback mode enabled";
        }
    }
    
    LOG_INFO << "[4.5] Disabling receive filters...";
    canfd.ioctl(CAN_DEV_SET_FILTER, nullptr, 0, nullptr, 0);

    // 等待所有配置生效
    usleep(300000);

    LOG_INFO << "[5] Testing CAN FD frame loopback...";
    {
        MB_DDF::PhysicalLayer::Device::CanFrame tx_frame;
        tx_frame.id = 0x123;
        tx_frame.ide = false;
        tx_frame.rtr = false;
        tx_frame.fdf = true;
        tx_frame.brs = true;
        tx_frame.esi = false;
        tx_frame.dlc = 8;
        tx_frame.len = 8;
        tx_frame.data = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};

        LOG_INFO << "Sending CAN FD frame: id=0x" << std::hex << tx_frame.id << std::dec
                 << ", dlc=" << static_cast<int>(tx_frame.dlc)
                 << ", fdf=" << tx_frame.fdf
                 << ", brs=" << tx_frame.brs;

        bool send_ok = canfd.send(tx_frame);
        LOG_INFO << "Send result: " << (send_ok ? "OK" : "FAILED");

        if (send_ok) {
            // 等待硬件完成发送和回环
            usleep(50000);  // 50ms

            MB_DDF::PhysicalLayer::Device::CanFrame rx_frame;
            int32_t recv_result = canfd.receive(rx_frame, 100000);

            if (recv_result > 0) {
                LOG_INFO << "Received frame: id=0x" << std::hex << rx_frame.id << std::dec
                         << ", ide=" << rx_frame.ide
                         << ", fdf=" << rx_frame.fdf
                         << ", dlc=" << static_cast<int>(rx_frame.dlc)
                         << ", len=" << static_cast<int>(rx_frame.len);

                bool match = (rx_frame.id == tx_frame.id && rx_frame.len == tx_frame.len);
                if (match && rx_frame.data.size() >= tx_frame.data.size()) {
                    for (size_t i = 0; i < tx_frame.data.size(); ++i) {
                        if (rx_frame.data[i] != tx_frame.data[i]) {
                            match = false;
                            break;
                        }
                    }
                }
                LOG_INFO << "Loopback verification: " << (match ? "PASSED" : "FAILED");
            } else if (recv_result == 0) {
                LOG_ERROR << "Receive timeout - loopback not working";
            } else {
                LOG_ERROR << "Receive error: " << recv_result;
            }
        }
    }

    LOG_INFO << "[6] Testing extended ID CAN FD frame loopback...";
    {
        MB_DDF::PhysicalLayer::Device::CanFrame tx_frame;
        tx_frame.id = 0x1ABCDEF;
        tx_frame.ide = true;
        tx_frame.rtr = false;
        tx_frame.fdf = true;
        tx_frame.brs = true;
        tx_frame.esi = false;
        tx_frame.dlc = 15;
        tx_frame.len = 64;
        tx_frame.data.resize(64);
        for (int i = 0; i < 64; ++i) {
            tx_frame.data[i] = static_cast<uint8_t>(i);
        }

        LOG_INFO << "Sending extended CAN FD frame: id=0x" << std::hex << tx_frame.id << std::dec
                 << ", ide=" << tx_frame.ide
                 << ", len=" << static_cast<int>(tx_frame.len);

        bool send_ok = canfd.send(tx_frame);
        LOG_INFO << "Send result: " << (send_ok ? "OK" : "FAILED");

        if (send_ok) {
            // 等待硬件完成发送和回环
            usleep(50000);  // 50ms

            MB_DDF::PhysicalLayer::Device::CanFrame rx_frame;
            int32_t recv_result = canfd.receive(rx_frame, 100000);

            if (recv_result > 0) {
                LOG_INFO << "Received frame: id=0x" << std::hex << rx_frame.id << std::dec
                         << ", ide=" << rx_frame.ide
                         << ", len=" << static_cast<int>(rx_frame.len);

                bool match = (rx_frame.id == tx_frame.id && rx_frame.ide == tx_frame.ide && rx_frame.len == tx_frame.len);
                LOG_INFO << "Loopback verification: " << (match ? "PASSED" : "FAILED");
            } else {
                LOG_ERROR << "Receive failed: " << (recv_result == 0 ? "timeout" : std::to_string(recv_result));
            }
        }
    }

    LOG_INFO << "[7] Closing CanFdDevice...";
    canfd.close();
    LOG_INFO << "CanFdDevice closed";

    LOG_INFO << "[8] Closing XdmaTransport...";
    transport.close();
    LOG_INFO << "XdmaTransport closed";

    LOG_INFO << "========== CanFdDevice Loopback Test Complete ==========";

}

}   // 
}   //
