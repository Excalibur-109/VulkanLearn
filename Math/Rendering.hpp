#pragma once

/**
 * @file Rendering.hpp
 * @brief 与图形 API 无关的实时渲染数学：法线、BRDF、光源、阴影、雾、深度重建和采样。
 *
 * 一次直接光照的典型流程：
 * 1. 从顶点法线/切线建立 TBN，并把法线贴图解码到世界空间；
 * 2. SampleLight 得到“表面到光源”的方向与 radiance；
 * 3. EvaluateCookTorrance 计算 BRDF，并乘 NdotL；
 * 4. 再乘光源 radiance、阴影可见度和材质/场景的其他项；
 * 5. 所有光源累加后加入间接光与 emissive，最后曝光和 tone mapping。
 *
 * 除特别说明外，normal、viewDirection、lightDirection 都指向表面外部并应归一化。
 */

#include "Math/Color.hpp"
#include "Math/Quaternion.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace math {

template <FloatingScalar T>
struct TangentFrame {
    Vector<T, 3> tangent{1, 0, 0};    ///< T：纹理 U 增大方向。
    Vector<T, 3> bitangent{0, 1, 0};  ///< B：纹理 V 增大方向，可能受镜像 UV 符号影响。
    Vector<T, 3> normal{0, 0, 1};     ///< N：几何表面朝外方向。
};

/// Frisvad 风格的稳定基构造。输入接近零时退化为世界坐标基。
template <FloatingScalar T>
inline TangentFrame<T> BuildTangentFrame(const Vector<T, 3>& inputNormal) noexcept {
    const Vector<T, 3> normal =
        NormalizeSafe(inputNormal, Vector<T, 3>(0, 0, 1));
    const Vector<T, 3> helper = Abs(normal.z) < static_cast<T>(0.999)
                                    ? Vector<T, 3>(0, 0, 1)
                                    : Vector<T, 3>(0, 1, 0);
    const Vector<T, 3> tangent = NormalizeSafe(
        Cross(helper, normal),
        Vector<T, 3>(1, 0, 0));
    return {tangent, Cross(normal, tangent), normal};
}

template <FloatingScalar T>
inline TangentFrame<T> BuildTangentFrame(const Vector<T, 3>& inputNormal, const Vector<T, 4>& tangentAndSign) noexcept {
    const Vector<T, 3> normal =
        NormalizeSafe(inputNormal, Vector<T, 3>(0, 0, 1));
    const Vector<T, 3> tangent = NormalizeSafe(
        tangentAndSign.xyz() - normal * Dot(normal, tangentAndSign.xyz()),
        BuildTangentFrame(normal).tangent);
    const T sign = tangentAndSign.w < static_cast<T>(0)
                       ? static_cast<T>(-1)
                       : static_cast<T>(1);
    return {tangent, Cross(normal, tangent) * sign, normal};
}

template <FloatingScalar T>
inline Vector<T, 3> TangentToWorld(const TangentFrame<T>& frame, const Vector<T, 3>& tangentSpaceVector) noexcept {
    return NormalizeSafe(
        frame.tangent * tangentSpaceVector.x +
            frame.bitangent * tangentSpaceVector.y +
            frame.normal * tangentSpaceVector.z,
        frame.normal);
}

template <FloatingScalar T>
constexpr Vector<T, 3> WorldToTangent(const TangentFrame<T>& frame, const Vector<T, 3>& worldVector) noexcept {
    return {
        Dot(worldVector, frame.tangent),
        Dot(worldVector, frame.bitangent),
        Dot(worldVector, frame.normal)};
}

/// 普通法线贴图通常存储 [0,1]，先解码到 [-1,1]，再通过 TBN 转到世界空间。
template <FloatingScalar T>
inline Vector<T, 3> DecodeNormalMap(const Vector<T, 3>& encodedNormal, const TangentFrame<T>& frame, T normalScale = static_cast<T>(1)) noexcept {
    Vector<T, 3> tangentNormal = encodedNormal * static_cast<T>(2) - static_cast<T>(1);
    tangentNormal.x *= normalScale;
    tangentNormal.y *= normalScale;
    return TangentToWorld(frame, NormalizeSafe(tangentNormal, Vector<T, 3>(0, 0, 1)));
}

template <FloatingScalar T>
constexpr Vector<T, 2> EncodeNormalOctahedral(Vector<T, 3> normal) noexcept {
    // 先投影到 L1 单位八面体，再把下半球折叠到上半球，最终把 [-1,1] 编码到 [0,1]。
    // 相比直接存 xyz，可用两个分量保存完整单位法线，常用于 GBuffer 与环境贴图数据。
    // 八面体折叠要求 0 也有确定的正号。普通 Sign(0) 返回 0，会把 -Z 轴错误压到中心。
    const auto signNotZero = [](T value) constexpr noexcept {
        return value >= static_cast<T>(0) ? static_cast<T>(1) : static_cast<T>(-1);
    };
    const T inverseL1 = static_cast<T>(1) /
                        Max(Abs(normal.x) + Abs(normal.y) + Abs(normal.z),
                            std::numeric_limits<T>::epsilon());
    normal *= inverseL1;
    Vector<T, 2> encoded(normal.x, normal.y);
    if (normal.z < static_cast<T>(0)) {
        encoded = Vector<T, 2>(
            (static_cast<T>(1) - Abs(encoded.y)) * signNotZero(encoded.x),
            (static_cast<T>(1) - Abs(encoded.x)) * signNotZero(encoded.y));
    }
    return encoded * static_cast<T>(0.5) + static_cast<T>(0.5);
}

template <FloatingScalar T>
inline Vector<T, 3> DecodeNormalOctahedral(const Vector<T, 2>& encoded) noexcept {
    const auto signNotZero = [](T value) constexpr noexcept {
        return value >= static_cast<T>(0) ? static_cast<T>(1) : static_cast<T>(-1);
    };
    const Vector<T, 2> value = encoded * static_cast<T>(2) - static_cast<T>(1);
    Vector<T, 3> normal(value.x, value.y,
                        static_cast<T>(1) - Abs(value.x) - Abs(value.y));
    if (normal.z < static_cast<T>(0)) {
        const T oldX = normal.x;
        normal.x = (static_cast<T>(1) - Abs(normal.y)) * signNotZero(oldX);
        normal.y = (static_cast<T>(1) - Abs(oldX)) * signNotZero(normal.y);
    }
    return NormalizeSafe(normal, Vector<T, 3>(0, 0, 1));
}

template <FloatingScalar T>
inline std::optional<Matrix<T, 3, 3>> NormalMatrix(const Matrix<T, 4, 4>& localToWorld) noexcept {
    // 法线必须乘线性变换的 inverse-transpose，才能在非均匀缩放后继续垂直于切平面。
    // 平移不影响方向，因此只提取 localToWorld 左上角 3x3。
    Matrix<T, 3, 3> upper{};
    for (std::size_t row = 0; row < 3; ++row) {
        for (std::size_t column = 0; column < 3; ++column) {
            upper[row][column] = localToWorld[row][column];
        }
    }
    const std::optional<Matrix<T, 3, 3>> inverse = Inverse(upper);
    return inverse.has_value()
               ? std::optional<Matrix<T, 3, 3>>(Transpose(*inverse))
               : std::nullopt;
}

template <FloatingScalar T>
inline Vector<T, 3> TransformNormal(const Matrix<T, 4, 4>& localToWorld, const Vector<T, 3>& normal) noexcept {
    const std::optional<Matrix<T, 3, 3>> matrix = NormalMatrix(localToWorld);
    return matrix.has_value()
               ? NormalizeSafe(*matrix * normal, Vector<T, 3>(0, 0, 1))
               : NormalizeSafe(normal, Vector<T, 3>(0, 0, 1));
}

template <FloatingScalar T>
constexpr Vector<T, 3> LambertDiffuse(const Vector<T, 3>& albedo) noexcept {
    // Lambert BRDF=f_d=albedo/pi。后续还需乘 NdotL；除以 pi 用于满足能量守恒。
    return albedo * InvPi<T>;
}

template <FloatingScalar T>
constexpr Vector<T, 3> FresnelSchlick(T cosineTheta, const Vector<T, 3>& reflectanceAtNormal) noexcept {
    // Schlick 近似 F=F0+(1-F0)(1-cosTheta)^5：掠射角反射趋近 1。
    // 常见非金属 F0 约为 0.04；金属 F0 带颜色，通常直接来自 baseColor。
    const T factor = static_cast<T>(1) - Saturate(cosineTheta);
    const T factor5 = factor * factor * factor * factor * factor;
    return reflectanceAtNormal +
           (Vector<T, 3>(static_cast<T>(1)) - reflectanceAtNormal) * factor5;
}

template <FloatingScalar T>
constexpr Vector<T, 3> FresnelSchlickRoughness(T cosineTheta, const Vector<T, 3>& reflectanceAtNormal, T roughness) noexcept {
    const Vector<T, 3> grazing = Max(
        Vector<T, 3>(static_cast<T>(1) - Saturate(roughness)),
        reflectanceAtNormal);
    const T factor = static_cast<T>(1) - Saturate(cosineTheta);
    const T factor5 = factor * factor * factor * factor * factor;
    return reflectanceAtNormal + (grazing - reflectanceAtNormal) * factor5;
}

/// Trowbridge-Reitz GGX 法线分布函数 D。
template <FloatingScalar T>
constexpr T DistributionGGX(T normalDotHalf, T roughness) noexcept {
    // D 描述微表面法线朝向半程向量 H 的密度。粗糙度越小，高光峰值越高、范围越窄。
    const T alpha = Max(roughness * roughness, static_cast<T>(0.001));
    const T alphaSquared = alpha * alpha;
    const T nDotH = Saturate(normalDotHalf);
    const T denominator = nDotH * nDotH * (alphaSquared - static_cast<T>(1)) +
                          static_cast<T>(1);
    return alphaSquared /
           Max(Pi<T> * denominator * denominator, std::numeric_limits<T>::epsilon());
}

/// Schlick-GGX 几何遮蔽项。directLighting=true 使用直接光 k，false 使用 IBL k。
template <FloatingScalar T>
constexpr T GeometrySchlickGGX(T normalDotDirection, T roughness, bool directLighting = true) noexcept {
    const T r = directLighting ? roughness + static_cast<T>(1) : roughness;
    const T k = directLighting
                    ? r * r / static_cast<T>(8)
                    : r * r / static_cast<T>(2);
    const T nDotD = Saturate(normalDotDirection);
    return nDotD /
           Max(nDotD * (static_cast<T>(1) - k) + k, std::numeric_limits<T>::epsilon());
}

template <FloatingScalar T>
constexpr T GeometrySmith(T normalDotView, T normalDotLight, T roughness, bool directLighting = true) noexcept {
    // Smith G 项把视线和光线两个方向上的微表面遮蔽概率相乘。
    return GeometrySchlickGGX(normalDotView, roughness, directLighting) *
           GeometrySchlickGGX(normalDotLight, roughness, directLighting);
}

template <FloatingScalar T>
struct PBRMaterialSample {
    Vector<T, 3> baseColor{1, 1, 1};         ///< 线性 RGB；金属时也是有色 F0。
    T metallic = static_cast<T>(0);          ///< 0=电介质，1=金属，中间值用于纹理过渡。
    T roughness = static_cast<T>(0.5);       ///< 感知粗糙度；内部会限制到稳定范围。
    Vector<T, 3> emissive{0, 0, 0};          ///< 材质自发光，不受 NdotL 和阴影影响。
    T ambientOcclusion = static_cast<T>(1);  ///< 通常只衰减间接光，不应直接压暗直接光。
};

/**
 * @brief 单个光源方向的 Cook-Torrance BRDF。
 *
 * normal、viewDirection、lightDirection 均为从表面点向外的单位向量；返回值已乘 NdotL，
 * 但没有乘光源 radiance，因此可以复用于方向光、点光和聚光灯。
 */
template <FloatingScalar T>
inline Vector<T, 3> EvaluateCookTorrance(
    const PBRMaterialSample<T>& material,
    const Vector<T, 3>& normal,
    const Vector<T, 3>& viewDirection,
    const Vector<T, 3>& lightDirection) noexcept {
    const Vector<T, 3> n = NormalizeSafe(normal, Vector<T, 3>(0, 0, 1));
    const Vector<T, 3> v = NormalizeSafe(viewDirection, n);
    const Vector<T, 3> l = NormalizeSafe(lightDirection, n);
    const T normalDotLight = Saturate(Dot(n, l));
    const T normalDotView = Saturate(Dot(n, v));
    if (normalDotLight <= static_cast<T>(0) || normalDotView <= static_cast<T>(0)) {
        return Vector<T, 3>(static_cast<T>(0));
    }

    const Vector<T, 3> halfVector = NormalizeSafe(v + l, n);
    const T roughness = Clamp(material.roughness, static_cast<T>(0.04), static_cast<T>(1));
    const T metallic = Saturate(material.metallic);
    const Vector<T, 3> dielectricF0(static_cast<T>(0.04));
    const Vector<T, 3> f0 = Lerp(dielectricF0, material.baseColor, metallic);
    const Vector<T, 3> fresnel = FresnelSchlick(Saturate(Dot(halfVector, v)), f0);
    const T distribution = DistributionGGX(Saturate(Dot(n, halfVector)), roughness);
    const T geometry = GeometrySmith(normalDotView, normalDotLight, roughness);
    const Vector<T, 3> specular =
        // Cook-Torrance 镜面项：D * F * G / (4 * NdotV * NdotL)。
        fresnel * (distribution * geometry /
                   Max(static_cast<T>(4) * normalDotView * normalDotLight,
                       std::numeric_limits<T>::epsilon()));
    const Vector<T, 3> diffuseWeight =
        // 被 Fresnel 反射和金属镜面占用的能量不能再次进入漫反射。
        (Vector<T, 3>(static_cast<T>(1)) - fresnel) * (static_cast<T>(1) - metallic);
    return (diffuseWeight * LambertDiffuse(material.baseColor) + specular) * normalDotLight;
}

template <FloatingScalar T>
inline Vector<T, 3> EvaluateBlinnPhong(
    const Vector<T, 3>& diffuseColor,
    const Vector<T, 3>& specularColor,
    T shininess,
    const Vector<T, 3>& normal,
    const Vector<T, 3>& viewDirection,
    const Vector<T, 3>& lightDirection) noexcept {
    const Vector<T, 3> n = NormalizeSafe(normal, Vector<T, 3>(0, 0, 1));
    const Vector<T, 3> v = NormalizeSafe(viewDirection, n);
    const Vector<T, 3> l = NormalizeSafe(lightDirection, n);
    const Vector<T, 3> h = NormalizeSafe(v + l, n);
    const T nDotL = Saturate(Dot(n, l));
    const T specular = std::pow(
        Saturate(Dot(n, h)),
        Max(shininess, static_cast<T>(1)));
    return diffuseColor * nDotL + specularColor * specular * nDotL;
}

template <FloatingScalar T>
struct LightSample {
    Vector<T, 3> directionToLight{0, 1, 0};           ///< 从着色点指向光源，供 NdotL 使用。
    Vector<T, 3> radiance{0, 0, 0};                   ///< 到达表面的线性 RGB 辐亮度，已含距离/锥形衰减。
    T distance = std::numeric_limits<T>::infinity();  ///< 阴影射线最大距离；方向光为无穷。
};

template <FloatingScalar T>
struct DirectionalLightData {
    Vector<T, 3> directionToLight{0, 1, 0};  ///< 所有表面共享的“指向光源”方向。
    Vector<T, 3> color{1, 1, 1};             ///< 线性 RGB 光色。
    T illuminance = static_cast<T>(1);       ///< 本实现作为辐射强度缩放使用。
};

template <FloatingScalar T>
struct PointLightData {
    Vector<T, 3> position{0, 0, 0};   ///< 世界空间位置。
    Vector<T, 3> color{1, 1, 1};      ///< 线性 RGB 光色。
    T intensity = static_cast<T>(1);  ///< 点源强度缩放，进入 inverse-square 衰减。
    T range = static_cast<T>(10);     ///< 平滑衰减到 0 的最大影响距离；<=0 表示不截断。
};

template <FloatingScalar T>
struct SpotLightData {
    Vector<T, 3> position{0, 0, 0};
    Vector<T, 3> direction{0, 0, -1};  ///< 光从灯向外传播的方向。
    Vector<T, 3> color{1, 1, 1};
    T intensity = static_cast<T>(1);
    T range = static_cast<T>(10);
    T innerConeRadians = Radians(static_cast<T>(20));
    T outerConeRadians = Radians(static_cast<T>(30));
};

template <FloatingScalar T>
constexpr T SmoothDistanceAttenuation(T distance, T range) noexcept {
    // 物理点光源按 1/r^2 衰减。有限 range 窗函数在边界平滑降至 0，避免光照突然截断。
    const T distanceSquared = Max(distance * distance, static_cast<T>(0.0001));
    if (range <= static_cast<T>(0)) {
        return static_cast<T>(1) / distanceSquared;
    }
    const T normalized = distance / range;
    const T window = Saturate(static_cast<T>(1) - normalized * normalized * normalized * normalized);
    return window * window / distanceSquared;
}

template <FloatingScalar T>
inline LightSample<T> SampleLight(const DirectionalLightData<T>& light) noexcept {
    return {
        NormalizeSafe(light.directionToLight, Vector<T, 3>(0, 1, 0)),
        light.color * Max(light.illuminance, static_cast<T>(0)),
        std::numeric_limits<T>::infinity()};
}

template <FloatingScalar T>
inline LightSample<T> SampleLight(const PointLightData<T>& light, const Vector<T, 3>& surfacePosition) noexcept {
    const Vector<T, 3> toLight = light.position - surfacePosition;
    const T distance = Length(toLight);
    return {
        NormalizeSafe(toLight, Vector<T, 3>(0, 1, 0)),
        light.color * Max(light.intensity, static_cast<T>(0)) *
            SmoothDistanceAttenuation(distance, light.range),
        distance};
}

template <FloatingScalar T>
inline T SpotConeAttenuation(const Vector<T, 3>& lightToSurfaceDirection, const Vector<T, 3>& spotDirection, T innerConeRadians, T outerConeRadians) noexcept {
    // 角度比较改在 cosine 空间完成，inner 内为 1，outer 外为 0，中间用 SmoothStep 过渡。
    const T cosine = Dot(
        NormalizeSafe(lightToSurfaceDirection, Vector<T, 3>(0, 0, -1)),
        NormalizeSafe(spotDirection, Vector<T, 3>(0, 0, -1)));
    const T innerCosine = std::cos(Min(innerConeRadians, outerConeRadians));
    const T outerCosine = std::cos(Max(innerConeRadians, outerConeRadians));
    return SmoothStep(outerCosine, innerCosine, cosine);
}

template <FloatingScalar T>
inline LightSample<T> SampleLight(const SpotLightData<T>& light, const Vector<T, 3>& surfacePosition) noexcept {
    const Vector<T, 3> toLight = light.position - surfacePosition;
    const T distance = Length(toLight);
    const Vector<T, 3> directionToLight =
        NormalizeSafe(toLight, Vector<T, 3>(0, 1, 0));
    const T cone = SpotConeAttenuation(
        -directionToLight,
        light.direction,
        light.innerConeRadians,
        light.outerConeRadians);
    return {
        directionToLight,
        light.color * Max(light.intensity, static_cast<T>(0)) * cone *
            SmoothDistanceAttenuation(distance, light.range),
        distance};
}

template <FloatingScalar T>
inline Vector<T, 3> EvaluatePBRLight(
    const PBRMaterialSample<T>& material,
    const Vector<T, 3>& normal,
    const Vector<T, 3>& viewDirection,
    const LightSample<T>& light) noexcept {
    // EvaluateCookTorrance 已包含 NdotL，但不含光源能量；这里补乘采样后的 radiance。
    return EvaluateCookTorrance(material, normal, viewDirection, light.directionToLight) *
           light.radiance;
}

template <FloatingScalar T>
constexpr T ShadowBias(T normalDotLight, T constantBias, T slopeBias, T maximumBias) noexcept {
    // 表面越背离光线，深度误差越大，因此斜率 bias 随 1-NdotL 增长，并限制最大值。
    return Min(
        Max(constantBias, slopeBias * (static_cast<T>(1) - Saturate(normalDotLight))),
        maximumBias);
}

/// Variance Shadow Map 的 Chebyshev 上界；minimumVariance 抑制精度不足造成的漏光噪声。
template <FloatingScalar T>
constexpr T VarianceShadowVisibility(
    T receiverDepth,
    T firstMoment,
    T secondMoment,
    T minimumVariance = static_cast<T>(0.00002),
    T lightBleedingReduction = static_cast<T>(0.2)) noexcept {
    if (receiverDepth <= firstMoment) {
        return static_cast<T>(1);
    }
    const T variance = Max(secondMoment - firstMoment * firstMoment, minimumVariance);
    const T difference = receiverDepth - firstMoment;
    const T probability = variance / (variance + difference * difference);
    return Saturate(
        (probability - lightBleedingReduction) /
        Max(static_cast<T>(1) - lightBleedingReduction,
            std::numeric_limits<T>::epsilon()));
}

template <FloatingScalar T>
inline T FogTransmittanceLinear(T distance, T start, T end) noexcept {
    return static_cast<T>(1) - Saturate(InverseLerp(start, end, distance));
}

template <FloatingScalar T>
inline T FogTransmittanceExponential(T distance, T density) noexcept {
    // Beer-Lambert 定律 T=exp(-density*distance)：T=1 完全可见，T=0 完全被雾取代。
    return std::exp(-Max(density, static_cast<T>(0)) * Max(distance, static_cast<T>(0)));
}

template <FloatingScalar T>
inline T FogTransmittanceExponentialSquared(T distance, T density) noexcept {
    const T factor = Max(density, static_cast<T>(0)) * Max(distance, static_cast<T>(0));
    return std::exp(-factor * factor);
}

template <FloatingScalar T>
constexpr Vector<T, 3> ApplyFog(const Vector<T, 3>& sceneColor, const Vector<T, 3>& fogColor, T transmittance) noexcept {
    return Lerp(fogColor, sceneColor, Saturate(transmittance));
}

/// 通用投影矩阵深度反解，兼容 RH/LH、ZO/NO 和 reversed-Z，只要求传入对应 NDC z。
template <FloatingScalar T>
constexpr T ReconstructViewZ(T ndcDepth, const Matrix<T, 4, 4>& projection) noexcept {
    const T numerator = projection[2][3] - ndcDepth * projection[3][3];
    const T denominator = ndcDepth * projection[3][2] - projection[2][2];
    return numerator / denominator;
}

template <FloatingScalar T>
constexpr Vector<T, 3> ReconstructViewPosition(const Vector<T, 2>& ndc, T ndcDepth, const Matrix<T, 4, 4>& inverseProjection) noexcept {
    // 把屏幕 NDC 点乘逆投影返回齐次 view space，再除 w。延迟渲染可只存深度重建位置。
    const Vector<T, 4> homogeneous =
        inverseProjection * Vector<T, 4>(ndc.x, ndc.y, ndcDepth, static_cast<T>(1));
    return homogeneous.xyz() / homogeneous.w;
}

template <FloatingScalar T>
constexpr Vector<T, 3> ReconstructWorldPosition(const Vector<T, 2>& ndc, T ndcDepth, const Matrix<T, 4, 4>& inverseViewProjection) noexcept {
    return ReconstructViewPosition(ndc, ndcDepth, inverseViewProjection);
}

template <FloatingScalar T>
constexpr Vector<T, 2> PixelCenterToNDC(const Vector<T, 2>& pixel, const Vector<T, 2>& viewportSize, bool flipY = false) noexcept {
    Vector<T, 2> ndc =
        ((pixel + static_cast<T>(0.5)) / viewportSize) * static_cast<T>(2) -
        static_cast<T>(1);
    if (flipY) {
        ndc.y = -ndc.y;
    }
    return ndc;
}

constexpr std::uint32_t ReverseBits32(std::uint32_t bits) noexcept {
    // Van der Corput 序列把二进制位反转到小数点后，产生覆盖均匀的低差异样本。
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return bits;
}

constexpr float RadicalInverseVanDerCorput(std::uint32_t index) noexcept {
    return static_cast<float>(ReverseBits32(index)) * 2.3283064365386963e-10F;
}

constexpr float2 Hammersley2D(std::uint32_t index, std::uint32_t sampleCount) noexcept {
    return {
        sampleCount == 0 ? 0.0F : static_cast<float>(index) / static_cast<float>(sampleCount),
        RadicalInverseVanDerCorput(index)};
}

template <FloatingScalar T>
inline Vector<T, 3> CosineSampleHemisphere(const Vector<T, 2>& sample) noexcept {
    // 把单位方形样本映射到半球，PDF=cos(theta)/pi，返回局部空间 +Z 半球方向。
    const T radius = std::sqrt(Saturate(sample.x));
    const T angle = TwoPi<T> * sample.y;
    const T x = radius * std::cos(angle);
    const T y = radius * std::sin(angle);
    return {x, y, std::sqrt(Max(static_cast<T>(0), static_cast<T>(1) - x * x - y * y))};
}

template <FloatingScalar T>
inline Vector<T, 3> ImportanceSampleGGX(const Vector<T, 2>& sample, T roughness, const Vector<T, 3>& normal) noexcept {
    // 按 GGX NDF 的高概率区域采样半程向量，可用于预过滤环境贴图和 split-sum IBL。
    const T alpha = Max(roughness * roughness, static_cast<T>(0.001));
    const T phi = TwoPi<T> * sample.x;
    const T cosineTheta = std::sqrt(
        (static_cast<T>(1) - sample.y) /
        (static_cast<T>(1) + (alpha * alpha - static_cast<T>(1)) * sample.y));
    const T sineTheta = std::sqrt(Max(
        static_cast<T>(0),
        static_cast<T>(1) - cosineTheta * cosineTheta));
    const Vector<T, 3> tangentHalf(
        std::cos(phi) * sineTheta,
        std::sin(phi) * sineTheta,
        cosineTheta);
    return TangentToWorld(BuildTangentFrame(normal), tangentHalf);
}

template <FloatingScalar T>
using SH9Color = std::array<Vector<T, 3>, 9>;

/// 二阶实球谐基函数，系数顺序固定为 L00,L1-1,L10,L11,L2-2,L2-1,L20,L21,L22。
template <FloatingScalar T>
constexpr std::array<T, 9> SphericalHarmonicsBasis9(const Vector<T, 3>& direction) noexcept {
    const T x = direction.x;
    const T y = direction.y;
    const T z = direction.z;
    return {
        static_cast<T>(0.282095),
        static_cast<T>(0.488603) * y,
        static_cast<T>(0.488603) * z,
        static_cast<T>(0.488603) * x,
        static_cast<T>(1.092548) * x * y,
        static_cast<T>(1.092548) * y * z,
        static_cast<T>(0.315392) * (static_cast<T>(3) * z * z - static_cast<T>(1)),
        static_cast<T>(1.092548) * x * z,
        static_cast<T>(0.546274) * (x * x - y * y)};
}

template <FloatingScalar T>
constexpr Vector<T, 3> EvaluateSphericalHarmonics9(const SH9Color<T>& coefficients, const Vector<T, 3>& direction) noexcept {
    // SH 求值是九个“RGB 系数 * 标量基函数”的线性组合，适合低频漫反射环境光。
    const std::array<T, 9> basis = SphericalHarmonicsBasis9(direction);
    Vector<T, 3> result{};
    for (std::size_t index = 0; index < basis.size(); ++index) {
        result += coefficients[index] * basis[index];
    }
    return result;
}

using floatTangentFrame = TangentFrame<float>;
using floatPBRMaterialSample = PBRMaterialSample<float>;
using floatLightSample = LightSample<float>;
using floatDirectionalLightData = DirectionalLightData<float>;
using floatPointLightData = PointLightData<float>;
using floatSpotLightData = SpotLightData<float>;
using floatSH9Color = SH9Color<float>;

} // namespace math
