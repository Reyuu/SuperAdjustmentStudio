#include "hook_manager.h"


void HookManager::setSdkContext(SDKContext* sdk){
    sdkInstance = sdk;
}

void* HookManager::install(const char* name, void* target, void* detour){
    if (!sdkInstance || !sdkInstance->isInitialized() || !name || !target || !detour) {
        return nullptr;
    }

    void* orig = sdkInstance->initializer()->InstallHook(name, target, detour);
    if (orig) {
        hookNames.insert(name);
        areHooksInstalledState.store(true);
    }
    return orig;
}

void HookManager::uninstallAll(){
    if (!sdkInstance || !sdkInstance->isInitialized()) {
        return;
    }
    for (const std::string& name : hookNames) {
        sdkInstance->initializer()->UninstallHook(name.c_str());
    }
    hookNames.clear();
    areHooksInstalledState.store(false);
}
