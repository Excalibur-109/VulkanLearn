#include "Common.hlsli"
#include "SceneLayout.hlsli"
#include "PbrLighting.hlsli"

Texture2D<float> ShadowMap : register(t2, space0);
SamplerComparisonState ShadowSampler : register(s2, space0);

struct PbrVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct PbrVertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 lightClipPosition : TEXCOORD3;
    nointerpolation uint materialIndex : TEXCOORD4;
};

PbrVertexOutput VSMain(PbrVertexInput input, uint instanceId : SV_InstanceID)
{
    const uint objectBase = instanceId * OBJECT_STRIDE_BYTES;
    const float4 worldPosition = TransformPointFromColumns(ObjectBuffer, objectBase + OBJECT_LOCAL_TO_WORLD_OFFSET, input.position);

    PbrVertexOutput output;
    output.position = mul(sceneViewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(TransformVectorFromColumns(ObjectBuffer, objectBase + OBJECT_NORMAL_MATRIX_OFFSET, input.normal));
    output.uv = input.uv;
    output.lightClipPosition = mul(sceneLightViewProjection, worldPosition);
    output.materialIndex = ObjectBuffer.Load(objectBase + OBJECT_MATERIAL_INDEX_OFFSET);
    return output;
}

float SampleShadow(float4 lightClipPosition)
{
    const float3 projected = lightClipPosition.xyz / lightClipPosition.w;
    const float2 shadowUv = float2(projected.x * 0.5 + 0.5, -projected.y * 0.5 + 0.5);

    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
        shadowUv.y < 0.0 || shadowUv.y > 1.0 ||
        projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }

    const float bias = lightColorShadowBias.w;
    return ShadowMap.SampleCmpLevelZero(ShadowSampler, shadowUv, projected.z - bias);
}

float4 PSMain(PbrVertexOutput input) : SV_Target0
{
    const uint materialBase = input.materialIndex * MATERIAL_STRIDE_BYTES;
    const float4 materialBaseColor = LoadFloat4(MaterialBuffer, materialBase + 0);
    const float4 materialMetallicRoughness = LoadFloat4(MaterialBuffer, materialBase + 16);

    const float3 baseColor = SrgbToLinear(materialBaseColor.rgb);
    const float metallic = SaturateFloat(materialMetallicRoughness.x);
    const float roughness = clamp(materialMetallicRoughness.y, 0.04, 1.0);

    const float3 normal = normalize(input.normal);
    const float3 viewDirection = normalize(sceneCameraPosition.xyz - input.worldPosition);
    const float3 lightDirection = normalize(-lightDirectionIntensity.xyz);
    const float3 radiance = lightColorShadowBias.rgb * lightDirectionIntensity.w;

    const float shadow = SampleShadow(input.lightClipPosition);
    const float3 directLight = EvaluateDirectionalPbr(baseColor, metallic, roughness, normal, viewDirection, lightDirection, radiance) * shadow;
    const float3 ambient = sceneAmbientColorExposure.rgb * baseColor;

    float3 color = (ambient + directLight) * sceneAmbientColorExposure.w;
    color = LinearToSrgb(ReinhardToneMap(color));
    return float4(color, materialBaseColor.a);
}
