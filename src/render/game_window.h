#ifndef SAS_GAME_WINDOW_H
#define SAS_GAME_WINDOW_H
#include "../../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include <utility>
#include <vector>
#include <windows.h>

class GameWindow {
    public:
        void setPrimary(HWND hwnd) {
            primaryGameWindow = hwnd;
        };
        HWND primary() const {
            return primaryGameWindow;
        }
        void addSubclassed(HWND hwnd, WNDPROC proc) {
            subclassedWindowsVector.emplace_back(hwnd, proc);
        }
        void clearSubclassed() {
            subclassedWindowsVector.clear();
        }
        const std::vector<std::pair<HWND, WNDPROC>>& subclassedWindows() const {
            return subclassedWindowsVector;
        }

        WNDPROC subclass(HWND hwnd, WNDPROC handler);
        void subclassAllProcessWindows(WNDPROC handler);
        void restoreAll();

    private:
        std::vector<std::pair<HWND, WNDPROC>> subclassedWindowsVector;
        HWND primaryGameWindow = NULL;
};

#endif // SAS_GAME_WINDOW_H
