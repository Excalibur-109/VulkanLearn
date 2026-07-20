#version 450

// Directional-light shadow map vertex shader.
//
// A shadow map renders the scene from the light's point of view. This pass has
// no color attachment and no fragment shader: rasterization writes only the
// nearest post-projection Z value into a D32 depth texture.

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

void main() {
    vec4 worldPosition = ubo.model * vec4(inPosition, 1.0);
    gl_Position = ubo.lightViewProjection * worldPosition;
}
