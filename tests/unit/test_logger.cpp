/**
 * @file test_logger.cpp
 * @brief Logger单元测试
 */

#include <gtest/gtest.h>
#include <sstream>
#include <cstring>
#include <vector>
#include <string>

#include "MB_DDF/Debug/Logger.h"

using namespace MB_DDF;
using namespace MB_DDF::Debug;

// 辅助类：收集日志输出的回调
class LogCollector {
public:
    std::vector<std::pair<LogLevel, std::string>> messages;

    void clear() { messages.clear(); }

    Logger::OutputCallback get_callback() {
        return [this](LogLevel level, const std::string& msg) {
            messages.emplace_back(level, msg);
        };
    }

    bool contains(const std::string& text) const {
        for (const auto& [level, msg] : messages) {
            if (msg.find(text) != std::string::npos) return true;
        }
        return false;
    }

    size_t count() const { return messages.size(); }
};

// 测试固件：每个测试后清理回调
class LoggerTest : public ::testing::Test {
protected:
    void TearDown() override {
        // 清理所有回调，避免累积导致内存问题
        Logger::instance().clear_callbacks();
        // 重置日志级别为默认值
        Logger::instance().set_level(LogLevel::INFO);
    }
};

// ==============================
// 日志级别设置与过滤
// ==============================
TEST_F(LoggerTest, SetLevelAndFilter) {
    LogCollector collector;
    Logger::instance().add_callback(collector.get_callback());

    // 设置INFO级别
    Logger::instance().set_level(LogLevel::INFO);

    // INFO及以上应该输出
    LOG_INFO << "info message";
    EXPECT_TRUE(collector.contains("info message"));

    collector.clear();

    // DEBUG应该被过滤
    LOG_DEBUG << "debug message";
    EXPECT_FALSE(collector.contains("debug message"));

    // TRACE也应该被过滤
    LOG_TRACE << "trace message";
    EXPECT_FALSE(collector.contains("trace message"));
}

TEST_F(LoggerTest, AllLevelsOutputWhenSetToTrace) {
    LogCollector collector;
    Logger::instance().add_callback(collector.get_callback());

    // 设置TRACE级别（最低级别，所有日志都输出）
    Logger::instance().set_level(LogLevel::TRACE);

    LOG_TRACE << "trace test";
    LOG_DEBUG << "debug test";
    LOG_INFO << "info test";
    LOG_WARN << "warn test";
    LOG_ERROR << "error test";

    EXPECT_TRUE(collector.contains("trace test"));
    EXPECT_TRUE(collector.contains("debug test"));
    EXPECT_TRUE(collector.contains("info test"));
    EXPECT_TRUE(collector.contains("warn test"));
    EXPECT_TRUE(collector.contains("error test"));
}

TEST_F(LoggerTest, OnlyFatalWhenSetToFatal) {
    LogCollector collector;
    Logger::instance().add_callback(collector.get_callback());

    // 设置FATAL级别
    Logger::instance().set_level(LogLevel::FATAL);

    LOG_ERROR << "error test";
    LOG_FATAL << "fatal test";

    EXPECT_FALSE(collector.contains("error test"));
    EXPECT_TRUE(collector.contains("fatal test"));
}

// ==============================
// 日志输出格式
// ==============================
TEST_F(LoggerTest, MessageContainsLevelTag) {
    LogCollector collector;
    Logger::instance().add_callback(collector.get_callback());

    Logger::instance().set_level(LogLevel::INFO);

    LOG_INFO << "level tag test";

    // 验证包含INFO标签
    EXPECT_TRUE(collector.contains("INFO"));
}

TEST_F(LoggerTest, TimestampEnabled) {
    LogCollector collector;
    Logger::instance().add_callback(collector.get_callback());

    Logger::instance().set_timestamp_enabled(true);
    Logger::instance().set_level(LogLevel::INFO);

    LOG_INFO << "timestamp test";

    // 验证包含时间戳（应该包含数字）
    ASSERT_GT(collector.count(), 0);
    bool has_digit = false;
    for (const auto& [level, msg] : collector.messages) {
        for (char c : msg) {
            if (c >= '0' && c <= '9') {
                has_digit = true;
                break;
            }
        }
    }
    EXPECT_TRUE(has_digit);
}

// ==============================
// 多参数输出
// ==============================
TEST_F(LoggerTest, MultipleArgsOutput) {
    LogCollector collector;
    Logger::instance().add_callback(collector.get_callback());

    Logger::instance().set_level(LogLevel::INFO);

    int x = 42;
    std::string name = "test";
    LOG_INFO << "values: x=" << x << ", name=" << name;

    EXPECT_TRUE(collector.contains("x=42"));
    EXPECT_TRUE(collector.contains("name=test"));
}

// ==============================
// 日志单例
// ==============================
TEST_F(LoggerTest, Singleton) {
    Logger& log1 = Logger::instance();
    Logger& log2 = Logger::instance();

    EXPECT_EQ(&log1, &log2);
}

// ==============================
// 配置宏测试
// ==============================
TEST_F(LoggerTest, ConfigMacros) {
    LogCollector collector;
    Logger::instance().add_callback(collector.get_callback());

    // 测试设置级别的宏
    LOG_SET_LEVEL_DEBUG();

    LOG_DEBUG << "after set debug";
    EXPECT_TRUE(collector.contains("after set debug"));

    LOG_SET_LEVEL_WARN();

    LOG_INFO << "after set warn";
    EXPECT_FALSE(collector.contains("after set warn"));
}

TEST_F(LoggerTest, TimestampMacros) {
    // 这些宏只是设置，不直接产生输出，主要测试不崩溃
    LOG_ENABLE_TIMESTAMP();
    LOG_DISABLE_TIMESTAMP();
    LOG_ENABLE_TIMESTAMP();

    // 如果能执行到这里，说明宏工作正常
    SUCCEED();
}

// ==============================
// 空消息不输出
// ==============================
TEST_F(LoggerTest, EmptyMessageNotOutput) {
    LogCollector collector;
    Logger::instance().add_callback(collector.get_callback());

    Logger::instance().set_level(LogLevel::INFO);

    // 记录一条消息确认回调工作
    LOG_INFO << "before";
    EXPECT_EQ(collector.count(), 1);

    // 创建LogStream但不输出内容
    {
        auto stream = LOG_INFO;
        // 不写入任何内容
    }

    // 空消息应该不触发输出，消息数应保持不变
    EXPECT_EQ(collector.count(), 1);
}

// ==============================
// 清除回调功能测试
// ==============================
TEST_F(LoggerTest, ClearCallbacks) {
    LogCollector collector1;
    LogCollector collector2;

    Logger::instance().add_callback(collector1.get_callback());
    Logger::instance().add_callback(collector2.get_callback());

    LOG_INFO << "test message";
    EXPECT_EQ(collector1.count(), 1);
    EXPECT_EQ(collector2.count(), 1);

    // 清除所有回调
    Logger::instance().clear_callbacks();

    LOG_INFO << "after clear";
    // 回调已被清除，消息数不应增加
    EXPECT_EQ(collector1.count(), 1);
    EXPECT_EQ(collector2.count(), 1);
}

