#pragma once

// CPU 端数学类型和示例使用的 Cook-Torrance BRDF 参考实现。
// 这些类型刻意独立于 RHI 和 Win32 层，便于迁移到 GPU 着色器。

namespace pbrdemo {

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3 operator+(Vec3 rhs) const;
    Vec3 operator-(Vec3 rhs) const;
    Vec3 operator*(float scalar) const;
    Vec3 operator*(Vec3 rhs) const; // 逐分量相乘。
};

float dot(Vec3 lhs, Vec3 rhs);
Vec3 normalize(Vec3 value);
float saturate(float value);

struct PbrMaterial {
    Vec3 albedo{0.9f, 0.35f, 0.12f};
    float metallic = 0.15f;
    float roughness = 0.42f;
    float ambientOcclusion = 1.0f;
};

struct Camera {
    Vec3 position{0.0f, 0.0f, 3.2f};
    float fieldOfView = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

// 使用 GGX 法线分布、Smith 几何遮蔽和 Schlick Fresnel 计算一次直接光照。
// 当原生后端替换软件适配器时，GPU 片元着色器应复现这里的计算过程。
Vec3 evaluatePbr(const PbrMaterial& material, Vec3 normal, Vec3 view,
                 Vec3 light, Vec3 radiance);

} // 命名空间 pbrdemo
