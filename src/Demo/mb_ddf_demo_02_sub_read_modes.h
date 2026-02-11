#pragma once

#include "MB_DDF/DDS/DDSCore.h"
#include "MB_DDF/Debug/Logger.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>

namespace Demo {
namespace MB_DDF_Demos {

struct Demo02Payload {
    uint32_t seq;
    uint32_t value;
};

inline void RunDemo02_SubscribeCallbackAndPollingRead() {
    // Demo02：同一 Topic 下展示两种读法
    // 目标：
    // - 回调模式：create_subscriber(..., callback) -> 后台线程收到消息就回调处理
    // - 轮询模式：create_subscriber(..., nullptr) + read(latest/next) -> 主动拉取数据
    //
    // 说明：
    // - 回调模式适合“事件到达立即处理”
    // - 轮询模式适合“主循环按节奏读取”，并能自行选择读最新(latest)还是读队列(next)

    // 初始化 DDS（若已初始化会直接返回成功）
    auto& dds = MB_DDF::DDS::DDSCore::instance();
    if (!dds.initialize()) {
        LOG_ERROR << "DDSCore initialize failed";
        return;
    }

    // Topic 必须包含 ://
    const std::string topic = "demo://dds/read_modes";

    // 创建发布者：用于往该 Topic 写入 Demo02Payload
    auto publisher = dds.create_publisher(topic);
    if (!publisher) {
        LOG_ERROR << "create_publisher failed, topic=" << topic;
        return;
    }

    // 创建“回调订阅者”：收到消息后立即回调，适合事件处理
    std::atomic<uint32_t> cb_count{0};
    auto subscriber_cb = dds.create_subscriber(
        topic,
        true,
        [&](const void* data, size_t size, uint64_t) {
            // 回调收到的是“字节序列”，此处按 Demo02Payload 解析
            if (size < sizeof(Demo02Payload)) {
                LOG_WARN << "callback payload too small: size=" << size;
                return;
            }
            Demo02Payload p{};
            std::memcpy(&p, data, sizeof(p));
            cb_count.fetch_add(1, std::memory_order_relaxed);
            LOG_INFO << "callback seq=" << p.seq << " value=" << p.value;
        });
    if (!subscriber_cb) {
        LOG_ERROR << "create_subscriber(callback) failed, topic=" << topic;
        return;
    }

    // 创建“轮询订阅者”：不传 callback，不会启动订阅线程
    // 后续通过 subscriber_poll->read(...) 主动读取数据
    auto subscriber_poll = dds.create_subscriber(topic, true, nullptr);
    if (!subscriber_poll) {
        LOG_ERROR << "create_subscriber(poll) failed, topic=" << topic;
        return;
    }

    // 连续发布 5 条结构体消息
    for (uint32_t i = 0; i < 5; ++i) {
        Demo02Payload p{ i, i * 10 };
        publisher->publish(&p, sizeof(p));
    }

    // 等待回调线程处理（示例里用 sleep 简化）
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    LOG_INFO << "callback received count=" << cb_count.load(std::memory_order_relaxed);

    // 轮询读取：latest=true -> 读取“最新”一条（适合状态量）
    Demo02Payload p_latest{};
    const size_t n_latest = subscriber_poll->read(&p_latest, sizeof(p_latest), true);
    if (n_latest == sizeof(p_latest)) {
        LOG_INFO << "poll read_latest seq=" << p_latest.seq << " value=" << p_latest.value;
    } else {
        LOG_WARN << "poll read_latest got bytes=" << n_latest;
    }

    // 轮询读取：latest=false -> 逐条读取“下一条未读”（适合队列/事件流）
    while (true) {
        Demo02Payload p_next{};
        const size_t n_next = subscriber_poll->read(&p_next, sizeof(p_next), false);
        if (n_next == 0) break;
        if (n_next != sizeof(p_next)) {
            LOG_WARN << "poll read_next got bytes=" << n_next;
            break;
        }
        LOG_INFO << "poll read_next seq=" << p_next.seq << " value=" << p_next.value;
    }
}

} // namespace MB_DDF_Demos
} // namespace Demo
