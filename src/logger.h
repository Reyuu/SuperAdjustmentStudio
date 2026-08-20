#ifndef SAS_LOGGER_H
#define SAS_LOGGER_H

#include <string>
#include "plugin.h"
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"

inline std::shared_ptr<spdlog::logger> define_logger() {
    spdlog::set_level(spdlog::level::debug);
    // we kinda need the live log to diagnose the issues in the future
    spdlog::flush_on(spdlog::level::debug);
    spdlog::enable_backtrace(32);
    std::string name = std::string(PLUGIN_NAME);
    if (std::shared_ptr<spdlog::logger> existing = spdlog::get(name)) {
        return existing;
    }
    return spdlog::basic_logger_mt(name, name + ".log", true);
}

inline std::shared_ptr<spdlog::logger> Logger = define_logger();

#endif // SAS_LOGGER_H
