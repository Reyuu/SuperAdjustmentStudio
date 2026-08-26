#define INITGUID
#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "mouse.h"

#include <Unknwn.h> // LPUNKNOWN
#include <cstring>
#include <sstream>

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>

#include "application.h"
#include "hook_manager.h"
#include "logger.h"

#pragma region // WinApi mouse trap
static BOOL WINAPI hkGetCursorPos(LPPOINT lpPoint) {
    return Application::instance().mouse().hookGetCursorPos(lpPoint);
}

static BOOL WINAPI hkGetCursorInfo(PCURSORINFO pci) {
    return Application::instance().mouse().hookGetCursorInfo(pci);
}

BOOL Mouse::hookGetCursorPos(LPPOINT lpPoint) {
    SAS_HOOK_TRY {
        if (cursorPassthroughState) {
            return origGetCursorPos(lpPoint);
        }
        if (Application::instance().ui().showUI().load()) {
            if (lpPoint) {
                *lpPoint = frozenCursorPosition;
                return TRUE;
            }
            return FALSE;
        }
        return origGetCursorPos(lpPoint);
    } SAS_HOOK_CATCH_RET(origGetCursorPos ? origGetCursorPos(lpPoint) : FALSE)
}

BOOL Mouse::hookGetCursorInfo(PCURSORINFO pci) {
    SAS_HOOK_TRY {
        if (cursorPassthroughState) {
            return origGetCursorInfo(pci);
        }
        if (Application::instance().ui().showUI().load()) {
            if (pci) {
                pci->cbSize = sizeof(CURSORINFO);
                pci->flags = CURSOR_SHOWING;
                pci->hCursor = nullptr;
                pci->ptScreenPos = frozenCursorPosition;
            }
            return TRUE;
        }
        return origGetCursorInfo(pci);
    } SAS_HOOK_CATCH_RET(origGetCursorInfo ? origGetCursorInfo(pci) : FALSE)
}

// winapi mouse trap, probably overkill but w/e
void Mouse::initMouseTrap(HookManager& hooks) {
    HMODULE h = LoadLibraryA("user32.dll");
    if (!h) {
        Logger->debug("WinApi mouse trap: user32.dll failed to load.");
        return;
    }

    // GetCursorPos
    if (FARPROC p = GetProcAddress(h, "GetCursorPos")) {
        origGetCursorPos = (GetCursorPosFn)hooks.install("GetCursorPos", p, &hkGetCursorPos);
        if (origGetCursorPos) {
            Logger->debug("WinApi mouse trap: GetCursorPos hooked");
        } else {
            Logger->debug("WinApi mouse trap: failed to hook GetCursorPos!");
        }
    } else {
        Logger->debug("WinApi mouse trap: could not find GetCursorPos");
    }

    // GetCursorInfo
    if (FARPROC p = GetProcAddress(h, "GetCursorInfo")) {
        origGetCursorInfo = (GetCursorInfoFn)hooks.install("GetCursorInfo", p, &hkGetCursorInfo);
        if (origGetCursorInfo) {
            Logger->debug("WinApi mouse trap: GetCursorInfo hooked");
        } else {
            Logger->debug("WinApi mouse trap: failed to hook GetCursorInfo!");
        }
    } else {
        Logger->debug("WinApi mouse trap: could not find GetCursorInfo");
    }
}
#pragma endregion

#pragma region // DirectInput8 mouse trap

// Dereference the COM vtable pointer stored in the first field of an interface object.
static void** d8Vtbl(void* obj) {
    return *reinterpret_cast<void***>(obj);
}

bool Mouse::d8FreezeIfUI(DWORD cbData, LPVOID lpvData, const char* variant) {
    if (Application::instance().ui().showUI().load() && !Application::instance().engine().isCameraDragActive().load()) {
        Application::instance().engine().freezeLook(true);
        if (lpvData) {
            memset(lpvData, 0, cbData); // fill with zeros, so the game does not have anything to read
            return true;
        }
    }
    if (!di8PollLogged && lpvData && cbData >= 12) {
        const long lx = ((const long*)lpvData)[0];
        const long ly = ((const long*)lpvData)[1];
        if (lx != 0 || ly != 0) {
            std::ostringstream ss;
            ss << "DI8 mouse trap: game polls " << variant << " GetDeviceState (delta=" << lx << "," << ly << ")";
            Logger->debug(ss.str());
            di8PollLogged = true;
        }
    }
    return false;
}

// detours

static HRESULT STDMETHODCALLTYPE hkD8GetDeviceStateW(void* This, DWORD cbData, LPVOID lpvData) {
    return Application::instance().mouse().hookD8GetDeviceState(This, cbData, lpvData, true);
}

static HRESULT STDMETHODCALLTYPE hkD8GetDeviceStateA(void* This, DWORD cbData, LPVOID lpvData) {
    return Application::instance().mouse().hookD8GetDeviceState(This, cbData, lpvData, false);
}

static HRESULT STDMETHODCALLTYPE hkD8GetDeviceDataW(void* This, DWORD cbObjectData, void* rgdod, DWORD* pdwInOut, DWORD dwFlags) {
    return Application::instance().mouse().hookD8GetDeviceData(This, cbObjectData, rgdod, pdwInOut, dwFlags, true);
}

static HRESULT STDMETHODCALLTYPE hkD8GetDeviceDataA(void* This, DWORD cbObjectData, void* rgdod, DWORD* pdwInOut, DWORD dwFlags) {
    return Application::instance().mouse().hookD8GetDeviceData(This, cbObjectData, rgdod, pdwInOut, dwFlags, false);
}

HRESULT Mouse::hookD8GetDeviceState(void* This, DWORD cbData, LPVOID lpvData, bool isW) {
    SAS_HOOK_TRY {
        if (d8FreezeIfUI(cbData, lpvData, isW ? "W" : "A")) {
            return 0;
        }
        D8GetDeviceStateFn orig = isW ? origD8GetDeviceStateW : origD8GetDeviceStateA;
        return orig(This, cbData, lpvData);
    } SAS_HOOK_CATCH_RET((isW && origD8GetDeviceStateW) ? origD8GetDeviceStateW(This, cbData, lpvData)
                                                      : (origD8GetDeviceStateA ? origD8GetDeviceStateA(This, cbData, lpvData) : E_FAIL))
}

HRESULT Mouse::hookD8GetDeviceData(void* This, DWORD cbObjectData, void* rgdod, DWORD* pdwInOut, DWORD dwFlags, bool isW) {
    SAS_HOOK_TRY {
        if (Application::instance().ui().showUI().load() && !Application::instance().engine().isCameraDragActive().load()) {
            Application::instance().engine().freezeLook(true);
            if (pdwInOut) {
                *pdwInOut = 0;
            }
        }

        bool& logged = isW ? di8DataLoggedW : di8DataLoggedA;
        if (!logged && rgdod && pdwInOut && *pdwInOut > 0) {
            const DWORD* h = (const DWORD*)rgdod;
            if (h[1] != 0) {
                std::ostringstream ss;
                ss << "DI8 mouse trap: game polls " << (isW ? "W" : "A") << " GetDeviceData (ofs=" << h[0] << " delta=" << (int)h[1] << ")";
                Logger->debug(ss.str());
                logged = true;
            }
        }
        D8GetDeviceDataFn orig = isW ? origD8GetDeviceDataW : origD8GetDeviceDataA;
        return orig(This, cbObjectData, rgdod, pdwInOut, dwFlags);
    } SAS_HOOK_CATCH_RET((isW && origD8GetDeviceDataW) ? origD8GetDeviceDataW(This, cbObjectData, rgdod, pdwInOut, dwFlags)
                                                     : (origD8GetDeviceDataA ? origD8GetDeviceDataA(This, cbObjectData, rgdod, pdwInOut, dwFlags) : E_FAIL))
}

// insanity begins here

void Mouse::installD8DeviceHooks(void* dev, bool isW) {
    // vtable slots are fixed, 9 for devicestate, 10 for devicedata
    // vtable is just a pointer-function table in the first field of COM object
    void** vtable = d8Vtbl(dev);
    void* getState = vtable[9];
    void* getData = vtable[10];

    D8GetDeviceStateFn* origState = isW ? &origD8GetDeviceStateW : &origD8GetDeviceStateA;
    D8GetDeviceDataFn* origData = isW ? &origD8GetDeviceDataW : &origD8GetDeviceDataA;
    D8GetDeviceStateFn hkState = isW ? &hkD8GetDeviceStateW : &hkD8GetDeviceStateA;
    D8GetDeviceDataFn hkData = isW ? &hkD8GetDeviceDataW : &hkD8GetDeviceDataA;
    std::ostringstream ss;
    ss << "DI8 mouse trap:: hooking " << (isW ? "W" : "A") << " device (state=0x" << std::hex << getState << " data=0x" << getData << std::dec << ")";
    Logger->debug(ss.str());

    if (!*origState) {
        *origState = (D8GetDeviceStateFn)hookManager->install(isW ? "D8GetDeviceStateW" : "D8GetDeviceStateA", getState, (void*)hkState);
        if (!*origState) {
            Logger->debug("DI8 mouse trap: GetDeviceState hook failed!");
        }
    }

    if (!*origData) {
        *origData = (D8GetDeviceDataFn)hookManager->install(isW ? "D8GetDeviceDataW" : "D8GetDeviceDataA", getData, (void*)hkData);
        if (!*origData) {
            Logger->debug("DI8 mouse trap: GetDeviceData hook failed!");
        }
    }
}

static HRESULT STDMETHODCALLTYPE hkD8CreateDevice(void* This, REFGUID rguid, void** out, void* punkOuter) {
    return Application::instance().mouse().hookD8CreateDevice(This, rguid, out, punkOuter);
}

HRESULT Mouse::hookD8CreateDevice(void* This, REFGUID rguid, void** out, void* punkOuter) {
    SAS_HOOK_TRY {
        HRESULT hr = origD8CreateDevice ? origD8CreateDevice(This, rguid, out, punkOuter) : E_FAIL;
        if (SUCCEEDED(hr) && out && *out && IsEqualGUID(rguid, GUID_SysMouse)) {
            Logger->debug("DI8 mouse trap: Game created a mouse device. Hooking its poll methods.");
            auto it = d8VariantByInterface.find(This);
            bool isW = it != d8VariantByInterface.end() ? it->second : (d8CreateVariant == 0);
            installD8DeviceHooks(*out, isW);
        }
        return hr;
    } SAS_HOOK_CATCH_RET(origD8CreateDevice ? origD8CreateDevice(This, rguid, out, punkOuter) : E_FAIL)
}

static HRESULT STDMETHODCALLTYPE hkDirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
    return Application::instance().mouse().hookDirectInput8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
}

HRESULT Mouse::hookDirectInput8Create(HINSTANCE hinst, DWORD dwVersion, const IID& riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter) {
    SAS_HOOK_TRY {
        HRESULT hr = origD8Create ? origD8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter) : E_FAIL;
        if (SUCCEEDED(hr) && ppvOut && *ppvOut) {
            d8CreateVariant = IsEqualGUID(riidltf, IID_IDirectInput8W) ? 0 : 1;
            d8VariantByInterface[*ppvOut] = d8CreateVariant == 0;
            void** vtable = d8Vtbl(*ppvOut);
            void* createDev = vtable[3];
            if (!origD8CreateDevice) {
                origD8CreateDevice = (D8CreateDeviceFn)hookManager->install("D8CreateDevice", createDev, &hkD8CreateDevice);
                if (!origD8CreateDevice) {
                    Logger->debug("DI8 mouse trap: CreateDevice hook failed!");
                }
            } else {
                Logger->debug("DI8 mouse trap: game DirectInput8Create intercepted, CreateDevice hooked.");
            }
        }
        return hr;
    } SAS_HOOK_CATCH_RET(origD8Create ? origD8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter) : E_FAIL)
}

void Mouse::initDI8MouseHook(HookManager& hooks) {
    hookManager = &hooks;
    HMODULE h = LoadLibraryA("dinput8.dll");
    if (!h) {
        Logger->debug("DI8 mouse trap: dinput8.dll not loaded!");
        return;
    }

    D8CreateFn create = (D8CreateFn)GetProcAddress(h, "DirectInput8Create");
    if (!create) {
        Logger->debug("DI8 mouse trap: DirectInput8Create not found");
        return;
    }

    origD8Create = (D8CreateFn)hookManager->install("D8DirectInput8Create", (void*)create, &hkDirectInput8Create);
    if (origD8Create) {
        Logger->debug("DI8 mouse trap: DirectInput8Create export hooked.");
    } else {
        Logger->debug("DI8 mouse trap: could not hook DirectInput8Create export");
    }

    // try creating out own instances too, in case of proxy dll
    const GUID* variants[2] = {&IID_IDirectInput8W, &IID_IDirectInput8A};
    const D8GetDeviceStateFn hkState[2] = {&hkD8GetDeviceStateW, &hkD8GetDeviceStateA};
    const D8GetDeviceDataFn hkData[2] = {&hkD8GetDeviceDataW, &hkD8GetDeviceDataA};
    D8GetDeviceStateFn* origState[2] = {&origD8GetDeviceStateW, &origD8GetDeviceStateA};
    D8GetDeviceDataFn* origData[2] = {&origD8GetDeviceDataW, &origD8GetDeviceDataA};
    int hookedVariants = 0;

    for (int v = 0; v < 2; ++v) {
        void* di8 = nullptr;
        HRESULT hr = create((HINSTANCE)GetModuleHandle(NULL), 0x0800, *variants[v], &di8, NULL);
        if (FAILED(hr) || !di8) {
            std::ostringstream ss;
            ss << "DI8 mouse trap: DirectInput8Create(" << (v == 0 ? "W" : "A") << ") failed hr=0x" << std::hex << (unsigned long)hr << std::dec;
            Logger->debug(ss.str());
            continue;
        }

        void** d8vtable = d8Vtbl(di8);
        D8CreateDeviceFn createDev = (D8CreateDeviceFn)d8vtable[3];
        void* dev = nullptr;
        HRESULT dhr = createDev(di8, GUID_SysMouse, &dev, NULL);
        if (FAILED(dhr) || !dev) {
            std::ostringstream ss;
            ss << "DI8 mouse trap: CreateDevice(SysMouse) failed hr=0x" << std::hex << (unsigned long)dhr << std::dec;
            Logger->debug(ss.str());
            // call Release via vtable to release the temporary object di8
            ((D8ReleaseFn)d8vtable[2])(di8);
            continue;
        }

        void** vtable = d8Vtbl(dev);
        bool ok = true;
        if (!*origState[v]) {
            *origState[v] = (D8GetDeviceStateFn)hookManager->install(v == 0 ? "D8GetDeviceStateW" : "D8GetDeviceStateA", vtable[9], (void*)hkState[v]);
            if (!*origState[v]) {
                ok = false;
            }
        }

        if (!*origData[v]) {
            *origData[v] = (D8GetDeviceDataFn)hookManager->install(v == 0 ? "D8GetDeviceDataW" : "D8GetDeviceDataA", vtable[10], (void*)hkData[v]);
            if (!*origData[v]) {
                ok = false;
            }
        }

        ((D8ReleaseFn)vtable[2])(dev);
        ((D8ReleaseFn)d8vtable[2])(di8);
        if (ok) {
            ++hookedVariants;
        }

        std::ostringstream ss;
        ss << "DI8 mouse trap: " << (v == 0 ? "W" : "A") << " variant " << (ok ? "hooked" : "hook FAILED") << " (state=0x" << std::hex << vtable[9]
           << " data=0x" << vtable[10] << std::dec << ")";
        Logger->debug(ss.str());
    }
    if (hookedVariants == 0) {
        Logger->debug("DI8 mouse trap: no variants hooked!");
    }
}

#pragma endregion