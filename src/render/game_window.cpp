#include "game_window.h"
#include "logger.h"
#include "util.h"
#include <sstream>

struct SubclassAllArgs {
    public:
        GameWindow* window;
        WNDPROC handler;
};

WNDPROC GameWindow::subclass(HWND hwnd, WNDPROC handler) {
    if (!hwnd || !handler) {
        return nullptr;
    }

    for (const auto& e : subclassedWindowsVector) {
        if (e.first == hwnd) {
            return e.second;
        }
    }

    WNDPROC prev = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)handler);
    if (prev) {
        subclassedWindowsVector.emplace_back(hwnd, prev);
        wchar_t cls[128] = L"";
        GetClassNameW(hwnd, cls, 128);

        std::ostringstream ss;
        ss << "Subclassed window 0x" << (void*)hwnd << " class='" << WStringToUtf8(cls) << "'";
        Logger->debug(ss.str());
    } else {
        Logger->debug("failed to set WndProc on a window");
    }
    return prev;
}

BOOL CALLBACK EnumAllProcessWindows(HWND hwnd, LPARAM lParam) {
    auto* args = reinterpret_cast<SubclassAllArgs*>(lParam);
    DWORD wid = 0;
    GetWindowThreadProcessId(hwnd, &wid);
    if (wid == GetCurrentProcessId()) {
        args->window->subclass(hwnd, args->handler);
        return TRUE;
    }
    return FALSE;
}

void GameWindow::subclassAllProcessWindows(WNDPROC handler) {
    SubclassAllArgs args{this, handler};
    EnumWindows(EnumAllProcessWindows, reinterpret_cast<LPARAM>(&args));
}

void GameWindow::restoreAll() {
    for (const auto& e : subclassedWindowsVector) {
        SetWindowLongPtrW(e.first, GWLP_WNDPROC, (LONG_PTR)e.second);
    }
    subclassedWindowsVector.clear();
}