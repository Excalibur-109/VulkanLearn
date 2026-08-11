#version 460

layout(push_constant) uniform PBRConstants
{
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 baseColorMetallic;
    vec4 roughnessOcclusionIntensity;
    vec4 lightDirection;
} constants;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 normal;

void main()
{
    gl_Position = constants.viewProjection * vec4(inPosition, 1.0);
    worldPosition = inPosition;
    normal = normalize(inNormal);
}
