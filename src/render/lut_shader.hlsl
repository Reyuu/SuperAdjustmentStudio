// LUT shader based largely on ReShade's and MLUT's implementation

cbuffer LutCB : register(b0) {
    int    gMode; // 0 hald, 1 strip, 2 mlut
    int    gLevel;
    int    gTilesPerRow;
    int    gPadR1;
    float  gBlend;
    float  gChroma;
    float  gLuma;
    int    gPadA;
    int    gTileSize;
    int    gTileAmount;
    int    gLutAmount;
    int    gSelector;
    int    gUseDepth;
    float  gFocus;
    float  gRange;
    float  gBlendMin;
    float  gBlendMax;
    int    gPreview;
    int    gLinMode;
    int    gHeat;
    float  gNearZ;
    float  gFarZ;
    int    gInvert;
    int    gPadD;
};
Texture2D gScene  : register(t0);
Texture2D gLut    : register(t1);
Texture2D gDepth  : register(t2);
SamplerState gSampler : register(s0);

static const float3 LUMW = float3(0.2125f, 0.7154f, 0.0721f);

void VS(uint id : SV_VertexID, out float4 outPos : SV_Position,
        out float2 uv : TEXCOORD0) {
    uv = float2((float)((id << 1) & 2u), (float)(id & 2u));
    outPos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
}

float3 heatRamp(float t) {
    t = saturate(t);
    float3 hc = lerp(float3(0.0f, 0.0f, 1.0f), float3(0.0f, 1.0f, 1.0f), saturate(t * 4.0f));
    hc = lerp(hc, float3(0.0f, 1.0f, 0.0f), saturate(t * 4.0f - 1.0f));
    hc = lerp(hc, float3(1.0f, 1.0f, 0.0f), saturate(t * 4.0f - 2.0f));
    hc = lerp(hc, float3(1.0f, 0.0f, 0.0f), saturate(t * 4.0f - 3.0f));
    return hc;
}

float3 haldFetch(float2 rg, float slice, float tilesPerRow, float w) {
    tilesPerRow = max(tilesPerRow, 1.0f);
    w = max(w, 1.0f);
    float ts = w / tilesPerRow;
    float bx = fmod(slice, tilesPerRow);
    float by = floor(slice / tilesPerRow);
    float2 xy = float2(bx * ts + rg.x * (ts - 1.0f) + 0.5f, by * ts + rg.y * (ts - 1.0f) + 0.5f);
    return gLut.Sample(gSampler, xy / w).rgb;
}

float3 stripFetch(float2 rg, float slice, float tileSize, float tilesPerRow, float2 dims) {
    tilesPerRow = max(tilesPerRow, 1.0f);
    tileSize = max(tileSize, 1.0f);
    float tx = fmod(slice, tilesPerRow);
    float ty = floor(slice / tilesPerRow);
    float2 uv = float2(tx * tileSize + rg.x * (tileSize - 1.0f) + 0.5f, ty * tileSize + rg.y * (tileSize - 1.0f) + 0.5f);
    return gLut.Sample(gSampler, uv / dims).rgb;
}

float3 sampleLut(float3 color) {
    color = saturate(color);
    if (gMode == 0) {
        // Hald: N^3 x N^3 image, N x N tiles of N^2 x N^2, N^2 blue slices.
        float n = max((float)gLevel, 2.0f);
        float slices = n * n;
        float s = color.b * (slices - 1.0f);
        float s0 = floor(s);
        float f = s - s0;
        float s1 = min(s0 + 1.0f, slices - 1.0f);
        float3 a = haldFetch(color.rg, s0, n, n * n * n);
        float3 b = haldFetch(color.rg, s1, n, n * n * n);
        return lerp(a, b, f);
    }
    if (gMode == 1) {
        // Strip: N^3 pixels, tilesPerRow tiles of N x N, N blue slices.
        float n = max((float)gLevel, 2.0f);
        uint tw = 0u, th = 0u;
        gLut.GetDimensions(tw, th);
        float tpr = max((float)gTilesPerRow, 1.0f);
        float ts = max((float)tw / tpr, 1.0f);
        float2 dims = float2(max((float)tw, 1.0f), max((float)th, 1.0f));
        float sliceF = color.b * (n - 1.0f);
        float s0 = floor(sliceF);
        float f = sliceF - s0;
        float s1 = min(s0 + 1.0f, n - 1.0f);
        float3 a = stripFetch(color.rg, s0, ts, tpr, dims);
        float3 b = stripFetch(color.rg, s1, ts, tpr, dims);
        return lerp(a, b, f);
    }
    // MLUT: W = Ts*Ta, H = Ts*La, R in-tile, B selects tile, Y selects LUT.
    float ts = max((float)gTileSize, 2.0f);
    float ta = max((float)gTileAmount, 1.0f);
    float la = max((float)gLutAmount, 1.0f);
    float sel = clamp((float)gSelector, 0.0f, la - 1.0f);
    float sliceF = color.b * (ta - 1.0f);
    float s0 = floor(sliceF);
    float f = sliceF - s0;
    float s1 = min(s0 + 1.0f, ta - 1.0f);
    float baseX = (color.r * (ts - 1.0f) + 0.5f) / (ts * ta);
    float baseY = ((color.g * (ts - 1.0f) + 0.5f) / ts) / la + sel / la;
    float2 uvA = float2(baseX + s0 / ta, baseY);
    float2 uvB = float2(baseX + s1 / ta, baseY);
    float3 a = gLut.Sample(gSampler, uvA).rgb;
    float3 b = gLut.Sample(gSampler, uvB).rgb;
    return lerp(a, b, f);
}

float linearizeDepth(float depth) {
    if (gLinMode != 0) {
        float vz = gNearZ * gFarZ / max(gFarZ - depth * (gFarZ - gNearZ), 1e-6f);
        depth = saturate((vz - gNearZ) / max(gFarZ - gNearZ, 1e-6f));
    }
    if (gInvert != 0) {
        depth = 1.0f - depth;
    }
    return depth;
}

float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 src = gScene.Sample(gSampler, uv).rgb;
    float3 color = saturate(src);
    float3 lutColor = sampleLut(color);

    float srcLuma = dot(src, LUMW);
    float lutLuma = dot(lutColor, LUMW);

    // 1=chroma, 2=luma, 3=global blend mode.
    float gC = saturate(gChroma);
    float gL = saturate(gLuma);
    float gB = saturate(gBlend);
    float mixedLuma = lerp(srcLuma, lutLuma, gL);
    float3 mixedChroma = lerp(src - srcLuma, lutColor - lutLuma, gC);
    float3 finalColor = mixedLuma + mixedChroma;
    finalColor = lerp(src, finalColor, gB);

    if (gUseDepth != 0) {
        float depth = linearizeDepth(gDepth.Sample(gSampler, uv).r);

        float range = max(gRange, 1e-4f);
        float ns = min(saturate(gFocus - range), gFocus - 1e-4f);
        float fe = max(saturate(gFocus + range), gFocus + 1e-4f);
        float bn = smoothstep(ns, gFocus, depth);
        float bf = 1.0f - smoothstep(gFocus, fe, depth);
        float depthFactor = saturate(bn * bf);

        float w;
        if (gBlendMax <= gBlendMin) {
            w = step(gBlendMin, depthFactor);
        } else {
            float span = max(gBlendMax - gBlendMin, 1e-4f);
            w = saturate((depthFactor - gBlendMin) / span);
        }

        if (gPreview != 0) {
            if (gHeat != 0) {
                finalColor = heatRamp(depth);
            } else {
                finalColor = float3(w, w, w);
            }
        } else {
            finalColor = lerp(src, finalColor, w);
        }
    }
    return float4(finalColor, 1.0f);
}

float4 DepthViewPS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float depth = linearizeDepth(gDepth.Sample(gSampler, uv).r);
    if (gHeat != 0) {
        return float4(heatRamp(depth), 1.0f);
    }
    return float4(depth, depth, depth, 1.0f);
}
