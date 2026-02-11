#include "monitor_ui_logger.hpp"

#include "Logger.h"
#include "monitor_ui.hpp"

#include <atomic>
#include <string>

namespace {

std::string strip_ansi_codes(const std::string& input) {
    std::string out;
    out.reserve(input.size());

    enum class State { Normal, Esc, Csi };
    State state = State::Normal;

    for (char c : input) {
        switch (state) {
        case State::Normal:
            if (c == '\x1b') {
                state = State::Esc;
            } else {
                out.push_back(c);
            }
            break;
        case State::Esc:
            state = (c == '[') ? State::Csi : State::Normal;
            break;
        case State::Csi:
            if (c >= '@' && c <= '~') {
                state = State::Normal;
            }
            break;
        }
    }

    return out;
}

std::atomic<bool> g_logger_attached{false};

} // namespace

namespace MB_DDF::MonitorUI {

void attach_logger(const LoggerBridgeOptions& options) {
    bool expected = false;
    if (!g_logger_attached.compare_exchange_strong(expected, true)) {
        return;
    }

    auto& logger = MB_DDF::Debug::Logger::instance();
    logger.set_color_enabled(true);
    logger.set_timestamp_enabled(false);
    logger.set_function_line_enabled(false);

    if (options.disable_console) {
        logger.set_console_enabled(false);
    }

    logger.add_callback(
        [strip = options.strip_ansi](MB_DDF::Debug::LogLevel, const std::string& msg) {
            if (strip) {
                log_line(strip_ansi_codes(msg));
            } else {
                log_line(msg);
            }
        });
}

} // namespace MonitorUI
