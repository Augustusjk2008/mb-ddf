#pragma once

#include "MB_DDF/DDS/DDSCore.h"
#include "MB_DDF/Debug/Logger.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace Demo {
namespace MB_DDF_Demos {

inline void RunDemo01_DDSInitAndPubSub() {
    // Demo01：DDS 初始化 + 共享内存发布订阅最小闭环
    // 目标：
    // - 展示 DDSCore 的初始化方式
    // - 展示 Publisher 发布一段字节数据
    // - 展示 Subscriber 通过回调异步接收，并打印时间戳与载荷
    //
    // 注意：
    // - Topic 名称必须包含 "://"
    // - 回调模式会启动订阅线程，通常需要给一点时间让线程处理数据

    // 获取 DDSCore 单例，并初始化共享内存与 Topic 注册表
    auto& dds = MB_DDF::DDS::DDSCore::instance();
    if (!dds.initialize()) {
        LOG_ERROR << "DDSCore initialize failed";
        return;
    }

    // 选择一个符合规范的 Topic 名称：domain://address
    const std::string topic = "demo://dds/init_pubsub";

    // 创建发布者：绑定到共享内存中的该 Topic
    auto publisher = dds.create_publisher(topic);
    if (!publisher) {
        LOG_ERROR << "create_publisher failed, topic=" << topic;
        return;
    }

    // 创建订阅者：回调模式（异步线程触发 callback）
    // received 用于统计回调触发次数（示例演示）
    std::atomic<uint32_t> received{0};
    auto subscriber = dds.create_subscriber(
        topic,
        true,
        [&](const void* data, size_t size, uint64_t timestamp_ns) {
            // 将收到的字节载荷按字符串打印（仅用于 demo）
            // 如果 payload 末尾是 '\0'，去掉以便输出更干净
            std::string msg;
            if (data && size > 0) {
                msg.assign(static_cast<const char*>(data), static_cast<const char*>(data) + size);
                if (!msg.empty() && msg.back() == '\0') msg.pop_back();
            }
            received.fetch_add(1, std::memory_order_relaxed);
            LOG_INFO << "callback recv: size=" << size << " ts(ns)=" << timestamp_ns << " msg=" << msg;
        });

    if (!subscriber) {
        LOG_ERROR << "create_subscriber failed, topic=" << topic;
        return;
    }

    // 连续发布 3 条消息
    for (uint32_t i = 0; i < 3; ++i) {
        // 构造消息字符串
        std::string out = "hello_dds_" + std::to_string(i);

        // 演示写入“原始字节”：这里额外补一个 '\0' 便于回调按字符串打印
        std::vector<char> payload(out.begin(), out.end());
        payload.push_back('\0');

        // 发布到共享内存 ring buffer
        const bool ok = publisher->publish(payload.data(), payload.size());
        LOG_INFO << "publish seq=" << i << " ok=" << (ok ? 1 : 0);
    }

    // 给订阅线程一点时间接收并回调
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    // 输出回调接收计数
    LOG_INFO << "callback received count=" << received.load(std::memory_order_relaxed);
}

} // namespace MB_DDF_Demos
} // namespace Demo
