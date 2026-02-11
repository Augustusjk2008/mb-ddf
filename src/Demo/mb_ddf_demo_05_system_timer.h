#pragma once

#include "MB_DDF/Timer/SystemTimer.h"
#include "MB_DDF/Debug/Logger.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace Demo {
namespace MB_DDF_Demos {

inline void RunDemo05_SystemTimerUsage() {
    // Demo05：SystemTimer 高精度周期定时器用法
    // 目标：
    // - 以 "5ms" 周期启动 SystemTimer
    // - 在回调里累加计数
    // - 运行一小段时间后 stop，并输出触发次数
    //
    // 注意：
    // - SystemTimer 依赖 Linux 的 timer/signal 机制（目标平台为 aarch64 Linux）
    // - 线程调度策略/优先级/绑核等可通过 SystemTimerOptions 配置

    // 统计定时回调触发次数
    std::atomic<uint64_t> ticks{0};

    // start(period_str, callback, opt)
    // - period_str 支持 "s/ms/us/ns"
    // - callback 在定时触发时执行（参数为 opt.user_data）
    auto timer = MB_DDF::Timer::SystemTimer::start(
        "5ms",
        [&](void*) { ticks.fetch_add(1, std::memory_order_relaxed); },
        {});

    if (!timer) {
        LOG_ERROR << "SystemTimer start failed";
        return;
    }

    // 让定时器跑一会儿
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));

    // 停止定时器并输出触发次数
    timer->stop();
    LOG_INFO << "SystemTimer ticks=" << ticks.load(std::memory_order_relaxed);
}

} // namespace MB_DDF_Demos
} // namespace Demo

