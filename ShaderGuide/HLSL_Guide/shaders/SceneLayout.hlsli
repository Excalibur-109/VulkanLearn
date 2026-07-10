#ifndef HLSL_GUIDE_SCENE_LAYOUT_HLSLI
#define HLSL_GUIDE_SCENE_LAYOUT_HLSLI

static const uint OBJECT_LOCAL_TO_WORLD_OFFSET = 0;
static const uint OBJECT_NORMAL_MATRIX_OFFSET = 64;
static const uint OBJECT_MATERIAL_INDEX_OFFSET = 128;
static const uint OBJECT_STRIDE_BYTES = 144;
static const uint MATERIAL_STRIDE_BYTES = 32;

cbuffer SceneUniforms : register(b0, space0)
{
    column_major float4x4 sceneView;
    column_major float4x4 sceneProjection;
    column_major float4x4 sceneViewProjection;
    column_major float4x4 sceneLightViewProjection;
    float4 sceneCameraPosition;
    float4 sceneAmbientColorExposure;
};

cbuffer LightUniforms : register(b1, space0)
{
    float4 lightDirectionIntensity;
    float4 lightColorShadowBias;
};

ByteAddressBuffer ObjectBuffer : register(t0, space1);
ByteAddressBuffer MaterialBuffer : register(t1, space1);

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

float3 TransformVectorFromColumns(ByteAddressBuffer buffer, uint matrixByteOffset, float3 vectorValue)
{
    const float4 column0 = LoadFloat4(buffer, matrixByteOffset + 0);
    const float4 column1 = LoadFloat4(buffer, matrixByteOffset + 16);
    const float4 column2 = LoadFloat4(buffer, matrixByteOffset + 32);
    return column0.xyz * vectorValue.x + column1.xyz * vectorValue.y + column2.xyz * vectorValue.z;
}

#endif
