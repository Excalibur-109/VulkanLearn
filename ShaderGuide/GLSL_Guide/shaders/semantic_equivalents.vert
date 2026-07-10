#version 450

// GLSL 没有 POSITION/NORMAL/TEXCOORD0 这种语义名。
// 它用 location 和 C++ 顶点输入布局对齐：
// HLSL: float3 position : POSITION;  -> GLSL: layout(location = 0) in vec3 inPosition;
// HLSL: float3 normal   : NORMAL;    -> GLSL: layout(location = 1) in vec3 inNormal;
// HLSL: float2 uv       : TEXCOORD0; -> GLSL: layout(location = 2) in vec2 inUv;
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;

// HLSL: float2 uv : TEXCOORD0;
// GLSL: 自定义阶段变量继续用 location 配对。
layout(location = 0) out vec2 outUv;
layout(location = 1) out vec3 outNormal;

layout(set = 0, binding = 0) uniform CameraUniforms {
    mat4 viewProjection;
} cameraData;

void main() {
    outUv = inUv;
    outNormal = inNormal;

    // HLSL: output.position : SV_Position
    // GLSL: 直接写内建变量 gl_Position。
    gl_Position = cameraData.viewProjection * vec4(inPosition, 1.0);
}
