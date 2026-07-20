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
// 单方向光 + 方向光 Shadow Map（3x3 PCF），无 env map。
// 直接光受阴影影响，环境光不乘阴影可见度，避免遮挡区完全变黑。
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
    mat4 lightViewProjection;
    vec4 shadowParameters;
} ubo;

// A comparison sampler returns visibility instead of raw depth: 1 means the
// receiver is no farther from the light than the closest stored caster.
layout(set = 0, binding = 1) uniform sampler2DShadow shadowMap;

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

float shadowVisibility(vec3 worldPosition, vec3 normal, vec3 lightVector) {
    vec4 lightClip = ubo.lightViewProjection * vec4(worldPosition, 1.0);
    if (lightClip.w <= 0.0) {
        return 1.0;
    }

    vec3 lightNdc = lightClip.xyz / lightClip.w;
    vec2 shadowUV = lightNdc.xy * 0.5 + 0.5;
    if (shadowUV.x <= 0.0 || shadowUV.x >= 1.0 ||
        shadowUV.y <= 0.0 || shadowUV.y >= 1.0 ||
        lightNdc.z <= 0.0 || lightNdc.z >= 1.0) {
        return 1.0;
    }

    // Grazing surfaces need more bias because one texel spans a larger depth
    // interval. This removes acne while preserving the contact shadow.
    float normalFactor = 1.0 - max(dot(normal, lightVector), 0.0);
    float bias = max(
        ubo.shadowParameters.z,
        ubo.shadowParameters.w * normalFactor);

    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(x, y) * ubo.shadowParameters.xy;
            visibility += texture(
                shadowMap,
                vec3(shadowUV + offset, lightNdc.z - bias));
        }
    }
    return visibility / 9.0;
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

    float shadow = shadowVisibility(vWorldPos, N, L);
    vec3 directLight =
        (diffuse + specular) * ubo.lightColor.rgb * NoL * shadow;

    // 简化环境光：金属用 f0，非金属用 albedo
    vec3 ambient = mix(vec3(0.03) * albedo, f0, metallic) * ao;

    // 简单 gamma correction（sRGB framebuffer 会再做硬件 gamma）
    vec3 finalColor = ambient + directLight;
    outColor = vec4(finalColor, 1.0);
}
