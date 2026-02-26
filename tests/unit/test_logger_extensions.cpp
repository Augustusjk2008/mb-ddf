/**
 * @file test_logger_extensions.cpp
 * @brief LoggerExtensions 单元测试
 *
 * 测试日志扩展功能，包括：
 * - display_width: UTF-8字符串显示宽度计算
 * - LoggerExtensions: 各种格式化打印方法
 * - 宏定义: LOG_BLANK_LINE, LOG_SEPARATOR, LOG_TITLE, LOG_PROGRESS, LOG_BOX, LOG_TIMESTAMP
 */

#include <gtest/gtest.h>
#include <string>

#include "MB_DDF/Debug/LoggerExtensions.h"
#include "MB_DDF/Debug/Logger.h"

using namespace MB_DDF;
using namespace MB_DDF::Debug;
using namespace MB_DDF::Debug::Extensions;

// 测试固件：每个测试后清理回调
class LoggerExtensionsTest : public ::testing::Test {
protected:
    void TearDown() override {
        Logger::instance().clear_callbacks();
        Logger::instance().set_level(LogLevel::INFO);
    }
};

// ==============================
// display_width 测试
// ==============================
TEST_F(LoggerExtensionsTest, DisplayWidthASCII) {
    EXPECT_EQ(display_width("hello"), 5);
    EXPECT_EQ(display_width(""), 0);
    EXPECT_EQ(display_width("a"), 1);
}

TEST_F(LoggerExtensionsTest, DisplayWidthCJK) {
    // CJK字符宽度为2
    EXPECT_EQ(display_width("你好"), 4);      // 2个字符 x 2宽度
    EXPECT_EQ(display_width("中文字符"), 8);   // 4个字符 x 2宽度
}

TEST_F(LoggerExtensionsTest, DisplayWidthMixed) {
    // 混合ASCII和CJK
    EXPECT_EQ(display_width("Hello世界"), 9);  // 5 + 2*2 = 9
    EXPECT_EQ(display_width("测试A"), 5);      // 2*2 + 1 = 5
}

TEST_F(LoggerExtensionsTest, DisplayWidthSpecial) {
    // 特殊UTF-8字符
    EXPECT_EQ(display_width("🎉"), 2);  // Emoji通常宽度为2
    EXPECT_EQ(display_width("こんにちは"), 10);  // 日语假名
}

TEST_F(LoggerExtensionsTest, DisplayWidthInvalidUTF8) {
    // 测试无效UTF-8处理（不应该崩溃）
    std::string invalid;
    invalid.push_back(0xC0);  // 无效起始字节
    invalid.push_back(0x80);

    int width = display_width(invalid);
    EXPECT_GE(width, 0);  // 应该返回非负值，不崩溃
}

// 辅助函数：捕获日志输出
class LogCapture {
public:
    std::vector<std::string> messages;

    void start() {
        messages.clear();
        Logger::instance().add_callback([this](LogLevel, const std::string& msg) {
            messages.push_back(msg);
        });
    }

    void clear() {
        messages.clear();
    }

    bool contains(const std::string& substr) const {
        for (const auto& msg : messages) {
            if (msg.find(substr) != std::string::npos) {
                return true;
            }
        }
        return false;
    }

    size_t count_lines() const {
        return messages.size();
    }
};

// ==============================
// LoggerExtensions 打印方法测试
// ==============================
TEST_F(LoggerExtensionsTest, PrintBlankLines) {
    LogCapture capture;
    capture.start();

    // 测试打印1个空行
    LoggerExtensions::print_blank_lines(LogLevel::INFO, 1);
    EXPECT_GE(capture.count_lines(), 1u);

    capture.clear();

    // 测试打印3个空行
    LoggerExtensions::print_blank_lines(LogLevel::INFO, 3);
    EXPECT_GE(capture.count_lines(), 3u);

    capture.clear();

    // 零值应该不打印
    LoggerExtensions::print_blank_lines(LogLevel::INFO, 0);
    // 可能为0或保持之前的状态
}

TEST_F(LoggerExtensionsTest, PrintSeparator) {
    LogCapture capture;
    capture.start();

    // 测试分隔符打印
    LoggerExtensions::print_separator(LogLevel::INFO, '-', 40);
    EXPECT_TRUE(capture.contains("----------------------------------------"));

    capture.clear();

    LoggerExtensions::print_separator(LogLevel::INFO, '=', 10);
    EXPECT_TRUE(capture.contains("=========="));

    capture.clear();

    // 边界值：0长度应该没有输出或输出很短
    LoggerExtensions::print_separator(LogLevel::INFO, '*', 0);
    // 不验证具体内容，只要不崩溃即可
}

TEST_F(LoggerExtensionsTest, PrintTitle) {
    LogCapture capture;
    capture.start();

    // 测试标题打印
    LoggerExtensions::print_title("Test Title", LogLevel::INFO, '=', 40);

    // 验证标题内容被输出
    EXPECT_TRUE(capture.contains("Test Title"));

    capture.clear();

    // CJK标题
    LoggerExtensions::print_title("中文标题", LogLevel::INFO, '-', 40);
    EXPECT_TRUE(capture.contains("中文标题"));

    capture.clear();

    // 窄宽度
    LoggerExtensions::print_title("X", LogLevel::INFO, '*', 10);
    EXPECT_TRUE(capture.contains("X"));
}

TEST_F(LoggerExtensionsTest, PrintTitleWithCJK) {
    // CJK标题居中测试
    LoggerExtensions::print_title("系统启动", LogLevel::INFO, '=', 40);
    LoggerExtensions::print_title("测试完成", LogLevel::INFO, '-', 30);
}

TEST_F(LoggerExtensionsTest, PrintProgress) {
    LogCapture capture;
    capture.start();

    // 测试进度打印
    LoggerExtensions::print_progress("Loading", 0, LogLevel::INFO, 20);
    EXPECT_TRUE(capture.contains("Loading"));
    EXPECT_TRUE(capture.contains("0%"));

    capture.clear();

    LoggerExtensions::print_progress("Loading", 50, LogLevel::INFO, 20);
    EXPECT_TRUE(capture.contains("50%"));

    capture.clear();

    LoggerExtensions::print_progress("Loading", 100, LogLevel::INFO, 20);
    EXPECT_TRUE(capture.contains("100%"));
}

TEST_F(LoggerExtensionsTest, PrintProgressBoundary) {
    // 边界值测试
    LoggerExtensions::print_progress("Task", -10, LogLevel::INFO);  // 应该被修正为0
    LoggerExtensions::print_progress("Task", 150, LogLevel::INFO);  // 应该被修正为100
    LoggerExtensions::print_progress("Task", 0, LogLevel::INFO, 10);
    LoggerExtensions::print_progress("Task", 100, LogLevel::INFO, 100);
}

TEST_F(LoggerExtensionsTest, PrintBoxedText) {
    LogCapture capture;
    capture.start();

    // 测试框架文本
    LoggerExtensions::print_boxed_text("Hello", LogLevel::INFO, '*');
    EXPECT_TRUE(capture.contains("Hello"));
    // 验证有边框字符
    EXPECT_TRUE(capture.contains("*"));

    capture.clear();

    LoggerExtensions::print_boxed_text("World", LogLevel::INFO, '#');
    EXPECT_TRUE(capture.contains("World"));
    EXPECT_TRUE(capture.contains("#"));
}

TEST_F(LoggerExtensionsTest, PrintTimestampMarker) {
    LogCapture capture;
    capture.start();

    // 测试时间戳标记
    LoggerExtensions::print_timestamp_marker("Start processing", LogLevel::INFO);
    EXPECT_TRUE(capture.contains("Start processing"));

    capture.clear();

    LoggerExtensions::print_timestamp_marker("Complete", LogLevel::INFO);
    EXPECT_TRUE(capture.contains("Complete"));
}

TEST_F(LoggerExtensionsTest, PrintDoubleSeparator) {
    LoggerExtensions::print_double_separator(LogLevel::INFO, 40);
    LoggerExtensions::print_double_separator(LogLevel::WARN, 80);
}

TEST_F(LoggerExtensionsTest, PrintDottedSeparator) {
    LoggerExtensions::print_dotted_separator(LogLevel::INFO, 40);
    LoggerExtensions::print_dotted_separator(LogLevel::DEBUG, 60);
}

// ==============================
// 宏定义测试
// ==============================
TEST_F(LoggerExtensionsTest, MacroLogBlankLine) {
    LOG_BLANK_LINE();
    LOG_BLANK_LINES(3);
    LOG_BLANK_LINE_DEBUG();
}

TEST_F(LoggerExtensionsTest, MacroLogSeparator) {
    LOG_SEPARATOR();
    LOG_SEPARATOR_CUSTOM('=', 40);
    LOG_DOUBLE_SEPARATOR();
    LOG_DOUBLE_SEPARATOR_CUSTOM(60);
    LOG_DOTTED_SEPARATOR();
    LOG_DOTTED_SEPARATOR_CUSTOM(50);
}

TEST_F(LoggerExtensionsTest, MacroLogTitle) {
    LOG_TITLE("Main Section");
    LOG_TITLE_CUSTOM("Sub Section", '-', 40);
    LOG_TITLE_WARN("Warning Section");
}

TEST_F(LoggerExtensionsTest, MacroLogProgress) {
    LOG_PROGRESS("Initializing", 0);
    LOG_PROGRESS("Processing", 50);
    LOG_PROGRESS("Complete", 100);
    LOG_PROGRESS_CUSTOM("Custom Width", 75, 30);
}

TEST_F(LoggerExtensionsTest, MacroLogBox) {
    LOG_BOX("Important Message");
    LOG_BOX_CUSTOM("Custom Box", '#');
    LOG_BOX_ERROR("Error Message");
}

TEST_F(LoggerExtensionsTest, MacroLogTimestamp) {
    LOG_TIMESTAMP();
    LOG_TIMESTAMP_MSG("Event occurred");
}

// ==============================
// 不同日志级别测试
// ==============================
TEST_F(LoggerExtensionsTest, DifferentLogLevels) {
    // 测试不同日志级别下的打印
    LoggerExtensions::print_title("Debug Title", LogLevel::DEBUG);
    LoggerExtensions::print_title("Info Title", LogLevel::INFO);
    LoggerExtensions::print_title("Warn Title", LogLevel::WARN);
    LoggerExtensions::print_title("Error Title", LogLevel::ERROR);

    LoggerExtensions::print_separator(LogLevel::DEBUG, '-', 30);
    LoggerExtensions::print_separator(LogLevel::ERROR, '!', 30);
}

// ==============================
// 组合使用测试
// ==============================
TEST_F(LoggerExtensionsTest, CombinedUsage) {
    // 模拟一个典型的日志输出场景
    LOG_DOUBLE_SEPARATOR();
    LOG_TITLE("Application Start");
    LOG_DOUBLE_SEPARATOR();

    LOG_BLANK_LINE();

    LOG_PROGRESS("Loading modules", 0);
    LOG_PROGRESS("Loading modules", 50);
    LOG_PROGRESS("Loading modules", 100);

    LOG_BLANK_LINE();

    LOG_BOX("System Ready");

    LOG_BLANK_LINE();

    LOG_TIMESTAMP_MSG("Initialization complete");

    LOG_BLANK_LINE();

    LOG_SEPARATOR();
    LOG_TITLE("Processing Data");
    LOG_SEPARATOR();

    LOG_BLANK_LINE();

    LOG_DOTTED_SEPARATOR();
}
