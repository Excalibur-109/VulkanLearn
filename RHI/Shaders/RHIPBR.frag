#version 460

layout(push_constant) uniform PBRConstants
{
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 baseColorMetallic;
    vec4 roughnessOcclusionIntensity;
    vec4 lightDirection;
} constants;

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 normal;
layout(location = 0) out vec4 outColor;

const float Pi = 3.14159265;

float distributionGGX(vec3 n, vec3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(n, h), 0.0);
    float denominator = nDotH * nDotH * (a2 - 1.0) + 1.0;
    return a2 / max(Pi * denominator * denominator, 0.0001);
}

float geometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

vec3 fresnelSchlick(float cosine, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}

void main()
{
    vec3 n = normalize(normal);
    vec3 v = normalize(constants.cameraPosition.xyz - worldPosition);
    vec3 l = normalize(constants.lightDirection.xyz);
    vec3 h = normalize(v + l);
    float roughness = clamp(constants.roughnessOcclusionIntensity.x, 0.045, 1.0);
    float metallic = clamp(constants.baseColorMetallic.w, 0.0, 1.0);
    vec3 baseColor = max(constants.baseColorMetallic.rgb, vec3(0.0));
    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    float ndf = distributionGGX(n, h, roughness);
    float geometry = geometrySchlickGGX(max(dot(n, v), 0.0), roughness) * geometrySchlickGGX(max(dot(n, l), 0.0), roughness);
    vec3 fresnel = fresnelSchlick(max(dot(h, v), 0.0), f0);
    vec3 specular = (ndf * geometry * fresnel) / max(4.0 * max(dot(n, v), 0.0) * max(dot(n, l), 0.0), 0.0001);
    vec3 kd = (vec3(1.0) - fresnel) * (1.0 - metallic);
    vec3 radiance = vec3(1.0, 0.98, 0.92) * constants.roughnessOcclusionIntensity.z;
    vec3 direct = (kd * baseColor / Pi + specular) * radiance * max(dot(n, l), 0.0);
    vec3 ambient = baseColor * (0.025 + 0.12 * max(n.y, 0.0)) * constants.roughnessOcclusionIntensity.y;
    vec3 color = ambient + direct;
    color = color / (color + vec3(1.0));
    outColor = vec4(pow(color, vec3(1.0 / 2.2)), 1.0);
}
