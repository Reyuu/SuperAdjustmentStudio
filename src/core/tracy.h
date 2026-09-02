#ifndef SAS_TRACY_H
#define SAS_TRACY_H

// tracy profiler wrapper — only active in Debug builds when SAS_TRACY_ENABLE is ON
// in Release (or when TRACY_ENABLE not defined) all macros become no-ops
#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>
// optional D3D11 GPU profiling
// #include <tracy/TracyD3D11.hpp>
#else
// no-op fallbacks so instrumentation sites compile without Tracy headers
#define ZoneScoped
#define ZoneScopedN(name)
#define ZoneScopedC(color)
#define ZoneNamed(name, active)
#define ZoneNamedN(var, name, active)
#define ZoneNamedC(var, name, color, active)
#define ZoneText(text, size)
#define ZoneName(text, size)
#define ZoneValue(value)
#define FrameMark
#define FrameMarkNamed(name)
#define FrameMarkStart(name)
#define FrameMarkEnd(name)
#define TracyMessage(text, size)
#define TracyMessageL(text)
#define TracyMessageC(text, size, color)
#define TracyPlot(name, val)
#define TracyAppInfo(text, size)
#define TracyAlloc(ptr, size)
#define TracyFree(ptr)
#define TracySecureAlloc(ptr, size)
#define TracySecureFree(ptr)
#endif

// scoped per-function zone with custom name
#ifdef TRACY_ENABLE
#define SAS_ZONE_SCOPED      ZoneScoped
#define SAS_ZONE_NAMED(name) ZoneScopedN(name)
#else
#define SAS_ZONE_SCOPED
#define SAS_ZONE_NAMED(name)
#endif

#endif // SAS_TRACY_H
