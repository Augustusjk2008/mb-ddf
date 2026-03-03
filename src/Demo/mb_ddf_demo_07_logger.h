#pragma once

#include "MB_DDF/Debug/Logger.h"
#include "MB_DDF/PhysicalLayer/Support/Log.h"
#include "MB_DDF/Debug/monitor.hpp"
#include "MB_DDF/Debug/monitor_ui.hpp"
#include "MB_DDF/Debug/monitor_ui_logger.hpp"
#include "Demo/fk_to_datalink_protocol.h"

#include <memory>
#include <mutex>
#include <string>

namespace Demo {
namespace MB_DDF_Demos {

inline void RunDemo07_LoggerUsage() {
    // Demo07：Logger 各种用法（级别、开关、文件输出、回调、条件日志）
    // 目标：
    // - 展示 Logger 的常用配置项：日志级别、时间戳、颜色、函数名/行号
    // - 展示文件输出：set_file_output
    // - 展示回调：add_callback（可用于把日志接到 UI/网络/自定义 sink）
    // - 展示基本日志宏：LOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL
    //
    // 说明：
    // - Logger 的配置也有对应的宏封装（详见 Logger.h 的 LoggerConfigMacros）
    // - 本 demo 同时演示了 PhysicalLayer 的 C 风格日志宏（不在 man.md 中说明）
    using MB_DDF::Debug::LogLevel;

    // 获取 Logger 单例并进行配置
    auto& logger = MB_DDF::Debug::Logger::instance();
    logger.set_level(LogLevel::TRACE);
    logger.set_timestamp_enabled(true);
    logger.set_color_enabled(true);
    logger.set_function_line_enabled(true);
    logger.set_file_output("demo.log");

    // 注册一个自定义回调：把最后一条格式化日志保存到 last_log（示例用途）
    struct CallbackState {
        std::mutex mtx;
        std::string last_log;
    };
    auto state = std::make_shared<CallbackState>();
    logger.add_callback([state](LogLevel, const std::string& formatted) {
        std::lock_guard<std::mutex> lk(state->mtx);
        state->last_log = formatted;
    });

    // 输出不同级别日志（TRACE 最详细，FATAL 最严重）
    LOG_TRACE << "trace message";
    LOG_DEBUG << "debug message";
    LOG_INFO << "info message";
    LOG_WARN << "warn message";
    LOG_ERROR << "error message";
    LOG_FATAL << "fatal message (demo)";

    // 条件日志：仅当条件满足时输出（这里用 if 直观表达）
    if (2 + 2 == 4) {
        LOG_DEBUG << "conditional log matched";
    }

    // 读取回调里保存的 last_log，并输出长度（演示回调生效）
    size_t last_log_len = 0;
    {
        std::lock_guard<std::mutex> lk(state->mtx);
        last_log_len = state->last_log.size();
    }
    LOG_INFO << "last_log_len=" << last_log_len;

    // C 风格日志（PhysicalLayer::Support::Log.h），仅做演示
    LOGI("demo", "op", 0, "plain_c_style_log %d", 123); // 目前 LOGI 不显示了
    LOGW("demo", "op", 1, "warn_code=%d", 1);
    LOGE("demo", "op", -1, "error_code=%d", -1);

    // 等待用户按键
    std::cout << "Press any key to show ftxui..." << std::endl;
    std::cin.get();

    // 展示 MonitorUI：
    // - 展示 Logger 日志输出到 MonitorUI（通过 LoggerBridge）
    // - 展示 MonitorUI 可以监控变量（通过 MONITOR 宏）
    // - 展示 MonitorUI 可以自定义更新函数（通过 run_monitor_table）
    // - 展示 Schema 感知的结构体监控（通过 MONITOR_SCHEMA 宏）
    MB_DDF::MonitorUI::attach_logger();
    int frame_id = 0;
    float temperature = 36.5f;
    float fft_output[64]{};

    // 创建 Fk_to_datalink 结构体实例和 Schema
    ProtocolModel::Fk_to_datalink fk_data;
    auto fk_schema = ProtocolModel::Fk_to_datalinkProtocol::buildSchema();

    // MB_DDF::MonitorUI::run_monitor_table(update);
    auto update = [&]() {
        static int tick = 0;
        ++tick;
        frame_id = 1;
        temperature += 0.1f;

        for (int i = 0; i < 64; ++i) {
            fft_output[i] = static_cast<float>(i + frame_id);
        }

        // 更新 Fk_to_datalink 结构体字段
        fk_data.serial = static_cast<uint16_t>(tick % 65536);
        fk_data.missile_pos_north = 100.0 + tick * 0.1;
        fk_data.missile_pos_up = 50.0 + tick * 0.05;
        fk_data.missile_pos_east = 200.0 - tick * 0.08;
        fk_data.bitGroup2.engine_ignition = (tick % 10 == 0) ? 1 : 0;
        fk_data.bitGroup2.navigation_valid = 1;
        fk_data.image_roll = static_cast<double>(tick) * 0.006;

        MONITOR(frame_id);
        MONITOR(temperature);
        MONITOR_N(fft_output, 14);
        MONITOR_SCHEMA(fk_data, fk_schema);  // Schema-aware struct monitoring
        LOG_INFO << "Test log";
    };
    MB_DDF::MonitorUI::run_monitor_table(true, update);
}

} // namespace MB_DDF_Demos
} // namespace Demo
