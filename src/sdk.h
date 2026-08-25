#ifndef SAS_SDK_H
#define SAS_SDK_H

#include "../thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp"

#include "plugin.h"
#include <LESDK/Includes.LE2.hpp>
#include <LESDK/Init.hpp>

#define SDK_INITIALIZE_OBJECT_TYPED(variable, resolveType, address) \
if (!variable) { \
    variable = sdkInitializer->ResolveTyped<resolveType>(address); \
}

// we need to resolve the native functions, because for some reason LESDK's way does not work
// UE3: Core/Inc/UnPkg.h and LESDK
using loadPackageType = UPackage*(
    UPackage*,
    wchar_t*,
    ELoadFlags
);

class SDKContext {
    public:
        void initSdkGlobals(ISharedProxyInterface* proxy);
        bool isInitialized() const {
            return sdkInitializer != nullptr;
        }
        void* processEventAddress() const {
            return processEventAddressPointer;
        }
        void* gameEngineTickAddress() const {
            return gameEngineTickAddressPointer;
        }
        ISharedProxyInterface* proxy() const {
            return spiProxy;
        }
        LESDK::Initializer* initializer() const {
            return sdkInitializer;
        }
        loadPackageType* loadPackage() const {
            return loadPackagePointer;
        }
    
    private:
        ISharedProxyInterface* spiProxy = nullptr;
        LESDK::Initializer* sdkInitializer = nullptr;
        void* processEventAddressPointer = nullptr;
        void* gameEngineTickAddressPointer = nullptr;
        loadPackageType* loadPackagePointer = nullptr;
};

#endif // SAS_SDK_H