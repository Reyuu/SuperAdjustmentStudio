#include "oodle.h"
#include "logger.h"

#include <cstdint>
#include <mutex>
#include <windows.h>

typedef int64_t (*OodleLZ_DecompressFn)(const void* compBuf, int64_t compBufSize, void* rawBuf, int64_t rawLen, int fuzzSafe, int checkCRC, int verbosity,
                                        void* decBufBase, int64_t decBufSize, void* fpCallback, void* callbackUserData, void* decoderMemory,
                                        int64_t decoderMemorySize, int threadPhase);

static OodleLZ_DecompressFn pfnDecompress = nullptr;
static HMODULE s_hOodle = nullptr;
static std::once_flag s_oodleInitFlag;

bool Oodle_init() {
    std::call_once(s_oodleInitFlag, []() {
        s_hOodle = LoadLibraryA("oo2core_8_win64.dll");
        if (!s_hOodle) {
            Logger->error("Oodle: failed to load oo2core_8_win64.dll");
            return;
        }
        pfnDecompress = reinterpret_cast<OodleLZ_DecompressFn>(GetProcAddress(s_hOodle, "OodleLZ_Decompress"));
        if (!pfnDecompress) {
            Logger->error("Oodle: could not resolve OodleLZ_Decompress export");
            return;
        }
        Logger->info("Oodle: resolved OodleLZ_Decompress");
    });

    if (!pfnDecompress) {
        Logger->error("Oodle: oo2core_8_win64.dll not ready");
        return false;
    }
    return true;
}

size_t Oodle_decompress(const void* src, size_t srcLen, void* dst, size_t dstLen) {
    if (!pfnDecompress) {
        Logger->error("Oodle_decompress called before Oodle_init");
        return 0;
    }
    if (!src || !dst || srcLen == 0 || dstLen == 0) {
        return 0;
    }

    int64_t result = pfnDecompress(src, static_cast<int64_t>(srcLen), dst, static_cast<int64_t>(dstLen),
                                   1,       // OodleLZ_FuzzSafe_Yes
                                   0,       // OodleLZ_CheckCRC_No
                                   0,       // OodleLZ_Verbosity_None
                                   nullptr, // decBufBase
                                   0,       // decBufSize
                                   nullptr, // fpCallback
                                   nullptr, // callbackUserData
                                   nullptr, // decoderMemory
                                   0,       // decoderMemorySize
                                   3        // OodleLZ_Decode_Unthreaded
    );

    if (result <= 0) {
        Logger->error("Oodle_decompress: decompression failed");
        return 0;
    }

    return static_cast<size_t>(result);
}
