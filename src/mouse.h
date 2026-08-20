#ifndef SAS_MOUSE_H
#define SAS_MOUSE_H

#include <windows.h>
#include <Unknwn.h> // LPUNKNOWN
#include <unordered_map>
#include "hook_manager.h"

// Useful: https://www.youtube.com/watch?v=oh9i7hPQZT8

typedef BOOL(WINAPI* GetCursorPosFn)(LPPOINT);
typedef BOOL(WINAPI* GetCursorInfoFn)(PCURSORINFO);
typedef HRESULT(STDMETHODCALLTYPE* D8CreateFn)(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter);
typedef HRESULT(STDMETHODCALLTYPE* D8CreateDeviceFn)(void* This, REFGUID rguid, void** out, void* punkOuter);
typedef HRESULT(STDMETHODCALLTYPE* D8GetDeviceStateFn)(void* This, DWORD cbData, LPVOID lpvData);
typedef HRESULT(STDMETHODCALLTYPE* D8GetDeviceDataFn)(void* This, DWORD cbObjectData, void* rgdod, DWORD* pdwInOut, DWORD dwFlags);
typedef ULONG(STDMETHODCALLTYPE* D8ReleaseFn)(void* This);

class Mouse {
    public:
        bool& cursorPassthrough() {
            return cursorPassthroughState;
        }
        POINT& frozenCursor() {
            return frozenCursorPosition;
        }
        
        void initMouseTrap(HookManager& hooks);
        void initDI8MouseHook(HookManager& hooks);

        BOOL hookGetCursorPos(LPPOINT lpPoint);
        BOOL hookGetCursorInfo(PCURSORINFO pci);

        HRESULT hookD8GetDeviceState(void* This, DWORD cbData, LPVOID lpvData, bool isW);
        HRESULT hookD8GetDeviceData(void* This, DWORD cbObjectData, void* rgdod, DWORD* pdwInOut, DWORD dwFlags, bool isW);
        HRESULT hookD8CreateDevice(void* This, REFGUID rguid, void** out, void* punkOuter);
        HRESULT hookDirectInput8Create(HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter);

    private:
        bool cursorPassthroughState = false;
        POINT frozenCursorPosition = {0, 0};

        HookManager* hookManager = nullptr;

        GetCursorPosFn origGetCursorPos = nullptr;
        GetCursorInfoFn origGetCursorInfo = nullptr;
        D8GetDeviceStateFn origD8GetDeviceStateW = nullptr;
        D8GetDeviceStateFn origD8GetDeviceStateA = nullptr;
        D8GetDeviceDataFn origD8GetDeviceDataW = nullptr;
        D8GetDeviceDataFn origD8GetDeviceDataA = nullptr;
        D8CreateFn origD8Create = nullptr;
        D8CreateDeviceFn origD8CreateDevice = nullptr;

        int d8CreateVariant = 0;
        std::unordered_map<void*, bool> d8VariantByInterface;

        bool di8PollLogged = false;
        bool di8DataLoggedW = false;
        bool di8DataLoggedA = false;

        bool d8FreezeIfUI(DWORD cbData, LPVOID lpvData, const char* variant);
        void installD8DeviceHooks(void* dev, bool isW);
};

#endif // SAS_MOUSE_H