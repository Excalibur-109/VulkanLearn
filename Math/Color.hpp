#pragma once

/**
 * @file Color.hpp
 * @brief 渲染颜色空间、曝光、色调映射、HSV 和像素打包。
 *
 * 光照、插值、过滤和混合原则上都应在线性空间完成。sRGB 是面向存储/显示的非线性编码，
 * 不能把 sRGB 纹理值直接代入 BRDF。HDR 光照完成后通常依次执行曝光、tone mapping、
 * LinearToSRGB，再写入非 sRGB 后备缓冲；使用 sRGB 后备缓冲时最后一步由硬件完成。
 */

#include "Math/Functions.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace math {

/// IEC 61966-2-1 sRGB EOTF：把存储/显示用 sRGB 转为光照计算需要的线性值。
template <FloatingScalar T>
inline T SRGBToLinear(T value) noexcept {
    value = Max(value, static_cast<T>(0));
    return value <= static_cast<T>(0.04045)
               ? value / static_cast<T>(12.92)
               : std::pow(
                     (value + static_cast<T>(0.055)) / static_cast<T>(1.055),
                     static_cast<T>(2.4));
}

template <FloatingScalar T>
inline Vector<T, 3> SRGBToLinear(const Vector<T, 3>& color) noexcept {
    return {
        SRGBToLinear(color.x),
        SRGBToLinear(color.y),
        SRGBToLinear(color.z)};
}

/// IEC 61966-2-1 sRGB OETF：在线性空间完成光照后，编码为 sRGB 输出。
template <FloatingScalar T>
inline T LinearToSRGB(T value) noexcept {
    value = Max(value, static_cast<T>(0));
    return value <= static_cast<T>(0.0031308)
               ? value * static_cast<T>(12.92)
               : static_cast<T>(1.055) * std::pow(value, static_cast<T>(1.0 / 2.4)) -
                     static_cast<T>(0.055);
}

template <FloatingScalar T>
inline Vector<T, 3> LinearToSRGB(const Vector<T, 3>& color) noexcept {
    return {
        LinearToSRGB(color.x),
        LinearToSRGB(color.y),
        LinearToSRGB(color.z)};
}

/// Rec.709/sRGB 在线性空间的相对亮度权重。
template <FloatingScalar T>
constexpr T Luminance(const Vector<T, 3>& linearColor) noexcept {
    return Dot(linearColor, Vector<T, 3>(
                                static_cast<T>(0.2126),
                                static_cast<T>(0.7152),
                                static_cast<T>(0.0722)));
}

template <FloatingScalar T>
inline Vector<T, 3> ApplyExposure(const Vector<T, 3>& linearColor, T exposureStops) noexcept {
    // 摄影中的 1 stop 表示光量乘 2，因此乘数为 2^exposureStops。
    return linearColor * std::exp2(exposureStops);
}

/// Reinhard: c/(1+c)，把无限 HDR 压入 [0,1)，实现简单但高光容易失去色彩与对比。
template <FloatingScalar T>
constexpr Vector<T, 3> ToneMapReinhard(const Vector<T, 3>& hdrColor) noexcept {
    return hdrColor / (Vector<T, 3>(static_cast<T>(1)) + hdrColor);
}

template <FloatingScalar T>
constexpr Vector<T, 3> ToneMapReinhardExtended(const Vector<T, 3>& hdrColor, T whitePoint) noexcept {
    const T whiteSquared = Max(whitePoint * whitePoint, std::numeric_limits<T>::epsilon());
    return (hdrColor * (Vector<T, 3>(static_cast<T>(1)) + hdrColor / whiteSquared)) /
           (Vector<T, 3>(static_cast<T>(1)) + hdrColor);
}

/// Narkowicz ACES filmic 近似，适合实时渲染预览；它不是完整 ACES 色彩管理流程。
template <FloatingScalar T>
constexpr Vector<T, 3> ToneMapACES(const Vector<T, 3>& hdrColor) noexcept {
    constexpr T a = static_cast<T>(2.51);
    constexpr T b = static_cast<T>(0.03);
    constexpr T c = static_cast<T>(2.43);
    constexpr T d = static_cast<T>(0.59);
    constexpr T e = static_cast<T>(0.14);
    return Saturate((hdrColor * (a * hdrColor + Vector<T, 3>(b))) /
                    (hdrColor * (c * hdrColor + Vector<T, 3>(d)) + Vector<T, 3>(e)));
}

/// RGB 与 HSV 均使用 [0,1] 范围；hue=0 和 hue=1 表示同一个红色方向。
template <FloatingScalar T>
inline Vector<T, 3> RGBToHSV(const Vector<T, 3>& rgb) noexcept {
    const T maximum = MaxComponent(rgb);
    const T minimum = MinComponent(rgb);
    const T chroma = maximum - minimum;

    T hue = static_cast<T>(0);
    if (chroma > std::numeric_limits<T>::epsilon()) {
        if (maximum == rgb.x) {
            hue = Mod((rgb.y - rgb.z) / chroma, static_cast<T>(6));
        } else if (maximum == rgb.y) {
            hue = (rgb.z - rgb.x) / chroma + static_cast<T>(2);
        } else {
            hue = (rgb.x - rgb.y) / chroma + static_cast<T>(4);
        }
        hue /= static_cast<T>(6);
        if (hue < static_cast<T>(0)) {
            hue += static_cast<T>(1);
        }
    }
    const T saturation = maximum <= std::numeric_limits<T>::epsilon()
                             ? static_cast<T>(0)
                             : chroma / maximum;
    return {hue, saturation, maximum};
}

template <FloatingScalar T>
inline Vector<T, 3> HSVToRGB(const Vector<T, 3>& hsv) noexcept {
    const T hue = Repeat(hsv.x, static_cast<T>(1)) * static_cast<T>(6);
    const T saturation = Saturate(hsv.y);
    const T value = Max(hsv.z, static_cast<T>(0));
    const T chroma = value * saturation;
    const T x = chroma *
                (static_cast<T>(1) - Abs(Mod(hue, static_cast<T>(2)) - static_cast<T>(1)));
    const T match = value - chroma;

    Vector<T, 3> sector{};
    if (hue < static_cast<T>(1)) {
        sector = {chroma, x, 0};
    } else if (hue < static_cast<T>(2)) {
        sector = {x, chroma, 0};
    } else if (hue < static_cast<T>(3)) {
        sector = {0, chroma, x};
    } else if (hue < static_cast<T>(4)) {
        sector = {0, x, chroma};
    } else if (hue < static_cast<T>(5)) {
        sector = {x, 0, chroma};
    } else {
        sector = {chroma, 0, x};
    }
    return sector + match;
}

template <FloatingScalar T>
constexpr Vector<T, 4> PremultiplyAlpha(const Vector<T, 4>& color) noexcept {
    // 预乘 Alpha 存储 (rgb*a,a)，透明边缘过滤时不会把无效 RGB 颜色渗入可见像素。
    return {color.x * color.w, color.y * color.w, color.z * color.w, color.w};
}

template <FloatingScalar T>
constexpr Vector<T, 4> UnpremultiplyAlpha(const Vector<T, 4>& color) noexcept {
    if (color.w <= std::numeric_limits<T>::epsilon()) {
        return Vector<T, 4>(static_cast<T>(0));
    }
    return {color.x / color.w, color.y / color.w, color.z / color.w, color.w};
}

inline std::uint32_t PackRGBA8UNorm(const float4& color) noexcept {
    // 低 8 bit 存 R，随后依次 G/B/A；这是数值位布局，具体纹理格式仍由 RHI 声明。
    const uint4 bytes(VectorCast<std::uint32_t>(Round(Saturate(color) * 255.0F)));
    return bytes.x | (bytes.y << 8U) | (bytes.z << 16U) | (bytes.w << 24U);
}

constexpr float4 UnpackRGBA8UNorm(std::uint32_t packed) noexcept {
    constexpr float inverse255 = 1.0F / 255.0F;
    return {
        static_cast<float>(packed & 0xFFU) * inverse255,
        static_cast<float>((packed >> 8U) & 0xFFU) * inverse255,
        static_cast<float>((packed >> 16U) & 0xFFU) * inverse255,
        static_cast<float>((packed >> 24U) & 0xFFU) * inverse255};
}

// 颜色本质仍是线性代数向量；别名只表达语义，不增加内存或运行时成本。
using ColorRGB = float3;
using ColorRGBA = float4;
using Color = ColorRGBA;

} // namespace math

// 包含 Color.hpp 后可直接使用 Color、ToneMapACES 等颜色 API。
#if !defined(MATH_DISABLE_GLOBAL_NAMESPACE_EXPORTS)
using math::Color;
using math::ColorRGB;
using math::ColorRGBA;

using math::ApplyExposure;
using math::HSVToRGB;
using math::LinearToSRGB;
using math::Luminance;
using math::PackRGBA8UNorm;
using math::PremultiplyAlpha;
using math::RGBToHSV;
using math::SRGBToLinear;
using math::ToneMapACES;
using math::ToneMapReinhard;
using math::ToneMapReinhardExtended;
using math::UnpackRGBA8UNorm;
using math::UnpremultiplyAlpha;
#endif
