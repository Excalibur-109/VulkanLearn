#version 450
#extension GL_GOOGLE_include_directive : require

#include "scene_layout.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outUv;
layout(location = 3) out vec4 outLightClipPosition;
layout(location = 4) flat out uint outMaterialIndex;

void main() {
    ObjectData objectInfo = objectData.objects[gl_InstanceIndex];
    vec4 worldPosition = objectInfo.localToWorld * vec4(inPosition, 1.0);

    outWorldPosition = worldPosition.xyz;
    outNormal = normalize((objectInfo.normalMatrix * vec4(inNormal, 0.0)).xyz);
    outUv = inUv;
    outLightClipPosition = sceneData.lightViewProjection * worldPosition;
    outMaterialIndex = objectInfo.materialIndex.x;

    gl_Position = sceneData.viewProjection * worldPosition;
}
