/**
 * @file test_system_timer.cpp
 * @brief SystemTimer单元测试
 *
 * 注意：这些测试在目标板上运行，使用真实的POSIX定时器
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

#include "MB_DDF/Timer/SystemTimer.h"

using namespace MB_DDF::Timer;

// ==============================
// 基本启动与停止
// ==============================
TEST(SystemTimerTest, StartAndStop) {
    std::atomic<int> counter{0};

    auto timer = SystemTimer::start("10ms", [&counter](void*) {
        counter++;
    });

    ASSERT_NE(timer, nullptr);
    EXPECT_TRUE(timer->isRunning());

    // 等待一段时间让定时器触发几次
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 验证已经触发（至少5次，留有余量）
    EXPECT_GE(counter.load(), 5);

    // 停止定时器
    timer->stop();
    EXPECT_FALSE(timer->isRunning());

    // 保存当前计数
    int count_after_stop = counter.load();

    // 再等待一段时间，计数不应增加
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(counter.load(), count_after_stop);
}

TEST(SystemTimerTest, DifferentPeriods) {
    std::atomic<int> fast_counter{0};
    std::atomic<int> slow_counter{0};

    // 快速定时器 (5ms)
    auto fast_timer = SystemTimer::start("5ms", [&fast_counter](void*) {
        fast_counter++;
    });

    // 慢速定时器 (20ms)
    auto slow_timer = SystemTimer::start("20ms", [&slow_counter](void*) {
        slow_counter++;
    });

    ASSERT_NE(fast_timer, nullptr);
    ASSERT_NE(slow_timer, nullptr);

    // 等待100ms
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 快速定时器应该触发更多次（至少3倍）
    EXPECT_GE(fast_counter.load(), slow_counter.load() * 3);

    fast_timer->stop();
    slow_timer->stop();
}

// ==============================
// 周期字符串解析
// ==============================
TEST(SystemTimerTest, PeriodParsing) {
    std::atomic<int> counter{0};

    // 测试不同单位的周期字符串
    auto timer1 = SystemTimer::start("100ms", [&counter](void*) { counter++; });
    ASSERT_NE(timer1, nullptr);
    timer1->stop();

    auto timer2 = SystemTimer::start("1s", [&counter](void*) { counter++; });
    ASSERT_NE(timer2, nullptr);
    timer2->stop();

    auto timer3 = SystemTimer::start("500us", [&counter](void*) { counter++; });
    ASSERT_NE(timer3, nullptr);
    timer3->stop();
}

// ==============================
// 重置功能
// ==============================
TEST(SystemTimerTest, ResetTimer) {
    std::atomic<int> counter{0};

    auto timer = SystemTimer::start("50ms", [&counter](void*) {
        counter++;
    });

    ASSERT_NE(timer, nullptr);

    // 等待一部分时间
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    int count_before = counter.load();
    EXPECT_GE(count_before, 1);

    // 重置定时器
    timer->reset();

    // 再等待一段时间
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    int count_after = counter.load();
    EXPECT_GT(count_after, count_before);

    timer->stop();
}

// ==============================
// 线程句柄获取
// ==============================
TEST(SystemTimerTest, WorkerHandle) {
    auto timer = SystemTimer::start("10ms", [](void*) {}, SystemTimerOptions{});
    ASSERT_NE(timer, nullptr);

    auto handle = timer->workerHandle();
    EXPECT_TRUE(handle.has_value());

    timer->stop();
}

// ==============================
// 自定义选项
// ==============================
TEST(SystemTimerTest, CustomOptions) {
    std::atomic<bool> triggered{false};

    SystemTimerOptions opts;
    opts.priority = 1;  // 较低优先级
    opts.cpu = -1;      // 不绑核

    auto timer = SystemTimer::start("10ms", [&triggered](void*) {
        triggered.store(true);
    }, opts);

    ASSERT_NE(timer, nullptr);
    EXPECT_TRUE(timer->isRunning());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(triggered.load());

    timer->stop();
}

// ==============================
// 用户数据传递
// ==============================
TEST(SystemTimerTest, UserData) {
    int test_value = 42;
    std::atomic<int*> received_ptr{nullptr};

    SystemTimerOptions opts;
    opts.user_data = &test_value;

    auto timer = SystemTimer::start("10ms", [&received_ptr](void* data) {
        received_ptr.store(static_cast<int*>(data));
    }, opts);

    ASSERT_NE(timer, nullptr);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    EXPECT_EQ(received_ptr.load(), &test_value);
    EXPECT_EQ(*received_ptr.load(), 42);

    timer->stop();
}

// ==============================
// 多次停止安全
// ==============================
TEST(SystemTimerTest, MultipleStopSafe) {
    auto timer = SystemTimer::start("10ms", [](void*) {});
    ASSERT_NE(timer, nullptr);

    // 多次调用stop应该安全
    timer->stop();
    EXPECT_FALSE(timer->isRunning());

    timer->stop();  // 再次调用不应崩溃
    EXPECT_FALSE(timer->isRunning());
}

// ==============================
// 回调执行时间测试
// ==============================
TEST(SystemTimerTest, CallbackTiming) {
    std::atomic<int> counter{0};

    auto timer = SystemTimer::start("20ms", [&counter](void*) {
        counter++;
    });

    ASSERT_NE(timer, nullptr);

    // 等待100ms
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    int count = counter.load();
    // 100ms / 20ms = 5，考虑到调度延迟，至少应该触发3次
    EXPECT_GE(count, 3);
    EXPECT_LE(count, 10);  // 也不应该触发太多次

    timer->stop();
}

