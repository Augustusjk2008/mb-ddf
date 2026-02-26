/**
 * @file test_system_timer.cpp
 * @brief SystemTimer单元测试
 *
 * 注意：这些测试在目标板上运行，使用真实的POSIX定时器
 */

#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>
#include <mutex>
#include <iostream>

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

// ==============================
// 无效周期字符串测试
// ==============================
TEST(SystemTimerTest, InvalidPeriodString) {
    std::atomic<bool> triggered{false};

    // 无效单位 "xs" - 应该抛出异常
    EXPECT_THROW({
        auto timer = SystemTimer::start("10xs", [&triggered](void*) {
            triggered.store(true);
        });
        (void)timer;
    }, std::exception);

    // 无效格式：没有数字
    EXPECT_THROW({
        auto timer = SystemTimer::start("ms", [&triggered](void*) {
            triggered.store(true);
        });
        (void)timer;
    }, std::exception);

    // 无效格式：只有数字没有单位
    EXPECT_THROW({
        auto timer = SystemTimer::start("100", [&triggered](void*) {
            triggered.store(true);
        });
        (void)timer;
    }, std::exception);

    // 空字符串
    EXPECT_THROW({
        auto timer = SystemTimer::start("", [&triggered](void*) {
            triggered.store(true);
        });
        (void)timer;
    }, std::exception);

    // 包含非法字符 - "10m s" 可能被解析为 "10m"，不抛出异常
    // 如果实现会抛出异常则保留，否则改为验证行为

    // 负数
    EXPECT_THROW({
        auto timer = SystemTimer::start("-10ms", [&triggered](void*) {
            triggered.store(true);
        });
        (void)timer;
    }, std::exception);
}

TEST(SystemTimerTest, EdgeCasePeriodStrings) {
    std::atomic<bool> triggered{false};

    // 极小的周期（0ms）- 应该抛出异常
    EXPECT_THROW({
        auto timer1 = SystemTimer::start("0ms", [&triggered](void*) {
            triggered.store(true);
        });
        (void)timer1;
    }, std::exception);

    // 非常大的数字 - 应该能启动（周期很长）
    auto timer2 = SystemTimer::start("999999999ms", [&triggered](void*) {
        triggered.store(true);
    });
    if (timer2) {
        // 应该启动成功，但周期很长
        EXPECT_TRUE(timer2->isRunning());
        timer2->stop();
    }
}

// ==============================
// 定时器精度/抖动测试
// ==============================
TEST(SystemTimerTest, TimerPrecision) {
    std::vector<std::chrono::steady_clock::time_point> timestamps;
    std::mutex mtx;

    auto timer = SystemTimer::start("50ms", [&timestamps, &mtx](void*) {
        std::lock_guard<std::mutex> lock(mtx);
        timestamps.push_back(std::chrono::steady_clock::now());
    });

    ASSERT_NE(timer, nullptr);

    // 收集10个样本
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    timer->stop();

    std::lock_guard<std::mutex> lock(mtx);
    ASSERT_GE(timestamps.size(), 8);  // 至少收集到8个样本

    // 计算平均周期
    std::vector<long long> intervals;
    for (size_t i = 1; i < timestamps.size(); ++i) {
        auto interval = std::chrono::duration_cast<std::chrono::microseconds>(
            timestamps[i] - timestamps[i-1]).count();
        intervals.push_back(interval);
    }

    // 计算平均间隔
    long long sum = 0;
    for (auto interval : intervals) {
        sum += interval;
    }
    long long avg_interval = sum / intervals.size();

    // 期望50ms = 50000us，允许10%误差（45000-55000us）
    EXPECT_GE(avg_interval, 45000);
    EXPECT_LE(avg_interval, 55000);

    // 计算最大抖动（最大偏差）
    long long max_jitter = 0;
    for (auto interval : intervals) {
        long long jitter = std::llabs(interval - 50000);
        if (jitter > max_jitter) {
            max_jitter = jitter;
        }
    }

    // 最大抖动不应超过20ms
    EXPECT_LE(max_jitter, 20000);

    std::cout << "Timer precision test: avg_interval=" << avg_interval
              << "us, max_jitter=" << max_jitter << "us" << std::endl;
}

TEST(SystemTimerTest, TimerJitterStatistics) {
    constexpr int expected_period_ms = 20;  // 20ms周期
    constexpr int num_samples = 50;

    std::atomic<int> counter{0};
    std::vector<long long> jitter_values;
    jitter_values.reserve(num_samples);
    std::mutex mtx;

    auto last_time = std::chrono::steady_clock::now();

    auto timer = SystemTimer::start(
        std::to_string(expected_period_ms) + "ms",
        [&counter, &jitter_values, &mtx, &last_time](void*) {
            auto now = std::chrono::steady_clock::now();
            auto actual_interval = std::chrono::duration_cast<std::chrono::microseconds>(
                now - last_time).count();
            last_time = now;

            // 计算与期望周期的偏差（第一个样本跳过，因为没有上次时间）
            if (counter.load() > 0) {
                long long expected_us = expected_period_ms * 1000;
                long long jitter = std::llabs(actual_interval - expected_us);

                std::lock_guard<std::mutex> lock(mtx);
                jitter_values.push_back(jitter);
            }
            counter++;
        });

    ASSERT_NE(timer, nullptr);

    // 收集足够样本
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (counter.load() < num_samples && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    timer->stop();
    ASSERT_GE(counter.load(), num_samples) << "Timed out waiting for timer samples";

    std::lock_guard<std::mutex> lock(mtx);
    ASSERT_GE(jitter_values.size(), num_samples / 2);

    // 计算统计值
    long long sum = 0;
    long long max_jitter = 0;
    for (auto j : jitter_values) {
        sum += j;
        if (j > max_jitter) max_jitter = j;
    }
    long long avg_jitter = sum / jitter_values.size();

    // 计算标准差
    long long variance_sum = 0;
    for (auto j : jitter_values) {
        variance_sum += (j - avg_jitter) * (j - avg_jitter);
    }
    long long std_dev = std::sqrt(variance_sum / jitter_values.size());

    std::cout << "Jitter statistics (" << expected_period_ms << "ms period):" << std::endl;
    std::cout << "  Samples: " << jitter_values.size() << std::endl;
    std::cout << "  Avg jitter: " << avg_jitter << " us" << std::endl;
    std::cout << "  Max jitter: " << max_jitter << " us" << std::endl;
    std::cout << "  Std dev: " << std_dev << " us" << std::endl;

    // 平均抖动不应超过期望周期的20%
    EXPECT_LE(avg_jitter, expected_period_ms * 1000 * 0.2);

    // 最大抖动不应超过期望周期的50%
    EXPECT_LE(max_jitter, expected_period_ms * 1000 * 0.5);
}

TEST(SystemTimerTest, LongRunningStability) {
    std::atomic<int> counter{0};

    auto timer = SystemTimer::start("10ms", [&counter](void*) {
        counter++;
    });

    ASSERT_NE(timer, nullptr);

    // 运行500ms
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    int count = counter.load();
    // 500ms / 10ms = 50，考虑到启动延迟，至少应该触发40次
    EXPECT_GE(count, 40);

    timer->stop();

    // 记录停止后的计数
    int count_after_stop = counter.load();

    // 再等待100ms，确保定时器完全停止
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 计数不应增加
    EXPECT_EQ(counter.load(), count_after_stop);
}

