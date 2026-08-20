#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "SPI.h"
#include "application.h"
#include "plugin.h"

static Application gApplication;

SPI_PLUGINSIDE_SUPPORT(LPLUGIN_NAME, LPLUGIN_AUTHOR, LPLUGIN_VERSION, SPI_GAME_LE2, SPI_VERSION_ANY);
SPI_PLUGINSIDE_POSTLOAD;
SPI_PLUGINSIDE_SEQATTACH;

SPI_IMPLEMENT_ATTACH {
    return gApplication.attach(InterfacePtr);
}

SPI_IMPLEMENT_DETACH {
    return true;
}

// win32 call for when the DLL is attached
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
        case DLL_PROCESS_ATTACH:
            break;
        case DLL_PROCESS_DETACH:
            // tear it all down
            gApplication.detach();
            break;
        default:
            break;
    }
    return TRUE;
}
