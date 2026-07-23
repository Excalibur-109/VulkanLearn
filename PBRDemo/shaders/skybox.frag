#version 450

layout(set = 0, binding = 2) uniform samplerCube skyboxTexture;

layout(location = 0) in vec3 vDirection;
layout(location = 0) out vec4 outColor;

void main() {
    vec3 color = texture(skyboxTexture, normalize(vDirection)).rgb;
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = max(mix(vec3(luminance), color, 1.18) * 1.18, vec3(0.0));
    outColor = vec4(color, 1.0);
}
