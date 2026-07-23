#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0) uniform PBRUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    vec4 lightColor;
    vec4 cameraPos;
    vec4 baseColor;
    vec4 materialParams;
    mat4 lightViewProjection;
    vec4 shadowParameters;
} ubo;

layout(location = 0) out vec3 vDirection;

void main() {
    mat4 viewRotation = mat4(mat3(ubo.view));
    vec4 clipPosition = ubo.proj * viewRotation * vec4(inPosition, 1.0);
    gl_Position = clipPosition.xyww;
    vDirection = inPosition;
}
