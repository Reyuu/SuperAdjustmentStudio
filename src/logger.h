#ifndef SAS_LOGGER_H
#define SAS_LOGGER_H

#include <string>
#include <atomic>
#include <thread>
#include <sstream>
#include <exception>
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

inline LONG WINAPI SASUnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
    static std::atomic<LONG> crashDumped{0};
    if (crashDumped.exchange(1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    std::thread watchdog([]() {
        Sleep(3000);
        TerminateProcess(GetCurrentProcess(), 1);
    });
    watchdog.detach();

    try {
        if (Logger) {
            if (info && info->ExceptionRecord) {
                EXCEPTION_RECORD* rec = info->ExceptionRecord;
                std::ostringstream es;
                es << "crash handler: code=0x" << std::hex << rec->ExceptionCode
                   << " addr=" << rec->ExceptionAddress
                   << " nparams=" << std::dec << rec->NumberParameters;
                if (rec->ExceptionCode == 0xC0000005 && rec->NumberParameters >= 2) {
                    es << " access=" << (rec->ExceptionInformation[0] ? "write" : "read")
                       << " at=0x" << std::hex << rec->ExceptionInformation[1];
                }
                Logger->error(es.str());
                if (info->ContextRecord) {
                    std::ostringstream cs;
                    HMODULE mod = nullptr;
                    char modName[MAX_PATH] = {};
                    DWORD ripOffset = 0;
                    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           reinterpret_cast<LPCSTR>(static_cast<DWORD_PTR>(info->ContextRecord->Rip)), &mod)) {
                        ripOffset = static_cast<DWORD>(info->ContextRecord->Rip - reinterpret_cast<DWORD_PTR>(mod));
                        GetModuleFileNameA(mod, modName, MAX_PATH);
                    }
                    cs << "crash handler: rip=" << (void*)info->ContextRecord->Rip
                       << " module=" << modName << "+0x" << std::hex << ripOffset
                       << " rsp=" << (void*)info->ContextRecord->Rsp
                       << " rax=" << (void*)info->ContextRecord->Rax
                       << " rcx=" << (void*)info->ContextRecord->Rcx
                       << " rdx=" << (void*)info->ContextRecord->Rdx;
                    Logger->error(cs.str());
                }
            }
            Logger->error("crash handler: dumping backtrace + stack");
            Logger->dump_backtrace();
            Logger->flush();
        }
    } catch (...) {}

    // let the game's own unhandled-exception handler run afterwards (its dialog etc.)
    return EXCEPTION_CONTINUE_SEARCH; 
}

inline void installCrashHandler() {
    SetUnhandledExceptionFilter(&SASUnhandledExceptionFilter);
}

#define SAS_HOOK_TRY try

#define SAS_HOOK_CATCH_VOID                                                \
    catch (const std::exception& e) {                                      \
        try {                                                             \
            if (Logger) {                                                 \
                Logger->error("SAS hook exception: {}", e.what());       \
                Logger->dump_backtrace();                                  \
                Logger->flush();                                           \
            }                                                            \
        } catch (...) {                                                   \
        }                                                                \
    }                                                                      \
    catch (...) {                                                          \
        try {                                                             \
            if (Logger) {                                                 \
                Logger->error("SAS hook exception: unknown (non-std)"); \
                Logger->dump_backtrace();                                  \
                Logger->flush();                                           \
            }                                                            \
        } catch (...) {                                                   \
        }                                                                \
    }

#define SAS_HOOK_CATCH_RET(fallback)                                       \
    catch (const std::exception& e) {                                      \
        try {                                                             \
            if (Logger) {                                                 \
                Logger->error("SAS hook exception: {}", e.what());       \
                Logger->dump_backtrace();                                  \
                Logger->flush();                                           \
            }                                                            \
        } catch (...) {                                                   \
        }                                                                \
        return (fallback);                                                 \
    }                                                                      \
    catch (...) {                                                          \
        try {                                                             \
            if (Logger) {                                                 \
                Logger->error("SAS hook exception: unknown (non-std)"); \
                Logger->dump_backtrace();                                  \
                Logger->flush();                                           \
            }                                                            \
        } catch (...) {                                                   \
        }                                                                \
        return (fallback);                                                 \
    }

#endif // SAS_LOGGER_H
