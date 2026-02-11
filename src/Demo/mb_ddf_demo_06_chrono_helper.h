#pragma once

#include "MB_DDF/Timer/ChronoHelper.h"
#include "MB_DDF/Debug/Logger.h"

#include <chrono>
#include <cstdint>
#include <thread>

namespace Demo {
namespace MB_DDF_Demos {

inline void RunDemo06_ChronoHelperUsage() {
    // Demo06：ChronoHelper 时间统计工具用法
    // 目标：
    // - timing：测一次函数耗时（打印 us）
    // - timingAverage：多次执行求平均（按量级打印 us/ms/s）
    // - clockStart/clockEnd：分段计时（打印 us）
    // - record：周期抖动统计（按 REPORT_INTERVAL 周期输出统计）
    //
    // 注意：
    // - timing/timingAverage 不允许嵌套调用（内部有保护）
    // - record 的统计数据是全局表，默认按单线程/外部同步使用

    // 关闭“覆盖输出”，让统计以普通打印方式输出
    MB_DDF::Timer::ChronoHelper::set_overwrite_output(false);
    // 确保统计功能开启
    MB_DDF::Timer::ChronoHelper::set_off(false);
    // 清空 counter_id=0 的历史统计
    MB_DDF::Timer::ChronoHelper::reset(0);

    // 1) 单次计时：用一个小计算模拟工作负载
    MB_DDF::Timer::ChronoHelper::timing([&] {
        uint64_t x = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        for (uint64_t i = 0; i < 20000; ++i) x = (x * 1664525ULL) + i;
        if (x == 0) std::this_thread::yield();
    });

    // 2) 平均计时：执行 50 次并打印平均耗时
    MB_DDF::Timer::ChronoHelper::timingAverage(50, [&] {
        uint64_t x = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        for (uint64_t i = 0; i < 2000; ++i) x = (x * 1664525ULL) + i;
        if (x == 0) std::this_thread::yield();
    });

    // 3) 分段计时：手动打点 start/end
    MB_DDF::Timer::ChronoHelper::clockStart(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    MB_DDF::Timer::ChronoHelper::clockEnd(0);

    // 4) 周期抖动统计：模拟 5ms 周期循环，统计偏差
    // expected_interval_us = 5000（5ms）
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(3100)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        MB_DDF::Timer::ChronoHelper::record(0, 5000);
    }

    LOG_INFO << "ChronoHelper record finished";
}

} // namespace MB_DDF_Demos
} // namespace Demo

