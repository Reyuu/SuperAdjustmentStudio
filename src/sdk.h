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

class SDKContext {
    public:
        void initSdkGlobals(ISharedProxyInterface* proxy);
        bool isInitialized() const {
            return sdkInitializer != nullptr;
        }
        void* processEventAddress() const {
            return processEventAddressPointer;
        }
        ISharedProxyInterface* proxy() const {
            return spiProxy;
        }
        LESDK::Initializer* initializer() const {
            return sdkInitializer;
        }
    
    private:
        ISharedProxyInterface* spiProxy = nullptr;
        LESDK::Initializer* sdkInitializer = nullptr;
        void* processEventAddressPointer = nullptr;
};

#endif // SAS_SDK_H