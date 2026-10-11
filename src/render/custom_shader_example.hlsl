// SAS custom shader example: vignette + tint with switchable blend modes.
//
// contract:
// - one file = one pipeline slot. the entry point must be named PS.
// - call whatever helper functions you like from PS (see shared.hlsl); branch on
//   toggle params for switchable looks in a single file.
// - bindings: t0 scene, t1 reserved (null), t2 scene depth when the LUT depth
//   capture is on (null otherwise), s0 sampler, b0 params below.
// - tunable UI is built from sas annotations, one per line. an annotation is a
//   comment whose text starts with `sas:` and a kind, eg:
//   - slider(Name, min, max, default [, step]) -> gParams[i].x
//   - drag(Name, min, max, default [, step])   -> gParams[i].x
//   - input(Name, default [, step])             -> gParams[i].x
//   - dragInt(Name, min, max, default)         -> gIntParams[i].x
//   - inputInt(Name, default [, step])          -> gIntParams[i].x
//   - toggle(Name, default 0|1)                -> gParams[i].x
//   - checkbox(Name, default 0|1)              -> gParams[i].x
//   - radio(Name, item0, item1, ..., index)    -> gParams[i].x (index)
//   - combo(Name, item0, item1, ..., index)    -> gParams[i].x (index)
//   - listbox(Name, item0, item1, ..., index)  -> gParams[i].x (index)
//   - color(Name, r, g, b [, a])               -> gColors[i].rgb(a)
//   sliders/drag/inputs/toggles/radios/combos/listboxes land in gParams in
//   annotation order, colors in gColors, integer widgets in gIntParams.
//   the array sizes are injected at compile time as SAS_NUM_FLOATS,
//   SAS_NUM_INTS and SAS_NUM_COLORS, so declare the cbuffer exactly as below and
//   read gParams[i].x / gIntParams[i].x / gColors[i].rgb - no cap on annotation
//   counts.
// - gTime, gResolution and gFlags are written by SAS for you. gFlags bit0 tells
//   you whether gDepth is bound, so never sample it blindly.
// - #include "other.hlsl" resolves relative to this file, then the shaders
//   directory. paths escaping above the shaders directory are rejected.
// - compile target is ps_5_0 (vs_5_0), falling back to 4_0 on devices
//   without feature level 11_0. errors show in the panel and the log.

#include "shared.hlsl"

// gParams indices: 0 Strength, 1 Softness, 2 Tint on, 3 Blend mode
// gIntParams indices: 0 Bands
// gColors indices: 0 Tint
// sas: slider(Strength, 0, 1, 0.45)
// sas: slider(Softness, 0.01, 0.5, 0.28)
// sas: toggle(Tint on, 1)
// sas: radio(Blend mode, Normal, Multiply, Screen, 0)
// sas: dragInt(Bands, 1, 8, 1)
// sas: color(Tint, 1.0, 0.85, 0.7)

cbuffer CustomCB : register(b0) {
    float4 gParams[SAS_NUM_FLOATS];
    int4   gIntParams[SAS_NUM_INTS];
    float4 gColors[SAS_NUM_COLORS];
    float  gTime;
    float2 gResolution;
    int    gFlags;
};
Texture2D gScene : register(t0);
Texture2D gAux0  : register(t1); // reserved, always null in v1
Texture2D gDepth : register(t2); // depth when LUT depth capture runs, else null
SamplerState gSampler : register(s0);

void VS(uint id : SV_VertexID, out float4 outPos : SV_Position,
        out float2 uv : TEXCOORD0) {
    uv = float2((float)((id << 1) & 2u), (float)(id & 2u));
    outPos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
}

float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 color = gScene.Sample(gSampler, uv).rgb;
    // params are positional: sliders/toggles/radios in annotation order, colors likewise.
    float strength = gParams[0].x;
    float softness = gParams[1].x;
    float tintOn   = gParams[2].x;
    int   mode     = (int)gParams[3].x;
    float bands    = max(1.0f, (float)gIntParams[0].x);
    float3 tint    = gColors[0].rgb;

    float3 graded = color;
    if (mode == 1) {
        graded = color * tint;                                 // Multiply
    } else if (mode == 2) {
        graded = 1.0f - (1.0f - color) * (1.0f - tint);        // Screen
    }
    color = lerp(color, graded, tintOn);
    color *= sasVignetteMask(uv, strength, softness, bands).r;
    return float4(color, 1.0f);
}