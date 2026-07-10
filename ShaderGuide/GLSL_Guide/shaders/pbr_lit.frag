#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"
#include "scene_layout.glsl"
#include "pbr_lighting.glsl"

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inLightClipPosition;
layout(location = 4) flat in uint inMaterialIndex;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap;

float sampleShadow(vec4 lightClipPosition) {
    vec3 projected = lightClipPosition.xyz / lightClipPosition.w;
    projected.xy = projected.xy * 0.5 + 0.5;

    if (projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0 ||
        projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }

    float bias = lightData.colorShadowBias.w;
    return texture(shadowMap, vec3(projected.xy, projected.z - bias));
}

void main() {
    MaterialData material = materialData.materials[inMaterialIndex];
    vec3 baseColor = srgbToLinear(material.baseColor.rgb);
    float metallic = saturate(material.metallicRoughness.x);
    float roughness = clamp(material.metallicRoughness.y, 0.04, 1.0);

    vec3 normal = normalize(inNormal);
    vec3 viewDirection = normalize(sceneData.cameraPosition.xyz - inWorldPosition);
    vec3 lightDirection = normalize(-lightData.directionIntensity.xyz);
    vec3 radiance = lightData.colorShadowBias.rgb * lightData.directionIntensity.w;

    float shadow = sampleShadow(inLightClipPosition);
    vec3 directLight = evaluateDirectionalPbr(baseColor, metallic, roughness, normal, viewDirection, lightDirection, radiance) * shadow;
    vec3 ambient = sceneData.ambientColorExposure.rgb * baseColor;

    vec3 color = (ambient + directLight) * sceneData.ambientColorExposure.w;
    color = linearToSrgb(reinhardToneMap(color));
    outColor = vec4(color, material.baseColor.a);
}
