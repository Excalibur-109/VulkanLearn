#version 450

// =============================================================================
// PBR Demo · Vertex Shader
//
// 顶点格式：position(vec3) + normal(vec3) + uv(vec2) = 32 字节 stride
//   layout(location = 0) in vec3 inPosition;
//   layout(location = 1) in vec3 inNormal;
//   layout(location = 2) in vec2 inUV;
//
// 单一 UBO（set=0, binding=0）携带 MVP、方向光、相机位置和材质参数。
// =============================================================================

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform PBRUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;       // xyz=世界空间方向（指向光源）
    vec4 lightColor;     // rgb=光强，a=unused
    vec4 cameraPos;      // xyz=世界空间相机位置
    vec4 baseColor;      // rgb=albedo，a=metallic
    vec4 materialParams; // x=roughness, y=ao, zw=unused
} ubo;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vWorldNormal;
layout(location = 2) out vec2 vUV;

void main() {
    vec4 worldPosition = ubo.model * vec4(inPosition, 1.0);
    vWorldPos = worldPosition.xyz;

    // 假设 model 矩阵只有旋转/平移/均匀缩放，法线变换用 mat3 即可；
    // 真实工程里应使用法线矩阵 transpose(inverse(mat3(model)))，这里为了 demo 简洁省略。
    vWorldNormal = mat3(ubo.model) * inNormal;

    vUV = inUV;

    gl_Position = ubo.proj * ubo.view * worldPosition;
}
