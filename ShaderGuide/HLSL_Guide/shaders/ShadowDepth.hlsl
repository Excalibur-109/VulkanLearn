#include "SceneLayout.hlsli"

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

ShadowVertexOutput ShadowVS(ShadowVertexInput input, uint instanceId : SV_InstanceID)
{
    const uint objectBase = instanceId * OBJECT_STRIDE_BYTES;
    const float4 worldPosition = TransformPointFromColumns(ObjectBuffer, objectBase + OBJECT_LOCAL_TO_WORLD_OFFSET, input.position);

    ShadowVertexOutput output;
    output.position = mul(sceneLightViewProjection, worldPosition);
    return output;
}

void ShadowPS()
{
    // Depth-only pass 不需要写颜色。固定管线会写入 depth target。
}
