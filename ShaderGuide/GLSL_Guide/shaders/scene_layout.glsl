#ifndef GLSL_GUIDE_SCENE_LAYOUT_GLSL
#define GLSL_GUIDE_SCENE_LAYOUT_GLSL

struct ObjectData {
    mat4 localToWorld;
    mat4 normalMatrix;
    uvec4 materialIndex;
};

struct MaterialData {
    vec4 baseColor;
    vec4 metallicRoughness;
};

layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 cameraPosition;
    vec4 ambientColorExposure;
} sceneData;

layout(set = 0, binding = 1) uniform LightUniforms {
    vec4 directionIntensity;
    vec4 colorShadowBias;
} lightData;

layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectData;

layout(std430, set = 1, binding = 1) readonly buffer MaterialBuffer {
    MaterialData materials[];
} materialData;

#endif
