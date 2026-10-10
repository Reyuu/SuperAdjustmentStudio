#ifndef SAS_LOGGER_H
#define SAS_LOGGER_H

#include "plugin.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/spdlog.h"
#include <atomic>
#include <cstdlib>
#include <dbghelp.h>
#include <exception>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

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

// a real stack trace. spdlog's backtrace only holds log records, which cannot locate
// a crash, so walk the frames and resolve symbols.
inline void logCrashStackTrace() {
    HANDLE process = GetCurrentProcess();
    static std::once_flag symInitFlag;
    std::call_once(symInitFlag, [process]() {
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
        SymInitialize(process, nullptr, TRUE);
    });

    void* frames[62];
    const USHORT captured = CaptureStackBackTrace(0, 62, frames, nullptr);
    SYMBOL_INFO* symbol = static_cast<SYMBOL_INFO*>(calloc(1, sizeof(SYMBOL_INFO) + MAX_SYM_NAME));
    if (!symbol) {
        Logger->error("crash handler: could not allocate symbol buffer");
        return;
    }
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME - 1;

    for (USHORT i = 0; i < captured; ++i) {
        DWORD64 displacement = 0;
        std::string name = "<unresolved>";
        if (SymFromAddr(process, reinterpret_cast<DWORD64>(frames[i]), &displacement, symbol)) {
            name = symbol->Name;
            const size_t bang = name.rfind('!');
            if (bang != std::string::npos) {
                name = name.substr(bang + 1);
            }
        }
        Logger->error("crash handler: #{} {} @{:p}", (int)i, name, frames[i]);
    }
    free(symbol);
}

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
        if (info && info->ExceptionRecord) {
            HANDLE hDump = CreateFileA("SuperAdjustmentStudio.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hDump != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION mei;
                mei.ThreadId = GetCurrentThreadId();
                mei.ExceptionPointers = info;
                mei.ClientPointers = FALSE;
                MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDump, MiniDumpNormal, &mei, nullptr, nullptr);
                CloseHandle(hDump);
            }
        }
        if (Logger) {
            if (info && info->ExceptionRecord) {
                EXCEPTION_RECORD* rec = info->ExceptionRecord;
                std::ostringstream es;
                es << "crash handler: code=0x" << std::hex << rec->ExceptionCode << " addr=" << rec->ExceptionAddress << " nparams=" << std::dec
                   << rec->NumberParameters;
                if (rec->ExceptionCode == 0xC0000005 && rec->NumberParameters >= 2) {
                    es << " access=" << (rec->ExceptionInformation[0] ? "write" : "read") << " at=0x" << std::hex << rec->ExceptionInformation[1];
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
                    cs << "crash handler: rip=" << (void*)info->ContextRecord->Rip << " module=" << modName << "+0x" << std::hex << ripOffset
                       << " rsp=" << (void*)info->ContextRecord->Rsp << " rax=" << (void*)info->ContextRecord->Rax << " rcx=" << (void*)info->ContextRecord->Rcx
                       << " rdx=" << (void*)info->ContextRecord->Rdx;
                    Logger->error(cs.str());
                }
            }
            Logger->error("crash handler: dumping backtrace + stack");
            logCrashStackTrace();
            Logger->dump_backtrace();
            Logger->flush();
        }
    } catch (...) {
    }

    // let the game's own unhandled-exception handler run afterwards (its dialog etc.)
    return EXCEPTION_CONTINUE_SEARCH;
}

// the filter above is easy to lose, the engine installs its own during startup and
// the last call wins, so a real crash can end up unlogged. a vectored handler runs
// first and cannot be replaced, so this catches it even on the first chance
inline LONG WINAPI SASVectoredExceptionHandler(EXCEPTION_POINTERS* info) {
    if (!info || !info->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    EXCEPTION_RECORD* rec = info->ExceptionRecord;
    const DWORD code = rec->ExceptionCode;
    // only real damage, the rest is routine engine control flow and would be noise
    const bool interesting = code == 0xC0000005     // access violation
                             || code == 0xC0000374  // heap corruption
                             || code == 0xC00000FD  // stack overflow
                             || code == 0xC000001D  // illegal instruction
                             || code == 0xC0000409; // stack buffer overrun
    if (!interesting || !Logger) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    try {
        std::ostringstream es;
        es << "VECTORED crash: code=0x" << std::hex << code << std::dec << " addr=" << rec->ExceptionAddress << " nparams=" << rec->NumberParameters;
        if (code == 0xC0000005 && rec->NumberParameters >= 2) {
            es << " access=" << (rec->ExceptionInformation[0] ? "write" : "read") << " at=0x" << std::hex << rec->ExceptionInformation[1];
        }
        if (info->ContextRecord) {
            HMODULE mod = nullptr;
            char modName[MAX_PATH] = {};
            DWORD ripOffset = 0;
            const DWORD_PTR rip = info->ContextRecord->Rip;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(rip),
                                   &mod)) {
                ripOffset = static_cast<DWORD>(rip - reinterpret_cast<DWORD_PTR>(mod));
                GetModuleFileNameA(mod, modName, MAX_PATH);
            }
            es << " | rip=" << reinterpret_cast<void*>(rip) << " module=" << modName << "+0x" << std::hex << ripOffset << " rax=" << std::hex
               << reinterpret_cast<void*>(info->ContextRecord->Rax) << " rcx=" << reinterpret_cast<void*>(info->ContextRecord->Rcx)
               << " rdx=" << reinterpret_cast<void*>(info->ContextRecord->Rdx) << " r8=" << reinterpret_cast<void*>(info->ContextRecord->R8);
        }
        Logger->error(es.str());
        // a few frames is enough to tell SAS from the game
        void* frames[24];
        const USHORT captured = CaptureStackBackTrace(0, 24, frames, nullptr);
        for (USHORT i = 0; i < captured; ++i) {
            HMODULE mod = nullptr;
            char modName[MAX_PATH] = {};
            DWORD off = 0;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(frames[i]),
                                   &mod)) {
                off = static_cast<DWORD>(reinterpret_cast<DWORD_PTR>(frames[i]) - reinterpret_cast<DWORD_PTR>(mod));
                GetModuleFileNameA(mod, modName, MAX_PATH);
            }
            Logger->error("VECTORED frame #" + std::to_string(i) + ": " + modName + "+0x" + [off] {
                char buf[32];
                snprintf(buf, sizeof(buf), "%llx", static_cast<unsigned long long>(off));
                return std::string(buf);
            }());
        }
        Logger->flush();
    } catch (...) {
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

inline void installCrashHandler() {
    SetUnhandledExceptionFilter(&SASUnhandledExceptionFilter);
    // front of the list so nothing can claim the fault first
    AddVectoredExceptionHandler(1, &SASVectoredExceptionHandler);
}

#define SAS_HOOK_TRY try

#define SAS_HOOK_CATCH_VOID                                             \
    catch (const std::exception& e) {                                   \
        try {                                                           \
            if (Logger) {                                               \
                Logger->error("SAS hook exception: {}", e.what());      \
                Logger->dump_backtrace();                               \
                Logger->flush();                                        \
            }                                                           \
        } catch (...) {                                                 \
        }                                                               \
    }                                                                   \
    catch (...) {                                                       \
        try {                                                           \
            if (Logger) {                                               \
                Logger->error("SAS hook exception: unknown (non-std)"); \
                Logger->dump_backtrace();                               \
                Logger->flush();                                        \
            }                                                           \
        } catch (...) {                                                 \
        }                                                               \
    }

#define SAS_HOOK_CATCH_RET(fallback)                                    \
    catch (const std::exception& e) {                                   \
        try {                                                           \
            if (Logger) {                                               \
                Logger->error("SAS hook exception: {}", e.what());      \
                Logger->dump_backtrace();                               \
                Logger->flush();                                        \
            }                                                           \
        } catch (...) {                                                 \
        }                                                               \
        return (fallback);                                              \
    }                                                                   \
    catch (...) {                                                       \
        try {                                                           \
            if (Logger) {                                               \
                Logger->error("SAS hook exception: unknown (non-std)"); \
                Logger->dump_backtrace();                               \
                Logger->flush();                                        \
            }                                                           \
        } catch (...) {                                                 \
        }                                                               \
        return (fallback);                                              \
    }

#endif // SAS_LOGGER_H
