#ifndef SAS_HOOK_MANAGER_H
#define SAS_HOOK_MANAGER_H

#include <atomic>
#include <set>
#include <string>
#include "sdk.h"

class HookManager {
    public:
        void setSdkContext(SDKContext* sdk);
        void* install(const char* name, void* target, void* detour);
        void uninstallAll();
        bool areHooksInstalled() const {
            return areHooksInstalledState;
        }
    private:
    SDKContext* sdkInstance = nullptr;
    std::set<std::string> hookNames;
    std::atomic<bool> areHooksInstalledState{false};

};

#endif //  SAS_HOOK_MANAGER_H