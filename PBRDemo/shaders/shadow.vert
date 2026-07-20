#version 450

// =============================================================================
// 方向光 Shadow Map · Depth-only Vertex Shader
//
// Shadow Mapping 分为两个阶段：
//   1. 从光源视角绘制 caster，把每个 texel 最近的深度写入 D32 Shadow Map；
//   2. 主 PBR Pass 把 receiver 重新变换到光源空间，与该深度比较。
//
// 本 Pass 没有 color attachment，也没有 Fragment Shader。顶点输出 gl_Position 后，
// 光栅器和深度测试固定功能会自动插值并写入最接近光源的深度值。
// =============================================================================

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
    // local -> world：应用球体自己的平移/旋转。
    vec4 worldPosition = ubo.model * vec4(inPosition, 1.0);
    // world -> light clip：不是主相机的 projection * view，而是光源正交相机。
    gl_Position = ubo.lightViewProjection * worldPosition;
}
