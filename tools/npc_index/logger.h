// Minimal stand-in for SAS's spdlog logger so the PCC parser can be compiled into
// a plain console tool without pulling in the whole plugin logger.
#ifndef SAS_TOOL_LOGGER_H
#define SAS_TOOL_LOGGER_H

#include <cstdio>
#include <string>

namespace ToolLog {
inline bool gVerbose = false;

template <class... Args> inline void info(const char* fmt, Args&&...) {
}
template <class... Args> inline void debug(const char* fmt, Args&&...) {
}
template <class... Args> inline void warn(const char* fmt, Args&&...) {
}
template <class... Args> inline void error(const char* fmt, Args&&...) {
}
inline void error(const std::string& message) {
    std::fprintf(stderr, "error: %s\n", message.c_str());
}
inline void info(const std::string& message) {
    if (gVerbose) {
        std::fprintf(stderr, "info: %s\n", message.c_str());
    }
}
} // namespace ToolLog

struct ToolLogger {
        template <class... Args> void info(const char* fmt, Args&&...) {
        }
        template <class... Args> void debug(const char* fmt, Args&&...) {
        }
        template <class... Args> void warn(const char* fmt, Args&&...) {
        }
        template <class... Args> void error(const char* fmt, Args&&...) {
        }
        void info(const std::string& message) {
        }
        void error(const std::string& message) {
            std::fprintf(stderr, "error: %s\n", message.c_str());
        }
};

extern ToolLogger gToolLogger;
#define Logger (&gToolLogger)

#endif // SAS_TOOL_LOGGER_H
