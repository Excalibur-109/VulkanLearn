static const uint OBJECT_LOCAL_TO_WORLD_OFFSET = 0;

cbuffer SceneUniforms : register(b0)
{
    column_major float4x4 sceneView;
    column_major float4x4 sceneProjection;
    column_major float4x4 sceneViewProjection;
    column_major float4x4 sceneLightViewProjection;
    float4 sceneCameraPosition;
    float4 sceneAmbientColorExposure;
};

ByteAddressBuffer ObjectBuffer : register(t0);

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

float4 LoadFloat4(ByteAddressBuffer buffer, uint byteOffset)
{
    return asfloat(buffer.Load4(byteOffset));
}

float4 TransformPointFromColumns(ByteAddressBuffer buffer, uint matrixByteOffset, float3 position)
{
    const float4 column0 = LoadFloat4(buffer, matrixByteOffset + 0);
    const float4 column1 = LoadFloat4(buffer, matrixByteOffset + 16);
    const float4 column2 = LoadFloat4(buffer, matrixByteOffset + 32);
    const float4 column3 = LoadFloat4(buffer, matrixByteOffset + 48);
    return column0 * position.x + column1 * position.y + column2 * position.z + column3;
}

ShadowVertexOutput ShadowVertexMain(ShadowVertexInput input)
{
    const uint objectBase = 0;
    const float4 worldPosition = TransformPointFromColumns(ObjectBuffer, objectBase + OBJECT_LOCAL_TO_WORLD_OFFSET, input.position);

    ShadowVertexOutput output;
    output.position = mul(sceneLightViewProjection, worldPosition);
    return output;
}

void ShadowPixelMain()
{
}
