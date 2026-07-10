#ifndef SLANG_GUIDE_PBR_LIGHTING
#define SLANG_GUIDE_PBR_LIGHTING

#include "Common.slang"

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float ndoth = SaturateFloat(dot(normal, halfVector));
    const float denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denom * denom, EPSILON);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, EPSILON);
}

float GeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    return GeometrySchlickGGX(SaturateFloat(dot(normal, viewDirection)), roughness) *
           GeometrySchlickGGX(SaturateFloat(dot(normal, lightDirection)), roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - SaturateFloat(cosTheta), 5.0);
}

float3 EvaluateDirectionalPbr(
    float3 baseColor,
    float metallic,
    float roughness,
    float3 normal,
    float3 viewDirection,
    float3 lightDirection,
    float3 radiance)
{
    const float3 halfVector = normalize(viewDirection + lightDirection);
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);

    const float ndotl = SaturateFloat(dot(normal, lightDirection));
    const float ndotv = max(SaturateFloat(dot(normal, viewDirection)), EPSILON);

    const float normalDistribution = DistributionGGX(normal, halfVector, roughness);
    const float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    const float3 fresnel = FresnelSchlick(max(dot(halfVector, viewDirection), 0.0), f0);

    const float3 numerator = normalDistribution * geometry * fresnel;
    const float denominator = max(4.0 * ndotv * ndotl, EPSILON);
    const float3 specular = numerator / denominator;

    const float3 diffuseRatio = (1.0 - fresnel) * (1.0 - metallic);
    return (diffuseRatio * baseColor / PI + specular) * radiance * ndotl;
}

#endif
