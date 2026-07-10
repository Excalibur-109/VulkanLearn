#version 450
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) in vec2 inUv;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D hdrColor;

layout(set = 0, binding = 1) uniform ToneMappingUniforms {
    float exposure;
    float gamma;
    vec2 padding;
} toneData;

void main() {
    vec3 hdr = texture(hdrColor, inUv).rgb * toneData.exposure;
    vec3 mapped = reinhardToneMap(hdr);
    vec3 srgb = pow(max(mapped, vec3(0.0)), vec3(1.0 / max(toneData.gamma, 0.001)));
    outColor = vec4(srgb, 1.0);
}
