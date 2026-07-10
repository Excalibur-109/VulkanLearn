#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

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
    uvec4 materialIndex;
};

layout(std430, set = 1, binding = 0) readonly buffer ObjectBuffer {
    ObjectData objects[];
} objectData;

void main() {
    ObjectData objectInfo = objectData.objects[gl_InstanceIndex];
    gl_Position = sceneData.lightViewProjection * objectInfo.localToWorld * vec4(inPosition, 1.0);
}
