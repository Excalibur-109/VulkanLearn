// =============================================================================
// PBRDemo · D3D11/D3D12 共用 HLSL
//
// RHI 与 HLSL 的绑定契约：
//   b0         : 每个物体自己的常量缓冲（矩阵、光照、材质）
//   t1 + s1    : Shadow Map SRV + 深度比较 Sampler
//   POSITION0  : float3 顶点位置
//   NORMAL0    : float3 顶点法线
//   TEXCOORD0  : float2 UV
//
// cbuffer 字段顺序必须和 main.cpp 的 UniformBufferObject 完全一致，不能只按名称匹配。
// C++ Math 在 CPU 使用行主序；main.cpp 在写入常量缓冲前显式转置，因此这里继续按
// column_major 读取，并采用 mul(matrix, vector)，与 GLSL 的 matrix * vector 保持一致。
// =============================================================================

static const float PI = 3.14159265359F;

cbuffer PBRConstants : register(b0) {
    column_major float4x4 model;
    column_major float4x4 view;
    column_major float4x4 projection;
    float4 lightDirection;
    float4 lightColor;
    float4 cameraPosition;
    float4 baseColor;
    float4 materialParameters;
    // 世界空间 -> 光源裁剪空间，生成和查询 Shadow Map 都使用同一个矩阵。
    column_major float4x4 lightViewProjection;
    // xy=1/ShadowMapSize，z=最小 bias，w=法线斜率 bias。
    float4 shadowParameters;
};

// D3D 将纹理和采样状态分成 t/s 两类寄存器。RHI 的 CombinedTextureSampler
// 在 D3D 后端会拆成 t1 和 s1，在 Vulkan 后端则对应一个 combined descriptor。
Texture2D<float> shadowMap : register(t1);
SamplerComparisonState shadowSampler : register(s1);

struct VertexInput {
    float3 position : POSITION0;
    float3 normal   : NORMAL0;
    float2 uv       : TEXCOORD0;
};

struct VertexOutput {
    // SV_POSITION 是交给光栅器的最终裁剪空间位置。
    float4 clipPosition : SV_POSITION;
    // TEXCOORDn 在这里是 VS -> PS 的通用插值通道，不只表示模型 UV。
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal   : TEXCOORD1;
    float2 uv            : TEXCOORD2;
};

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    const float4 worldPosition = mul(model, float4(input.position, 1.0F));
    output.clipPosition = mul(projection, mul(view, worldPosition));
    output.worldPosition = worldPosition.xyz;

    // Demo 的 model 只有旋转和平移；若存在非均匀缩放，必须改用
    // transpose(inverse((float3x3)model)) 变换法线。
    output.worldNormal = mul((float3x3)model, input.normal);
    output.uv = input.uv;
    return output;
}

struct ShadowVertexInput {
    float3 position : POSITION0;
};

// Shadow Pass 没有 Pixel Shader：SV_POSITION 经光栅化后直接写入深度附件。
float4 ShadowVSMain(ShadowVertexInput input) : SV_POSITION {
    const float4 worldPosition = mul(model, float4(input.position, 1.0F));
    return mul(lightViewProjection, worldPosition);
}

// Trowbridge-Reitz GGX normal distribution function.
float DistributionGGX(float NoH, float alphaSquared) {
    const float denominator =
        NoH * NoH * (alphaSquared - 1.0F) + 1.0F;
    return alphaSquared / (PI * denominator * denominator);
}

// Height-correlated Smith visibility using the Schlick-GGX form.
float VisibilitySmithGGX(float NoV, float NoL, float alphaSquared) {
    const float visibilityV =
        NoL * sqrt(NoV * NoV * (1.0F - alphaSquared) + alphaSquared);
    const float visibilityL =
        NoV * sqrt(NoL * NoL * (1.0F - alphaSquared) + alphaSquared);
    return 0.5F / (visibilityV + visibilityL + 1.0e-5F);
}

float3 FresnelSchlick(float VoH, float3 f0) {
    const float factor = pow(saturate(1.0F - VoH), 5.0F);
    return f0 + (1.0F.xxx - f0) * factor;
}

float ShadowVisibility(float3 worldPosition, float3 normal, float3 lightVector) {
    // 步骤 1：把主相机片元重新投影到光源裁剪空间。
    const float4 lightClip = mul(
        lightViewProjection,
        float4(worldPosition, 1.0F));
    if (lightClip.w <= 0.0F) {
        return 1.0F;
    }

    // 步骤 2：透视除法得到 NDC。启用 GLM_FORCE_DEPTH_ZERO_TO_ONE 后，z 已是 [0,1]。
    const float3 lightNdc = lightClip.xyz / lightClip.w;
    // D3D viewport 把 NDC +Y 映射到纹理顶部，而纹理 V 向下增长，所以 Y 需要取反。
    // Vulkan 版本已在 CPU 的 lightProjection 中完成等价翻转，GLSL 不在这里再翻一次。
    const float2 shadowUV = float2(
        lightNdc.x * 0.5F + 0.5F,
        -lightNdc.y * 0.5F + 0.5F);
    if (shadowUV.x <= 0.0F || shadowUV.x >= 1.0F ||
        shadowUV.y <= 0.0F || shadowUV.y >= 1.0F ||
        lightNdc.z <= 0.0F || lightNdc.z >= 1.0F) {
        return 1.0F;
    }

    // 步骤 3：斜面使用更大 bias，降低有限深度精度造成的 self-shadow acne。
    // currentDepth - bias 会把 receiver 的比较位置轻微拉向光源；bias 过大则会让
    // 阴影离开物体接触点，形成 Peter-panning。
    const float normalFactor = 1.0F - max(dot(normal, lightVector), 0.0F);
    const float bias = max(
        shadowParameters.z,
        shadowParameters.w * normalFactor);

    // 步骤 4：在 3x3 邻域执行 9 次 Percentage-Closer Filtering。
    // SampleCmpLevelZero 以第三个参数为 referenceDepth，使用 SamplerComparisonState
    // 比较 referenceDepth <= storedDepth，并返回可见度而不是原始深度。
    // [unroll] 建议编译器展开固定小循环，减少循环控制开销，不改变计算结果。
    float visibility = 0.0F;
    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            const float2 offset = float2(x, y) * shadowParameters.xy;
            visibility += shadowMap.SampleCmpLevelZero(
                shadowSampler,
                shadowUV + offset,
                lightNdc.z - bias);
        }
    }
    return visibility / 9.0F;
}

float4 PSMain(VertexOutput input) : SV_TARGET0 {
    const float3 N = normalize(input.worldNormal);
    const float3 V = normalize(cameraPosition.xyz - input.worldPosition);
    const float3 L = normalize(-lightDirection.xyz);
    const float3 H = normalize(V + L);

    const float NoL = max(dot(N, L), 0.0F);
    const float NoV = max(dot(N, V), 1.0e-4F);
    const float NoH = max(dot(N, H), 0.0F);
    const float VoH = max(dot(V, H), 0.0F);

    const float3 albedo = baseColor.rgb;
    const float metallic = baseColor.a;
    const float roughness = clamp(materialParameters.x, 0.045F, 1.0F);
    const float ambientOcclusion = materialParameters.y;

    const float alpha = roughness * roughness;
    const float alphaSquared = max(alpha * alpha, 1.0e-5F);
    const float3 f0 = lerp(0.04F.xxx, albedo, metallic);

    const float distribution = DistributionGGX(NoH, alphaSquared);
    const float visibility = VisibilitySmithGGX(NoV, NoL, alphaSquared);
    const float3 fresnel = FresnelSchlick(VoH, f0);
    const float3 specular = distribution * visibility * fresnel;
    const float3 diffuse =
        (1.0F.xxx - fresnel) * (1.0F - metallic) * albedo / PI;

    // 只遮蔽当前方向光的直接照明；环境光模拟间接照明，不应一起变成全黑。
    const float shadow = ShadowVisibility(input.worldPosition, N, L);
    const float3 directLight =
        (diffuse + specular) * lightColor.rgb * NoL * shadow;
    const float3 ambient =
        lerp(0.03F * albedo, f0, metallic) * ambientOcclusion;
    return float4(ambient + directLight, 1.0F);
}
