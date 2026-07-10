#include "SceneLayout.slang"

struct ShadowVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct ShadowVertexOutput
{
    float4 position : SV_Position;
};

[shader("vertex")]
ShadowVertexOutput ShadowVS(ShadowVertexInput input, uint instanceId : SV_InstanceID)
{
    ObjectData objectInfo = ObjectBuffer[instanceId];
    const float4 worldPosition = mul(objectInfo.localToWorld, float4(input.position, 1.0));

    ShadowVertexOutput output;
    output.position = mul(sceneLightViewProjection, worldPosition);
    return output;
}

[shader("fragment")]
void ShadowPS()
{
    // Depth-only pass 不需要写颜色。固定管线会写入 depth target。
}
