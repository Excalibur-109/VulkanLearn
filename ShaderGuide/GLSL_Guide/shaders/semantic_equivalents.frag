#version 450

// GLSL 片元输入用 location 接收顶点 shader 的输出。
layout(location = 0) in vec2 inUv;
layout(location = 1) in vec3 inNormal;

// HLSL: float4 PSMain(...) : SV_Target0
// GLSL: 声明第 0 个 color attachment 输出。
layout(location = 0) out vec4 outColor;

void main() {
    vec3 normalColor = normalize(inNormal) * 0.5 + 0.5;
    outColor = vec4(normalColor * vec3(inUv, 1.0), 1.0);
}
