#pragma once

/**
 * @file Functions.hpp
 * @brief 将常见标量函数扩展到向量，并提供图形学常用的方向与坐标转换。
 *
 * Sin(float3)、Pow(float4, float4) 等接口按分量计算，行为接近 HLSL/GLSL。这里的 Map
 * 是实现细节，用来保证所有分量函数采用同一套循环和类型转换规则。
 */

#include "Math/Vector.hpp"

#include <cmath>
#include <cstddef>
#include <limits>

namespace math {

namespace detail {

// 一元/二元 Map 是向量逐分量函数的公共骨架，不应直接作为业务层 API 使用。
template <FloatingScalar T, std::size_t N, typename Function>
inline Vector<T, N> Map(const Vector<T, N>& value, Function&& function) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = static_cast<T>(function(value[index]));
    }
    return output;
}

template <FloatingScalar T, std::size_t N, typename Function>
inline Vector<T, N> Map(
    const Vector<T, N>& lhs,
    const Vector<T, N>& rhs,
    Function&& function) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = static_cast<T>(function(lhs[index], rhs[index]));
    }
    return output;
}

} // namespace detail

// 三角函数的输入输出均使用弧度，与 C++ 标准库及着色器语言保持一致。
template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Sin(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::sin(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Cos(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::cos(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Tan(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::tan(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Asin(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::asin(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Acos(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::acos(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Atan(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::atan(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Atan2(
    const Vector<T, N>& y,
    const Vector<T, N>& x) noexcept {
    return detail::Map(y, x, [](T yComponent, T xComponent) {
        return std::atan2(yComponent, xComponent);
    });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Sqrt(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::sqrt(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> ReciprocalSqrt(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) {
        return static_cast<T>(1) / std::sqrt(component);
    });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Exp(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::exp(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Exp2(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::exp2(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Log(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::log(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Log2(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::log2(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Log10(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::log10(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Pow(
    const Vector<T, N>& base,
    const Vector<T, N>& exponent) noexcept {
    return detail::Map(base, exponent, [](T baseComponent, T exponentComponent) {
        return std::pow(baseComponent, exponentComponent);
    });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Pow(const Vector<T, N>& base, T exponent) noexcept {
    return Pow(base, Vector<T, N>(exponent));
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Trunc(const Vector<T, N>& value) noexcept {
    return detail::Map(value, [](T component) { return std::trunc(component); });
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Mod(
    const Vector<T, N>& value,
    const Vector<T, N>& divisor) noexcept {
    return detail::Map(value, divisor, [](T component, T divisorComponent) {
        return std::fmod(component, divisorComponent);
    });
}

template <FloatingScalar T, std::size_t N>
constexpr Vector<T, N> Radians(const Vector<T, N>& degrees) noexcept {
    return degrees * (Pi<T> / static_cast<T>(180));
}

template <FloatingScalar T, std::size_t N>
constexpr Vector<T, N> Degrees(const Vector<T, N>& radians) noexcept {
    return radians * (static_cast<T>(180) / Pi<T>);
}

template <ArithmeticScalar T, std::size_t N>
constexpr Vector<int, N> Sign(const Vector<T, N>& value) noexcept {
    Vector<int, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = Sign(value[index]);
    }
    return output;
}

template <ArithmeticScalar T, std::size_t N>
constexpr T Sum(const Vector<T, N>& value) noexcept {
    // Reduction：把 N 个分量折叠为一个标量。Product/Min/MaxComponent 使用同一思想。
    T output{};
    for (std::size_t index = 0; index < N; ++index) {
        output += value[index];
    }
    return output;
}

template <ArithmeticScalar T, std::size_t N>
constexpr T Product(const Vector<T, N>& value) noexcept {
    T output = static_cast<T>(1);
    for (std::size_t index = 0; index < N; ++index) {
        output *= value[index];
    }
    return output;
}

template <ArithmeticScalar T, std::size_t N>
constexpr T MinComponent(const Vector<T, N>& value) noexcept {
    T output = value[0];
    for (std::size_t index = 1; index < N; ++index) {
        output = Min(output, value[index]);
    }
    return output;
}

template <ArithmeticScalar T, std::size_t N>
constexpr T MaxComponent(const Vector<T, N>& value) noexcept {
    T output = value[0];
    for (std::size_t index = 1; index < N; ++index) {
        output = Max(output, value[index]);
    }
    return output;
}

template <ArithmeticScalar T, std::size_t N>
constexpr auto DistanceSquared(
    const Vector<T, N>& lhs,
    const Vector<T, N>& rhs) noexcept {
    return LengthSquared(lhs - rhs);
}

template <Scalar T, std::size_t N>
constexpr Vector<T, N> Select(
    const Vector<bool, N>& condition,
    const Vector<T, N>& whenTrue,
    const Vector<T, N>& whenFalse) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = condition[index] ? whenTrue[index] : whenFalse[index];
    }
    return output;
}

template <FloatingScalar T, std::size_t N>
constexpr Vector<T, N> FaceForward(
    const Vector<T, N>& normal,
    const Vector<T, N>& incident,
    const Vector<T, N>& referenceNormal) noexcept {
    // 让 normal 朝向 incident 的反方向，常用于双面材质和着色法线朝向修正。
    return Dot(referenceNormal, incident) < static_cast<T>(0) ? normal : -normal;
}

template <FloatingScalar T>
constexpr Vector<T, 2> Perpendicular(const Vector<T, 2>& value) noexcept {
    return {-value.y, value.x};
}

template <FloatingScalar T>
inline Vector<T, 2> Rotate(const Vector<T, 2>& value, T radians) noexcept {
    const T cosine = std::cos(radians);
    const T sine = std::sin(radians);
    return {value.x * cosine - value.y * sine, value.x * sine + value.y * cosine};
}

template <FloatingScalar T>
inline Vector<T, 3> SphericalToCartesian(
    T radius,
    T inclination,
    T azimuth) noexcept {
    // 本库约定 inclination 从 +Y 轴向下量，azimuth 绕 Y 轴从 +X 朝 +Z 旋转。
    const T sinInclination = std::sin(inclination);
    return {
        radius * sinInclination * std::cos(azimuth),
        radius * std::cos(inclination),
        radius * sinInclination * std::sin(azimuth)};
}

template <FloatingScalar T>
inline Vector<T, 3> CartesianToSpherical(const Vector<T, 3>& value) noexcept {
    // 返回 (radius, inclination, azimuth)，与 SphericalToCartesian 的约定严格互逆。
    const T radius = Length(value);
    if (radius <= std::numeric_limits<T>::epsilon()) {
        return Vector<T, 3>(static_cast<T>(0));
    }
    return {radius, std::acos(Clamp(value.y / radius, static_cast<T>(-1), static_cast<T>(1))),
            std::atan2(value.z, value.x)};
}

} // namespace math
