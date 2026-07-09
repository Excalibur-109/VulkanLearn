#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUv;
layout(location = 3) out vec4 outLightClipPosition;
layout(location = 4) flat out uint outMaterialIndex;

layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 cameraPosition;
    vec4 ambientColorExposure;
} sceneData;

struct ObjectData {
    mat4 localToWorld;
    mat4 normalMatrix;
    uint materialIndex;
    vec3 padding0;
};

layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectData;

void main() {
    ObjectData objectInfo = objectData.objects[gl_InstanceIndex];
    vec4 worldPosition = objectInfo.localToWorld * vec4(inPosition, 1.0);

    outWorldPosition = worldPosition.xyz;
    outNormal = normalize((objectInfo.normalMatrix * vec4(inNormal, 0.0)).xyz);
    outUv = inUv;
    outLightClipPosition = sceneData.lightViewProjection * worldPosition;
    outMaterialIndex = objectInfo.materialIndex;

    gl_Position = sceneData.viewProjection * worldPosition;
}
