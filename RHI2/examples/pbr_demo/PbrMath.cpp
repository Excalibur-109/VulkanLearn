#include "PbrMath.h"

#include <cmath>

namespace pbrdemo {

Vec3 Vec3::operator+(Vec3 rhs) const { return {x + rhs.x, y + rhs.y, z + rhs.z}; }
Vec3 Vec3::operator-(Vec3 rhs) const { return {x - rhs.x, y - rhs.y, z - rhs.z}; }
Vec3 Vec3::operator*(float scalar) const { return {x * scalar, y * scalar, z * scalar}; }
Vec3 Vec3::operator*(Vec3 rhs) const { return {x * rhs.x, y * rhs.y, z * rhs.z}; }

float dot(Vec3 lhs, Vec3 rhs) { return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z; }

Vec3 normalize(Vec3 value) {
    const float length = std::sqrt(dot(value, value));
    return length > 0.0f ? value * (1.0f / length) : Vec3{};
}

float saturate(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

namespace {
float distributionGgx(float nDotH, float roughness) {
    const float a = roughness * roughness;
    const float a2 = a * a;
    const float denominator = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    return a2 / (3.14159265f * denominator * denominator);
}

float geometrySchlickGgx(float nDotV, float roughness) {
    const float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    return nDotV / (nDotV * (1.0f - k) + k);
}

float geometrySmith(float nDotV, float nDotL, float roughness) {
    return geometrySchlickGgx(nDotV, roughness) * geometrySchlickGgx(nDotL, roughness);
}

Vec3 fresnelSchlick(float cosTheta, Vec3 f0) {
    const float factor = std::pow(1.0f - saturate(cosTheta), 5.0f);
    return f0 + (Vec3{1.0f, 1.0f, 1.0f} - f0) * factor;
}
} // 匿名命名空间

Vec3 evaluatePbr(const PbrMaterial& material, Vec3 normal, Vec3 view,
                 Vec3 light, Vec3 radiance) {
    normal = normalize(normal);
    view = normalize(view);
    light = normalize(light);
    const Vec3 halfVector = normalize(view + light);

    const float nDotV = saturate(dot(normal, view));
    const float nDotL = saturate(dot(normal, light));
    const float nDotH = saturate(dot(normal, halfVector));
    const float vDotH = saturate(dot(view, halfVector));

    Vec3 f0{0.04f, 0.04f, 0.04f};
    f0 = f0 * (1.0f - material.metallic) + material.albedo * material.metallic;
    const Vec3 fresnel = fresnelSchlick(vDotH, f0);
    const float distribution = distributionGgx(nDotH, material.roughness);
    const float geometry = geometrySmith(nDotV, nDotL, material.roughness);
    const float denominator = 4.0f * nDotV * nDotL + 0.001f;

    const Vec3 specular = fresnel * (distribution * geometry / denominator);
    const Vec3 diffuseWeight = (Vec3{1.0f, 1.0f, 1.0f} - fresnel) * (1.0f - material.metallic);
    const Vec3 diffuse = material.albedo * (1.0f / 3.14159265f);
    return (diffuse * diffuseWeight + specular) * radiance * nDotL * material.ambientOcclusion;
}

} // 命名空间 pbrdemo
