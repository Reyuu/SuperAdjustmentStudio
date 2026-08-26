#include "sdk.h"
#include "logger.h"
#include <sstream>

void SDKContext::initSdkGlobals(ISharedProxyInterface* proxy) {
    if (!proxy) {
        Logger->debug("No SPI proxy detected.");
        return;
    }

    if (!sdkInitializer) {
        spiProxy = proxy;
        sdkInitializer = new LESDK::Initializer(proxy, PLUGIN_NAME);
    }

    SDK_INITIALIZE_OBJECT_TYPED(UObject::GObjObjects, TArray<UObject*>, BUILTIN_GOBOBJECTS_RIP);
    SDK_INITIALIZE_OBJECT_TYPED(GMalloc, FMallocLike*, BUILTIN_GMALLOC_RIP);
    SDK_INITIALIZE_OBJECT_TYPED(SFXName::GBioNamePools, SFXNameEntry const*, BUILTIN_SFXNAMEPOOLS_RIP);
    SDK_INITIALIZE_OBJECT_TYPED(SFXName::GInitMethod, SFXName::tInitMethod, BUILTIN_SFXNAMEINIT_PHOOK);
    SDK_INITIALIZE_OBJECT_TYPED(GWorld, UWorld*, BUILTIN_GWORLD_RIP);
    // for some reason, it can be NOT typed???
    if (!GError) {
        GError = (void**)sdkInitializer->Resolve(BUILTIN_GWORLD_RIP);
    }

    SDK_INITIALIZE_OBJECT_TYPED(loadPackagePointer, loadPackageType, BUILTIN_LOADPACKAGE_RVA);

    if (!processEventAddressPointer) {
        processEventAddressPointer = sdkInitializer->Resolve(BUILTIN_PROCESSEVENT_PHOOK);
    }
    if (!gameEngineTickAddressPointer) {
        gameEngineTickAddressPointer = sdkInitializer->Resolve(BUILTIN_GAMEENGINETICK_RVA);
    }

    std::ostringstream ss;
    ss << " GObjObjects=0x" << (void*)UObject::GObjObjects << " GMalloc=0x" << (void*)GMalloc << " GBioNamePools=0x" << (void*)SFXName::GBioNamePools
       << " GInitMethod=0x" << (void*)SFXName::GInitMethod << " GWorld=0x" << (void*)GWorld << " ProcessEvent=0x" << processEventAddressPointer;
    Logger->debug(ss.str());
}