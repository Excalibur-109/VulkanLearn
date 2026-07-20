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

// sampler2DShadow 是“深度比较采样器”，不是普通 sampler2D。
// texture(shadowMap, vec3(uv, referenceDepth)) 会比较 referenceDepth 与纹理深度：
// 返回 1 表示当前 receiver 比 Shadow Map 中最近的 caster 更靠近光源，即可见；
// 返回 0 表示中间存在更靠近光源的几何体，即当前片元处于阴影中。
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
    // 步骤 1：把主相机看到的世界空间片元重新投影到光源相机。
    // Shadow Map 是从光源视角生成的，所以比较双方必须处在同一坐标系。
    vec4 lightClip = ubo.lightViewProjection * vec4(worldPosition, 1.0);
    // 正交方向光通常 w=1；保留检查可以防止以后改成透视聚光灯时采样相机背后区域。
    if (lightClip.w <= 0.0) {
        return 1.0;
    }

    // 步骤 2：透视除法得到 NDC。GLM_FORCE_DEPTH_ZERO_TO_ONE 让 z 已处于 [0,1]；
    // x/y 仍处于 [-1,1]，因此乘 0.5 再加 0.5 映射到纹理 UV [0,1]。
    // Vulkan 的 Y 翻转已经包含在 CPU 生成的 lightProjection 中。
    vec3 lightNdc = lightClip.xyz / lightClip.w;
    vec2 shadowUV = lightNdc.xy * 0.5 + 0.5;
    // 光源正交盒外没有可靠的 Shadow Map 数据。当前 Demo 将它视为受光区域，
    // 避免对边界外坐标继续做 PCF 采样。
    if (shadowUV.x <= 0.0 || shadowUV.x >= 1.0 ||
        shadowUV.y <= 0.0 || shadowUV.y >= 1.0 ||
        lightNdc.z <= 0.0 || lightNdc.z >= 1.0) {
        return 1.0;
    }

    // 步骤 3：计算法线相关 bias。
    // N 与 L 越垂直（掠射角越大），一个 texel 覆盖的深度变化越大，需要更大偏移。
    // 比较时使用 currentDepth - bias，相当于把 receiver 稍微拉向光源，减少自阴影痤疮。
    // max 保证正对光源的表面也至少使用最小偏移。
    float normalFactor = 1.0 - max(dot(normal, lightVector), 0.0);
    float bias = max(
        ubo.shadowParameters.z,
        ubo.shadowParameters.w * normalFactor);

    // 步骤 4：3x3 Percentage-Closer Filtering（PCF）。
    // 以当前 UV 为中心，在周围 9 个 texel 分别执行深度比较，再求平均：
    //   0/9 表示全部被遮挡，9/9 表示全部可见，中间值形成阴影软边。
    // 这里过滤的是“比较结果”而不是原始深度，直接平均深度会产生错误遮挡。
    float visibility = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            vec2 offset = vec2(x, y) * ubo.shadowParameters.xy;
            visibility += texture(
                shadowMap,
                // sampler2DShadow 的第三个分量不是普通纹理坐标，而是比较参考深度。
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

    // 阴影只衰减来自当前方向光的 directLight。ambient 代表间接环境光，即使太阳
    // 被球挡住，周围环境仍会给 Plane 少量照明，因此不能一起乘 shadow。
    float shadow = shadowVisibility(vWorldPos, N, L);
    vec3 directLight =
        (diffuse + specular) * ubo.lightColor.rgb * NoL * shadow;

    // 简化环境光：金属用 f0，非金属用 albedo
    vec3 ambient = mix(vec3(0.03) * albedo, f0, metallic) * ao;

    // 简单 gamma correction（sRGB framebuffer 会再做硬件 gamma）
    vec3 finalColor = ambient + directLight;
    outColor = vec4(finalColor, 1.0);
}
