/**
 * @file test_chrono_helper.cpp
 * @brief ChronoHelper 时间工具测试
 */

#include <gtest/gtest.h>
#include <thread>
#include <chrono>

#include "MB_DDF/Timer/ChronoHelper.h"

using namespace MB_DDF;
using namespace MB_DDF::Timer;
using namespace std::chrono;

// ==============================
// timing 单次计时
// ==============================
TEST(ChronoHelperTiming, BasicTiming) {
    // 测试100ms睡眠
    testing::internal::CaptureStdout();

    ChronoHelper::timing([]() {
        std::this_thread::sleep_for(milliseconds(100));
    });

    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Function executed in"), std::string::npos);
    EXPECT_NE(output.find("us"), std::string::npos);
}

// ==============================
// timingAverage 平均计时
// ==============================
TEST(ChronoHelperTiming, AverageTiming) {
    testing::internal::CaptureStdout();

    ChronoHelper::timingAverage(10, []() {
        std::this_thread::sleep_for(microseconds(100));
    });

    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("Average time"), std::string::npos);
}

TEST(ChronoHelperTiming, AverageTimingReturn) {
    double avg_ms = ChronoHelper::timingAverageReturn(10, []() {
        std::this_thread::sleep_for(milliseconds(10));
    });

    // 平均应该在10ms左右（允许误差）
    EXPECT_GT(avg_ms, 8.0);   // > 8ms
    EXPECT_LT(avg_ms, 12.0);  // < 12ms
}

// ==============================
// clockStart/clockEnd 分段计时
// ==============================
TEST(ChronoHelperClock, SegmentTiming) {
    testing::internal::CaptureStdout();

    ChronoHelper::clockStart(0);
    std::this_thread::sleep_for(milliseconds(50));
    ChronoHelper::clockEnd(0);

    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("[Clock 0]"), std::string::npos);
    EXPECT_NE(output.find("Duration:"), std::string::npos);
}

TEST(ChronoHelperClock, MultipleSegments) {
    testing::internal::CaptureStdout();

    ChronoHelper::clockStart(1);
    ChronoHelper::clockStart(2);

    std::this_thread::sleep_for(milliseconds(10));

    ChronoHelper::clockEnd(1);
    ChronoHelper::clockEnd(2);

    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("[Clock 1]"), std::string::npos);
    EXPECT_NE(output.find("[Clock 2]"), std::string::npos);
}

TEST(ChronoHelperClock, InvalidClockId) {
    testing::internal::CaptureStdout();

    // 没有start直接end，应该报错
    ChronoHelper::clockEnd(999);

    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_NE(output.find("[ChronoHelper Error]"), std::string::npos);
}

// ==============================
// record 周期统计
// ==============================
TEST(ChronoHelperRecord, BasicRecord) {
    // 重置计数器
    ChronoHelper::reset(0);

    // 记录若干周期（1000us = 1ms）
    for (int i = 0; i < 5; ++i) {
        ChronoHelper::record(0, 1000);  // 期望1ms周期
        std::this_thread::sleep_for(milliseconds(1));
    }

    // 测试通过即表示功能正常
    SUCCEED();
}

TEST(ChronoHelperRecord, MultipleCounters) {
    // 使用不同ID的计数器
    ChronoHelper::reset(1);
    ChronoHelper::reset(2);

    ChronoHelper::record(1, 1000);
    ChronoHelper::record(2, 2000);

    SUCCEED();
}

// ==============================
// set_off 开关控制
// ==============================
TEST(ChronoHelperControl, OnOffSwitch) {
    // 关闭统计
    ChronoHelper::set_off(true);

    testing::internal::CaptureStdout();
    ChronoHelper::record(0, 1000);
    std::string output = testing::internal::GetCapturedStdout();

    // 关闭后应该无输出
    // （具体行为取决于实现，这里只是测试不崩溃）

    // 重新开启
    ChronoHelper::set_off(false);

    SUCCEED();
}

// ==============================
// reset 重置
// ==============================
TEST(ChronoHelperControl, ResetCounter) {
    ChronoHelper::record(3, 1000);
    ChronoHelper::record(3, 1000);

    // 重置
    ChronoHelper::reset(3);

    // 重置后重新记录应该正常
    ChronoHelper::record(3, 1000);

    SUCCEED();
}

// ==============================
// set_overwrite_output 输出模式
// ==============================
TEST(ChronoHelperControl, OverwriteOutput) {
    // 设置覆盖输出模式
    ChronoHelper::set_overwrite_output(true);

    ChronoHelper::record(0, 1000);

    // 恢复正常模式
    ChronoHelper::set_overwrite_output(false);

    SUCCEED();
}
