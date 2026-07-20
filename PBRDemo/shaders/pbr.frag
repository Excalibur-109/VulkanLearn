#version 450

// =============================================================================
// PBR Demo · Fragment Shader
//
// 简化版 Cook-Torrance PBR（不依赖纹理，仅材质常数）：
//   - 法线分布 D：GGX / Trowbridge-Reitz
//   - 几何遮蔽 G：Smith Schlick-GGX
//   - 菲涅尔 F：Schlick 近似
//   - 漫反射：Lambert (1 - F) * (1 - metallic) * albedo / PI
//   - 环境光：半球常数 f0 + albedo 混合（取代完整 IBL，方便 demo 跑通）
//
// 单方向光，无阴影、无 env map，足够展示 PBR 各项同性公式。
// =============================================================================

layout(set = 0, binding = 0) uniform PBRUBO {
    mat4 model;
    mat4 view;
    mat4 proj;
    vec4 lightDir;
    vec4 lightColor;
    vec4 cameraPos;
    vec4 baseColor;
    vec4 materialParams;
} ubo;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vWorldNormal;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// Trowbridge-Reitz / GGX 法线分布函数
float D_GGX(float NoH, float a) {
    float a2 = a * a;
    float f = (NoH * NoH) * (a2 - 1.0) + 1.0;
    return a2 / (PI * f * f);
}

// Smith Schlick-GGX 几何遮蔽（visibility 形式）
float V_SmithGGX(float NoV, float NoL, float a) {
    float a2 = a * a;
    float vV = NoL * sqrt(NoV * NoV * (1.0 - a2) + a2);
    float vL = NoV * sqrt(NoL * NoL * (1.0 - a2) + a2);
    return 0.5 / (vV + vL + 1e-5);
}

// Schlick Fresnel 近似
vec3 F_Schlick(float VoH, vec3 f0) {
    float f = pow(clamp(1.0 - VoH, 0.0, 1.0), 5.0);
    return f0 + (vec3(1.0) - f0) * f;
}

void main() {
    vec3 N = normalize(vWorldNormal);
    vec3 V = normalize(ubo.cameraPos.xyz - vWorldPos);
    vec3 L = normalize(-ubo.lightDir.xyz);
    vec3 H = normalize(V + L);

    float NoL = max(dot(N, L), 0.0);
    float NoV = max(dot(N, V), 1e-4);
    float NoH = max(dot(N, H), 0.0);
    float VoH = max(dot(V, H), 0.0);

    vec3  albedo    = ubo.baseColor.rgb;
    float metallic  = ubo.baseColor.a;
    float roughness = clamp(ubo.materialParams.x, 0.045, 1.0);
    float ao        = ubo.materialParams.y;

    // 物理上 roughness 平方是 α；这里加下限避免 NoH→0 时数值爆炸
    float a  = roughness * roughness;
    float a2 = max(a * a, 1e-5);

    // 非金属 f0 ≈ 0.04，金属 f0 = albedo
    vec3 f0 = mix(vec3(0.04), albedo, metallic);

    // 直接光照：specular + diffuse
    float  D   = D_GGX(NoH, a2);
    float  vis = V_SmithGGX(NoV, NoL, a2);
    vec3   F   = F_Schlick(VoH, f0);
    vec3   specular = D * vis * F;
    vec3   diffuse  = (vec3(1.0) - F) * (1.0 - metallic) * albedo / PI;

    vec3 directLight = (diffuse + specular) * ubo.lightColor.rgb * NoL;

    // 简化环境光：金属用 f0，非金属用 albedo
    vec3 ambient = mix(vec3(0.03) * albedo, f0, metallic) * ao;

    // 简单 gamma correction（sRGB framebuffer 会再做硬件 gamma）
    vec3 finalColor = ambient + directLight;
    outColor = vec4(finalColor, 1.0);
}
