#include "Common.slang"
#include "SceneLayout.slang"
#include "PbrLighting.slang"

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

[shader("vertex")]
PbrVertexOutput VSMain(PbrVertexInput input, uint instanceId : SV_InstanceID)
{
    ObjectData objectInfo = ObjectBuffer[instanceId];
    const float4 worldPosition = mul(objectInfo.localToWorld, float4(input.position, 1.0));

    PbrVertexOutput output;
    output.position = mul(sceneViewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(mul(objectInfo.normalMatrix, float4(input.normal, 0.0)).xyz);
    output.uv = input.uv;
    output.lightClipPosition = mul(sceneLightViewProjection, worldPosition);
    output.materialIndex = objectInfo.materialIndex.x;
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

[shader("fragment")]
float4 PSMain(PbrVertexOutput input) : SV_Target0
{
    MaterialData material = MaterialBuffer[input.materialIndex];
    const float3 baseColor = SrgbToLinear(material.baseColor.rgb);
    const float metallic = SaturateFloat(material.metallicRoughness.x);
    const float roughness = clamp(material.metallicRoughness.y, 0.04, 1.0);

    const float3 normal = normalize(input.normal);
    const float3 viewDirection = normalize(sceneCameraPosition.xyz - input.worldPosition);
    const float3 lightDirection = normalize(-lightDirectionIntensity.xyz);
    const float3 radiance = lightColorShadowBias.rgb * lightDirectionIntensity.w;

    const float shadow = SampleShadow(input.lightClipPosition);
    const float3 directLight = EvaluateDirectionalPbr(baseColor, metallic, roughness, normal, viewDirection, lightDirection, radiance) * shadow;
    const float3 ambient = sceneAmbientColorExposure.rgb * baseColor;

    float3 color = (ambient + directLight) * sceneAmbientColorExposure.w;
    color = LinearToSrgb(ReinhardToneMap(color));
    return float4(color, material.baseColor.a);
}
