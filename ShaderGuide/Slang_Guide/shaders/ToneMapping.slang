#include "Common.slang"

Texture2D<float4> HdrColor : register(t0, space0);
SamplerState LinearClampSampler : register(s0, space0);

cbuffer ToneMappingUniforms : register(b0, space0)
{
    float exposure;
    float gamma;
    float2 padding;
};

struct FullscreenVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("fragment")]
float4 PSMain(FullscreenVertexOutput input) : SV_Target0
{
    float3 hdr = HdrColor.Sample(LinearClampSampler, input.uv).rgb * exposure;
    float3 mapped = ReinhardToneMap(hdr);
    float3 srgb = pow(max(mapped, float3(0.0, 0.0, 0.0)), float3(1.0 / max(gamma, 0.001), 1.0 / max(gamma, 0.001), 1.0 / max(gamma, 0.001)));
    return float4(srgb, 1.0);
}
