// =============================================================================
// PBRDemo shared HLSL for Direct3D 11 and Direct3D 12
//
// RHI binding contract:
//   b0         : one per-object constant buffer
//   t1 + s1    : shadow depth texture + comparison sampler
//   POSITION0  : float3 position
//   NORMAL0    : float3 normal
//   TEXCOORD0  : float2 UV
//
// The cbuffer field order exactly matches UniformBufferObject in main.cpp.
// Matrices are explicitly column-major because GLM uploads column-major matrices,
// and mul(matrix, vector) then matches the GLSL expression "matrix * vector".
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
    column_major float4x4 lightViewProjection;
    float4 shadowParameters;
};

Texture2D<float> shadowMap : register(t1);
SamplerComparisonState shadowSampler : register(s1);

struct VertexInput {
    float3 position : POSITION0;
    float3 normal   : NORMAL0;
    float2 uv       : TEXCOORD0;
};

struct VertexOutput {
    // SV_POSITION is the final clip-space position consumed by the rasterizer.
    float4 clipPosition : SV_POSITION;
    // TEXCOORD semantics are generic interpolator channels between VS and PS.
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal   : TEXCOORD1;
    float2 uv            : TEXCOORD2;
};

VertexOutput VSMain(VertexInput input) {
    VertexOutput output;
    const float4 worldPosition = mul(model, float4(input.position, 1.0F));
    output.clipPosition = mul(projection, mul(view, worldPosition));
    output.worldPosition = worldPosition.xyz;

    // The demo model matrix contains only rotation and translation. A production
    // shader must use transpose(inverse((float3x3)model)) for non-uniform scale.
    output.worldNormal = mul((float3x3)model, input.normal);
    output.uv = input.uv;
    return output;
}

struct ShadowVertexInput {
    float3 position : POSITION0;
};

// The shadow pass has no pixel shader and writes only rasterized depth.
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
    const float4 lightClip = mul(
        lightViewProjection,
        float4(worldPosition, 1.0F));
    if (lightClip.w <= 0.0F) {
        return 1.0F;
    }

    const float3 lightNdc = lightClip.xyz / lightClip.w;
    // D3D maps NDC +Y toward the top of a viewport. Texture V grows downward,
    // while Vulkan performs the equivalent Y correction in its projection.
    const float2 shadowUV = float2(
        lightNdc.x * 0.5F + 0.5F,
        -lightNdc.y * 0.5F + 0.5F);
    if (shadowUV.x <= 0.0F || shadowUV.x >= 1.0F ||
        shadowUV.y <= 0.0F || shadowUV.y >= 1.0F ||
        lightNdc.z <= 0.0F || lightNdc.z >= 1.0F) {
        return 1.0F;
    }

    const float normalFactor = 1.0F - max(dot(normal, lightVector), 0.0F);
    const float bias = max(
        shadowParameters.z,
        shadowParameters.w * normalFactor);

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

    const float shadow = ShadowVisibility(input.worldPosition, N, L);
    const float3 directLight =
        (diffuse + specular) * lightColor.rgb * NoL * shadow;
    const float3 ambient =
        lerp(0.03F * albedo, f0, metallic) * ambientOcclusion;
    return float4(ambient + directLight, 1.0F);
}
