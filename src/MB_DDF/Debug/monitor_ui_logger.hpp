#pragma once

#include <string>

namespace MB_DDF::MonitorUI {

struct LoggerBridgeOptions {
    bool strip_ansi = false;
    bool disable_console = true;
};

void attach_logger(const LoggerBridgeOptions& options = LoggerBridgeOptions{});

} // namespace MonitorUI
