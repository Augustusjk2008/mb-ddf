#pragma once

#include "MB_DDF/DDS/DDSCore.h"
#include "MB_DDF/PhysicalLayer/Factory/HardwareFactory.h"
#include "MB_DDF/Debug/Logger.h"

#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

namespace Demo {
namespace MB_DDF_Demos {

inline void RunDemo04_PubAndSubQuickCreateWithHardwareFactory() {
    // Demo04：使用 DDS::PubAndSub 快速把“硬件工厂返回的 DDS::Handle”变成 DDS 接口
    // 目标：
    // - 通过 HardwareFactory::create("udp", ...) 得到一个可收发的 DDS::Handle
    // - 通过 PubAndSub(handle, callback) 直接获得 publisher/subscriber（无需手写 create_publisher/create_subscriber）
    // - 分别演示：
    //   1) 回调订阅（异步线程驱动）
    //   2) 轮询读取（显式 read()）

    // 初始化 DDS（PubAndSub 依赖 DDSCore 已完成初始化）
    auto& dds = MB_DDF::DDS::DDSCore::instance();
    if (!dds.initialize()) {
        LOG_ERROR << "DDSCore initialize failed";
        return;
    }

    // 选用 UDP 后端作为示例：在无硬件环境也能验证
    // 采用 self-loop 配置（同端口）：把远端设置为自身端口，便于 send 后 receive 取回
    static char udp_cfg[] = "127.0.0.1:20002|127.0.0.1:20002";
    auto handle = MB_DDF::PhysicalLayer::Factory::HardwareFactory::create("udp", udp_cfg);
    if (!handle) {
        LOG_ERROR << "HardwareFactory create udp failed";
        return;
    }

    {
        // 1) 回调订阅：PubAndSub 构造时传 callback，内部会创建 subscriber 并启动回调线程
        std::atomic<uint32_t> cb_count{0};
        MB_DDF::DDS::PubAndSub ps(handle, [&](const void* data, size_t size, uint64_t) {
            cb_count.fetch_add(1, std::memory_order_relaxed);
            std::string msg;
            if (data && size > 0) {
                msg.assign(static_cast<const char*>(data), static_cast<const char*>(data) + size);
                if (!msg.empty() && msg.back() == '\0') msg.pop_back();
            }
            LOG_INFO << "PubAndSub callback recv size=" << size << " msg=" << msg;
        });

        // 通过 ps.write(...) 发布到 handle 对应链路
        const char msg[] = "pubandsub_callback";
        ps.write(msg, sizeof(msg));

        // 给回调线程一点处理时间
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        LOG_INFO << "PubAndSub callback count=" << cb_count.load(std::memory_order_relaxed);
    }

    {
        // 2) 轮询读取：PubAndSub 构造时不传 callback，则不启动回调线程
        //    后续显式调用 ps.read(...) 读取（最终会走 handle->receive）
        MB_DDF::DDS::PubAndSub ps(handle, nullptr);
        const char msg[] = "pubandsub_polling";
        ps.write(msg, sizeof(msg));

        // 读取一帧并按字符串打印（仅 demo 用）
        std::array<uint8_t, 2048> buf{};
        const size_t n = ps.read(buf.data(), buf.size());
        std::string got;
        if (n > 0) {
            got.assign(reinterpret_cast<const char*>(buf.data()), reinterpret_cast<const char*>(buf.data()) + n);
            if (!got.empty() && got.back() == '\0') got.pop_back();
        }
        LOG_INFO << "PubAndSub polling read bytes=" << n << " msg=" << got;
    }
}

} // namespace MB_DDF_Demos
} // namespace Demo

