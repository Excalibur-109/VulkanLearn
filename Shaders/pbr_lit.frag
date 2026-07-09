#version 450

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inLightClipPosition;
layout(location = 4) flat in uint inMaterialIndex;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SceneUniforms {
    mat4 view;
    mat4 projection;
    mat4 viewProjection;
    mat4 lightViewProjection;
    vec4 cameraPosition;
    vec4 ambientColorExposure;
} sceneData;

layout(set = 0, binding = 1) uniform LightUniforms {
    vec4 directionIntensity;
    vec4 colorShadowBias;
} lightData;

layout(set = 0, binding = 2) uniform sampler2DShadow shadowMap;

struct MaterialData {
    vec4 baseColor;
    vec4 metallicRoughness;
};

layout(std430, set = 1, binding = 1) readonly buffer MaterialBuffer {
    MaterialData materials[];
} materialData;

const float PI = 3.14159265359;

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

float distributionGGX(vec3 normal, vec3 halfVector, float roughness) {
    float alpha = roughness * roughness;
    float alpha2 = alpha * alpha;
    float ndoth = saturate(dot(normal, halfVector));
    float denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denom * denom, 0.0001);
}

float geometrySchlickGGX(float ndotv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 0.0001);
}

float geometrySmith(vec3 normal, vec3 viewDirection, vec3 lightDirection, float roughness) {
    return geometrySchlickGGX(saturate(dot(normal, viewDirection)), roughness) *
           geometrySchlickGGX(saturate(dot(normal, lightDirection)), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - saturate(cosTheta), 5.0);
}

float sampleShadow(vec4 lightClipPosition) {
    vec3 projected = lightClipPosition.xyz / lightClipPosition.w;
    projected.xy = projected.xy * 0.5 + 0.5;

    if (projected.x < 0.0 || projected.x > 1.0 ||
        projected.y < 0.0 || projected.y > 1.0 ||
        projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }

    float bias = lightData.colorShadowBias.w;
    return texture(shadowMap, vec3(projected.xy, projected.z - bias));
}

void main() {
    MaterialData material = materialData.materials[inMaterialIndex];
    vec3 baseColor = pow(material.baseColor.rgb, vec3(2.2));
    float metallic = saturate(material.metallicRoughness.x);
    float roughness = clamp(material.metallicRoughness.y, 0.04, 1.0);

    vec3 normal = normalize(inNormal);
    vec3 viewDirection = normalize(sceneData.cameraPosition.xyz - inWorldPosition);
    vec3 lightDirection = normalize(-lightData.directionIntensity.xyz);
    vec3 halfVector = normalize(viewDirection + lightDirection);

    vec3 f0 = mix(vec3(0.04), baseColor, metallic);
    float ndotl = saturate(dot(normal, lightDirection));
    float ndotv = max(saturate(dot(normal, viewDirection)), 0.0001);

    float normalDistribution = distributionGGX(normal, halfVector, roughness);
    float geometry = geometrySmith(normal, viewDirection, lightDirection, roughness);
    vec3 fresnel = fresnelSchlick(max(dot(halfVector, viewDirection), 0.0), f0);

    vec3 numerator = normalDistribution * geometry * fresnel;
    float denominator = max(4.0 * ndotv * ndotl, 0.0001);
    vec3 specular = numerator / denominator;

    vec3 diffuseRatio = (1.0 - fresnel) * (1.0 - metallic);
    vec3 radiance = lightData.colorShadowBias.rgb * lightData.directionIntensity.w;
    float shadow = sampleShadow(inLightClipPosition);

    vec3 ambient = sceneData.ambientColorExposure.rgb * baseColor;
    vec3 lit = (diffuseRatio * baseColor / PI + specular) * radiance * ndotl * shadow;
    vec3 color = (ambient + lit) * sceneData.ambientColorExposure.w;

    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));
    outColor = vec4(color, material.baseColor.a);
}
