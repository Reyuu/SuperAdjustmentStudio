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
// UE3: Core/Inc/UnObjBas.h
using staticContructorObjectType = UObject*(
    UClass*, 
    UObject*, 
    SFXName, 
    QWORD, // DWORD for 32bit, QWORD for 64bit
    UObject*, 
    void*, 
    UObject*, 
    void*
);
// UE3: Engine/Inc/UnWorld.h
using worldSpawnActorType = AActor*(
    UWorld*,
    UClass*,
    SFXName,
    const FVector&,
    const FRotator&,
    AActor*,
    UBOOL,
    UBOOL,
    AActor*,
    APawn*,
    UBOOL,
    ULevel*
);
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
        staticContructorObjectType* staticConstructorObject() const {
            return staticConstructorObjectPointer;
        }
        worldSpawnActorType* worldSpawnActor() const {
            return worldSpawnActorPointer;
        }
        loadPackageType* loadPackage() const {
            return loadPackagePointer;
        }
    
    private:
        ISharedProxyInterface* spiProxy = nullptr;
        LESDK::Initializer* sdkInitializer = nullptr;
        void* processEventAddressPointer = nullptr;
        void* gameEngineTickAddressPointer = nullptr;
        staticContructorObjectType* staticConstructorObjectPointer = nullptr;
        worldSpawnActorType* worldSpawnActorPointer = nullptr;
        loadPackageType* loadPackagePointer = nullptr;
};

#endif // SAS_SDK_H