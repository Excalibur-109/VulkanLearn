#ifndef SLANG_GUIDE_SCENE_LAYOUT
#define SLANG_GUIDE_SCENE_LAYOUT

struct ObjectData
{
    float4x4 localToWorld;
    float4x4 normalMatrix;
    uint4 materialIndex;
};

struct MaterialData
{
    float4 baseColor;
    float4 metallicRoughness;
};

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

StructuredBuffer<ObjectData> ObjectBuffer : register(t0, space1);
StructuredBuffer<MaterialData> MaterialBuffer : register(t1, space1);

#endif
