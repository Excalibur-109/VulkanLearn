#ifndef HLSL_GUIDE_COMMON_HLSLI
#define HLSL_GUIDE_COMMON_HLSLI

static const float PI = 3.14159265359;
static const float EPSILON = 0.0001;

float SaturateFloat(float value)
{
    return clamp(value, 0.0, 1.0);
}

float3 SrgbToLinear(float3 color)
{
    return pow(max(color, float3(0.0, 0.0, 0.0)), float3(2.2, 2.2, 2.2));
}

float3 LinearToSrgb(float3 color)
{
    return pow(max(color, float3(0.0, 0.0, 0.0)), float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
}

float3 ReinhardToneMap(float3 color)
{
    return color / (color + float3(1.0, 1.0, 1.0));
}

#endif
