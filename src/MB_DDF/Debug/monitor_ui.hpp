#pragma once

#include <chrono>
#include <functional>
#include <string>

namespace MB_DDF::MonitorUI {

void run_monitor_table(bool blocking = false, std::function<void()> update_fn = {},
                       std::chrono::milliseconds interval = std::chrono::seconds(1));

void run_monitor_table(std::function<void()> update_fn,
                       std::chrono::milliseconds interval = std::chrono::seconds(1));

void log_line(const std::string& line);

} // namespace MonitorUI
