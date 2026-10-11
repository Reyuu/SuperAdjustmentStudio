// SAS shared helpers. include from any shader in this directory:
//   #include "shared.hlsl"
// include guards are your cycle protection.

#ifndef SAS_SHARED_HLSL
#define SAS_SHARED_HLSL

float sasLuma(float3 c) {
    return dot(c, float3(0.2126f, 0.7152f, 0.0722f));
}

float3 sasVignetteMask(float2 uv, float strength, float softness) {
    float d = distance(uv, float2(0.5f, 0.5f));
    float m = smoothstep(0.5f, 0.5f - max(softness, 1e-4f), d);
    return lerp(float3(1.0f, 1.0f, 1.0f), float3(m, m, m), saturate(strength));
}

// same mask, quantized into bands steps for a hard-edged vignette
float3 sasVignetteMask(float2 uv, float strength, float softness, float bands) {
    float3 m = sasVignetteMask(uv, strength, softness);
    if (bands > 1.5f) {
        float q = floor(m.r * bands + 0.5) / bands;
        m = float3(q, q, q);
    }
    return m;
}

#endif // SAS_SHARED_HLSL