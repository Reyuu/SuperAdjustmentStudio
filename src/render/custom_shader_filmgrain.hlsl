// SAS film grain, ported from GShade-Shaders FilmGrain.fx
// (https://github.com/killashok/GShade-Shaders, by CeeJay.dk) to the SAS custom
// shader contract. The Box-Muller gaussian noise is unchanged; ReShade's
// BackBuffer becomes gScene, Timer becomes gTime (seconds, so the salt is
// scaled), and the uniforms become sas annotations. The defaults below differ
// from FilmGrain.fx on purpose: its Variance 0.4 and SignalToNoiseRatio 6 leave
// barely a level of grain on anything but deep shadow, which reads as frozen.
//
// gParams: 0 Intensity, 1 Variance, 2 Mean, 3 Animation. gIntParams: 0 Signal to noise.
// sas: slider(Intensity, 0, 1, 0.5)
// sas: slider(Variance, 0, 1, 0.6)
// sas: slider(Mean, 0, 1, 0.5)
// sas: dragInt(Signal to noise, 0, 16, 2)
// sas: slider(Animation, 0, 1, 0.134)

cbuffer CustomCB : register(b0) {
    float4 gParams[SAS_NUM_FLOATS];
    int4   gIntParams[SAS_NUM_INTS];
    float  gTime;
    float2 gResolution;
    int    gFlags;
};
Texture2D gScene : register(t0);
SamplerState gSampler : register(s0);

float4 PS(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 color = gScene.Sample(gSampler, uv).rgb;

    const float intensity = gParams[0].x;
    const float varianceSlider = gParams[1].x;
    const float mean = gParams[2].x;
    const int signalToNoise = (int)gIntParams[0].x;
    // ReShade's Timer counts frames, so its salt is per frame; gTime is in
    // seconds, hence the much larger default factor
    const float animation = gParams[3].x;

    // inverted luma, used to scale the variance of the grain
    const float invLuma = dot(color, float3(-1.0 / 3.0, -1.0 / 3.0, -1.0 / 3.0)) + 1.0;

    const float PI = 3.1415927;
    const float t = gTime * animation;

    // two uniform noise values from one dot product
    const float seed = dot(uv, float2(12.9898, 78.233));
    const float sine = sin(seed);
    const float cosine = cos(seed);
    float noise1 = frac(sine * 43758.5453 + t);
    const float noise2 = frac(cosine * 53758.5453 - t);

    float stn = 1.0;
    if (signalToNoise != 0) {
        stn = pow(abs(invLuma), (float)signalToNoise);
    }
    const float variance = (varianceSlider * varianceSlider) * stn;

    // Box-Muller transform
    if (noise1 < 0.0001) {
        noise1 = 0.0001; // fix log(0)
    }
    float r = sqrt(-log(noise1));
    if (noise1 < 0.0001) {
        r = PI; // PI is close enough for the noise values that land here
    }
    const float theta = (2.0 * PI) * noise2;
    const float gaussian = variance * r * cos(theta) + mean;

    const float grain = lerp(1.0 + intensity, 1.0 - intensity, gaussian);
    return float4(color * grain, 1.0f);
}