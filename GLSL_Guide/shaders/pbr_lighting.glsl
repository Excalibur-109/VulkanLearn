#ifndef GLSL_GUIDE_PBR_LIGHTING_GLSL
#define GLSL_GUIDE_PBR_LIGHTING_GLSL

#include "common.glsl"

float distributionGGX(vec3 normal, vec3 halfVector, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float ndoth = saturate(dot(normal, halfVector));
    float denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denom * denom, EPSILON);
}

float geometrySchlickGGX(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, EPSILON);
}

float geometrySmith(vec3 normal, vec3 viewDirection, vec3 lightDirection, float roughness) {
    return geometrySchlickGGX(saturate(dot(normal, viewDirection)), roughness) *
           geometrySchlickGGX(saturate(dot(normal, lightDirection)), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - saturate(cosTheta), 5.0);
}

vec3 evaluateDirectionalPbr(
    vec3 baseColor,
    float metallic,
    float roughness,
    vec3 normal,
    vec3 viewDirection,
    vec3 lightDirection,
    vec3 radiance) {
    vec3 halfVector = normalize(viewDirection + lightDirection);
    vec3 f0 = mix(vec3(0.04), baseColor, metallic);

    float ndotl = saturate(dot(normal, lightDirection));
    float ndotv = max(saturate(dot(normal, viewDirection)), EPSILON);

    float normalDistribution = distributionGGX(normal, halfVector, roughness);
    float geometry = geometrySmith(normal, viewDirection, lightDirection, roughness);
    vec3 fresnel = fresnelSchlick(max(dot(halfVector, viewDirection), 0.0), f0);

    vec3 numerator = normalDistribution * geometry * fresnel;
    float denominator = max(4.0 * ndotv * ndotl, EPSILON);
    vec3 specular = numerator / denominator;

    vec3 diffuseRatio = (1.0 - fresnel) * (1.0 - metallic);
    return (diffuseRatio * baseColor / PI + specular) * radiance * ndotl;
}

#endif
