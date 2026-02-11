#pragma once

#include "MB_DDF/PhysicalLayer/Factory/HardwareFactory.h"
#include "MB_DDF/Debug/Logger.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace Demo {
namespace MB_DDF_Demos {

inline void RunDemo03_AllHardwareFactoryExamples() {
    // Demo03：当前已实现的硬件工厂（HardwareFactory）用法展示
    // 目标：
    // - 展示当前工程内 HardwareFactory::create(name, param) 支持的 name
    // - 展示 param（第二个参数）的基本用法：不同 name 对应不同类型的配置指针
    // - 使用 UDP 后端做一次“可在无硬件环境下验证的收发”
    //
    // 注意：
    // - can/helm/imu/dyt/ddr 依赖目标机上的 /dev/xdma0*，在 PC/无驱动环境下可能创建失败
    // - udp 是纯软件链路，适合作为开发期连通性验证

    // UDP 配置字符串（UdpLink 的 LinkConfig.name 解析规则见 UdpLink.h）：
    // "<local_ip>:<local_port>|<remote_ip>:<remote_port>"
    // 这里使用同端口 self-loop：同一个 socket 既绑定又把默认远端设为自身端口
    static char udp_cfg[] = "127.0.0.1:20001|127.0.0.1:20001";

    // 当前实现的工厂名称（以 HardwareFactory.cpp 为准）：
    // - can  : XDMA + CAN 设备
    // - helm : XDMA + 舵机设备
    // - imu  : XDMA + RS422 设备（IMU 通道，offset/event 不同）
    // - dyt  : XDMA + RS422 设备（另一通道，offset/event 不同）
    // - ddr  : XDMA + DDR 设备（DMA）
    // - udp  : UDP 链路
    const std::array<const char*, 6> names = { "can", "helm", "imu", "dyt", "ddr", "udp" };
    for (const char* name : names) {
        // param 的含义取决于 name：
        // - can  : param 指向 CanDevice::BitTiming（或不传 -> 默认 500000 波特率）
        // - helm : param 指向 HelmDevice::Config（或不传 -> 使用默认配置）
        // - imu/dyt : param 指向 Rs422Device::Config（或不传 -> 使用默认配置）
        // - ddr  : 当前不使用 param
        // - udp  : param 指向以 '\0' 结尾的配置字符串（LinkConfig.name）
        LOG_INFO << "HardwareFactory creating... name=" << name;
        void* param = nullptr;
        if (std::string(name) == "udp") {
            param = udp_cfg;
        }

        // 创建 DDS::Handle：上层可用 send/receive 进行收发
        auto handle = MB_DDF::PhysicalLayer::Factory::HardwareFactory::create(name, param);
        if (!handle) {
            LOG_ERROR << "HardwareFactory create failed, name=" << name;
            continue;
        }

        // 打印该 handle 的 MTU（不同后端差异很大）
        LOG_INFO << "HardwareFactory create ok, name=" << name << ", mtu=" << handle->getMTU();

        if (std::string(name) == "udp") {
            // UDP：发送一段 bytes
            const char payload[] = "demo_udp_payload";
            const bool send_ok = handle->send(reinterpret_cast<const uint8_t*>(payload), sizeof(payload));
            LOG_INFO << "udp send ok=" << (send_ok ? 1 : 0);

            // UDP：接收一段 bytes（带超时），并打印收到的长度
            std::array<uint8_t, 2048> buf{};
            const int32_t n = handle->receive(buf.data(), static_cast<uint32_t>(buf.size()), 200000);
            LOG_INFO << "udp receive bytes=" << n;
        }
    }

    // 轻微 sleep，避免 demo 连续打印导致输出过于集中
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

} // namespace MB_DDF_Demos
} // namespace Demo

