#pragma once

/**
 * @file Vector.hpp
 * @brief 2/3/4 维向量、swizzle、逐分量运算以及几何向量算法。
 *
 * Vector<T,N> 的分量连续存放，没有虚函数、指针或隐藏代理对象。float3 因此就是三个 float，
 * 适合 CPU 计算和紧凑顶点数据。GPU 常量缓冲的对齐规则由图形 API 决定，不能仅凭 sizeof
 * 直接假定 float3 在常量缓冲中也只占 12 字节。
 */

#include "Math/Scalar.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace math {

template <Scalar T, std::size_t N>
struct Vector;

/**
 * C++ 版 swizzle 使用函数形式，例如 `value.xyzz()`、`color.rgba()`；任意未命名组合可用
 * `value.Swizzle<0, 1, 2, 2>()`。这种实现不需要 union 类型重解释或带 this 指针的代理
 * 成员，所以 float3 始终只占 3 个 float，复制和上传到 GPU 时也没有隐藏状态。
 */

namespace detail {

template <std::size_t... Indices>
consteval bool IndicesAreUnique() {
    // 写 swizzle 不允许目标重复，例如 SetSwizzle<0,0> 含义不明确；读取则允许 xx/xyzz。
    constexpr std::array<std::size_t, sizeof...(Indices)> values{Indices...};
    for (std::size_t lhs = 0; lhs < values.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < values.size(); ++rhs) {
            if (values[lhs] == values[rhs]) {
                return false;
            }
        }
    }
    return true;
}

template <typename T>
using FloatingResult = std::conditional_t<std::floating_point<T>, T, double>;

} // namespace detail

// 每个命名 Swizzle 都只是通用 Swizzle<Indices...>() 的薄包装。下面按分量数生成笛卡尔积：
// float4 的 xyzw 与 rgba 各生成 16 个二分量、64 个三分量、256 个四分量函数。
#define MATH_DEFINE_SWIZZLE_2(A, AI, B, BI)                                                             \
    constexpr Vector<T, 2> A##B() const noexcept { return Swizzle<AI, BI>(); }
#define MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C, CI)                                                      \
    constexpr Vector<T, 3> A##B##C() const noexcept { return Swizzle<AI, BI, CI>(); }
#define MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, D, DI)                                               \
    constexpr Vector<T, 4> A##B##C##D() const noexcept {                                                \
        return Swizzle<AI, BI, CI, DI>();                                                               \
    }

#define MATH_SWIZZLE_2_ROW_2(A, AI, C0, I0, C1, I1)                                                     \
    MATH_DEFINE_SWIZZLE_2(A, AI, C0, I0)                                                                \
    MATH_DEFINE_SWIZZLE_2(A, AI, C1, I1)
#define MATH_SWIZZLE_3_PAIR_2(A, AI, B, BI, C0, I0, C1, I1)                                             \
    MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C0, I0)                                                         \
    MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C1, I1)
#define MATH_SWIZZLE_3_ROW_2(A, AI, C0, I0, C1, I1)                                                     \
    MATH_SWIZZLE_3_PAIR_2(A, AI, C0, I0, C0, I0, C1, I1)                                                \
    MATH_SWIZZLE_3_PAIR_2(A, AI, C1, I1, C0, I0, C1, I1)
#define MATH_SWIZZLE_4_TRIPLE_2(A, AI, B, BI, C, CI, C0, I0, C1, I1)                                    \
    MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, C0, I0)                                                  \
    MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, C1, I1)
#define MATH_SWIZZLE_4_PAIR_2(A, AI, B, BI, C0, I0, C1, I1)                                             \
    MATH_SWIZZLE_4_TRIPLE_2(A, AI, B, BI, C0, I0, C0, I0, C1, I1)                                       \
    MATH_SWIZZLE_4_TRIPLE_2(A, AI, B, BI, C1, I1, C0, I0, C1, I1)
#define MATH_SWIZZLE_4_ROW_2(A, AI, C0, I0, C1, I1)                                                     \
    MATH_SWIZZLE_4_PAIR_2(A, AI, C0, I0, C0, I0, C1, I1)                                                \
    MATH_SWIZZLE_4_PAIR_2(A, AI, C1, I1, C0, I0, C1, I1)
#define MATH_DEFINE_SWIZZLES_2_COMPONENTS(C0, I0, C1, I1)                                               \
    MATH_SWIZZLE_2_ROW_2(C0, I0, C0, I0, C1, I1)                                                        \
    MATH_SWIZZLE_2_ROW_2(C1, I1, C0, I0, C1, I1)                                                        \
    MATH_SWIZZLE_3_ROW_2(C0, I0, C0, I0, C1, I1)                                                        \
    MATH_SWIZZLE_3_ROW_2(C1, I1, C0, I0, C1, I1)                                                        \
    MATH_SWIZZLE_4_ROW_2(C0, I0, C0, I0, C1, I1)                                                        \
    MATH_SWIZZLE_4_ROW_2(C1, I1, C0, I0, C1, I1)

#define MATH_SWIZZLE_2_ROW_3(A, AI, C0, I0, C1, I1, C2, I2)                                             \
    MATH_DEFINE_SWIZZLE_2(A, AI, C0, I0)                                                                \
    MATH_DEFINE_SWIZZLE_2(A, AI, C1, I1)                                                                \
    MATH_DEFINE_SWIZZLE_2(A, AI, C2, I2)
#define MATH_SWIZZLE_3_PAIR_3(A, AI, B, BI, C0, I0, C1, I1, C2, I2)                                     \
    MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C0, I0)                                                         \
    MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C1, I1)                                                         \
    MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C2, I2)
#define MATH_SWIZZLE_3_ROW_3(A, AI, C0, I0, C1, I1, C2, I2)                                             \
    MATH_SWIZZLE_3_PAIR_3(A, AI, C0, I0, C0, I0, C1, I1, C2, I2)                                        \
    MATH_SWIZZLE_3_PAIR_3(A, AI, C1, I1, C0, I0, C1, I1, C2, I2)                                        \
    MATH_SWIZZLE_3_PAIR_3(A, AI, C2, I2, C0, I0, C1, I1, C2, I2)
#define MATH_SWIZZLE_4_TRIPLE_3(A, AI, B, BI, C, CI, C0, I0, C1, I1, C2, I2)                            \
    MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, C0, I0)                                                  \
    MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, C1, I1)                                                  \
    MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, C2, I2)
#define MATH_SWIZZLE_4_PAIR_3(A, AI, B, BI, C0, I0, C1, I1, C2, I2)                                     \
    MATH_SWIZZLE_4_TRIPLE_3(A, AI, B, BI, C0, I0, C0, I0, C1, I1, C2, I2)                               \
    MATH_SWIZZLE_4_TRIPLE_3(A, AI, B, BI, C1, I1, C0, I0, C1, I1, C2, I2)                               \
    MATH_SWIZZLE_4_TRIPLE_3(A, AI, B, BI, C2, I2, C0, I0, C1, I1, C2, I2)
#define MATH_SWIZZLE_4_ROW_3(A, AI, C0, I0, C1, I1, C2, I2)                                             \
    MATH_SWIZZLE_4_PAIR_3(A, AI, C0, I0, C0, I0, C1, I1, C2, I2)                                        \
    MATH_SWIZZLE_4_PAIR_3(A, AI, C1, I1, C0, I0, C1, I1, C2, I2)                                        \
    MATH_SWIZZLE_4_PAIR_3(A, AI, C2, I2, C0, I0, C1, I1, C2, I2)
#define MATH_DEFINE_SWIZZLES_3_COMPONENTS(C0, I0, C1, I1, C2, I2)                                       \
    MATH_SWIZZLE_2_ROW_3(C0, I0, C0, I0, C1, I1, C2, I2)                                                \
    MATH_SWIZZLE_2_ROW_3(C1, I1, C0, I0, C1, I1, C2, I2)                                                \
    MATH_SWIZZLE_2_ROW_3(C2, I2, C0, I0, C1, I1, C2, I2)                                                \
    MATH_SWIZZLE_3_ROW_3(C0, I0, C0, I0, C1, I1, C2, I2)                                                \
    MATH_SWIZZLE_3_ROW_3(C1, I1, C0, I0, C1, I1, C2, I2)                                                \
    MATH_SWIZZLE_3_ROW_3(C2, I2, C0, I0, C1, I1, C2, I2)                                                \
    MATH_SWIZZLE_4_ROW_3(C0, I0, C0, I0, C1, I1, C2, I2)                                                \
    MATH_SWIZZLE_4_ROW_3(C1, I1, C0, I0, C1, I1, C2, I2)                                                \
    MATH_SWIZZLE_4_ROW_3(C2, I2, C0, I0, C1, I1, C2, I2)

#define MATH_SWIZZLE_2_ROW_4(A, AI, C0, I0, C1, I1, C2, I2, C3, I3)                                     \
    MATH_DEFINE_SWIZZLE_2(A, AI, C0, I0)                                                                \
    MATH_DEFINE_SWIZZLE_2(A, AI, C1, I1)                                                                \
    MATH_DEFINE_SWIZZLE_2(A, AI, C2, I2)                                                                \
    MATH_DEFINE_SWIZZLE_2(A, AI, C3, I3)
#define MATH_SWIZZLE_3_PAIR_4(A, AI, B, BI, C0, I0, C1, I1, C2, I2, C3, I3)                             \
    MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C0, I0)                                                         \
    MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C1, I1)                                                         \
    MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C2, I2)                                                         \
    MATH_DEFINE_SWIZZLE_3(A, AI, B, BI, C3, I3)
#define MATH_SWIZZLE_3_ROW_4(A, AI, C0, I0, C1, I1, C2, I2, C3, I3)                                     \
    MATH_SWIZZLE_3_PAIR_4(A, AI, C0, I0, C0, I0, C1, I1, C2, I2, C3, I3)                                \
    MATH_SWIZZLE_3_PAIR_4(A, AI, C1, I1, C0, I0, C1, I1, C2, I2, C3, I3)                                \
    MATH_SWIZZLE_3_PAIR_4(A, AI, C2, I2, C0, I0, C1, I1, C2, I2, C3, I3)                                \
    MATH_SWIZZLE_3_PAIR_4(A, AI, C3, I3, C0, I0, C1, I1, C2, I2, C3, I3)
#define MATH_SWIZZLE_4_TRIPLE_4(                                                                        \
    A, AI, B, BI, C, CI, C0, I0, C1, I1, C2, I2, C3, I3)                                                \
    MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, C0, I0)                                                  \
    MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, C1, I1)                                                  \
    MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, C2, I2)                                                  \
    MATH_DEFINE_SWIZZLE_4(A, AI, B, BI, C, CI, C3, I3)
#define MATH_SWIZZLE_4_PAIR_4(A, AI, B, BI, C0, I0, C1, I1, C2, I2, C3, I3)                             \
    MATH_SWIZZLE_4_TRIPLE_4(A, AI, B, BI, C0, I0, C0, I0, C1, I1, C2, I2, C3, I3)                       \
    MATH_SWIZZLE_4_TRIPLE_4(A, AI, B, BI, C1, I1, C0, I0, C1, I1, C2, I2, C3, I3)                       \
    MATH_SWIZZLE_4_TRIPLE_4(A, AI, B, BI, C2, I2, C0, I0, C1, I1, C2, I2, C3, I3)                       \
    MATH_SWIZZLE_4_TRIPLE_4(A, AI, B, BI, C3, I3, C0, I0, C1, I1, C2, I2, C3, I3)
#define MATH_SWIZZLE_4_ROW_4(A, AI, C0, I0, C1, I1, C2, I2, C3, I3)                                     \
    MATH_SWIZZLE_4_PAIR_4(A, AI, C0, I0, C0, I0, C1, I1, C2, I2, C3, I3)                                \
    MATH_SWIZZLE_4_PAIR_4(A, AI, C1, I1, C0, I0, C1, I1, C2, I2, C3, I3)                                \
    MATH_SWIZZLE_4_PAIR_4(A, AI, C2, I2, C0, I0, C1, I1, C2, I2, C3, I3)                                \
    MATH_SWIZZLE_4_PAIR_4(A, AI, C3, I3, C0, I0, C1, I1, C2, I2, C3, I3)
#define MATH_DEFINE_SWIZZLES_4_COMPONENTS(C0, I0, C1, I1, C2, I2, C3, I3)                               \
    MATH_SWIZZLE_2_ROW_4(C0, I0, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_2_ROW_4(C1, I1, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_2_ROW_4(C2, I2, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_2_ROW_4(C3, I3, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_3_ROW_4(C0, I0, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_3_ROW_4(C1, I1, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_3_ROW_4(C2, I2, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_3_ROW_4(C3, I3, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_4_ROW_4(C0, I0, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_4_ROW_4(C1, I1, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_4_ROW_4(C2, I2, C0, I0, C1, I1, C2, I2, C3, I3)                                        \
    MATH_SWIZZLE_4_ROW_4(C3, I3, C0, I0, C1, I1, C2, I2, C3, I3)

// 分别特化 2/3/4 维，目的是提供 x/y/z/w 字段并保证简单、可预测的内存布局。
template <Scalar T>
struct Vector<T, 2> {
    using ValueType = T;
    static constexpr std::size_t ComponentCount = 2;

    T x{};
    T y{};

    constexpr Vector() noexcept = default;
    explicit constexpr Vector(T scalar) noexcept
        : x(scalar), y(scalar) {
    }
    constexpr Vector(T xValue, T yValue) noexcept
        : x(xValue), y(yValue) {
    }

    template <Scalar U>
    explicit constexpr Vector(const Vector<U, 2>& other) noexcept
        : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)) {
    }

    constexpr T& operator[](std::size_t index) noexcept {
        assert(index < ComponentCount);
        return index == 0 ? x : y;
    }

    constexpr const T& operator[](std::size_t index) const noexcept {
        assert(index < ComponentCount);
        return index == 0 ? x : y;
    }

    // 颜色分量是 x/y 的只读别名；按值返回不会改变 Vector 的紧凑内存布局。
    constexpr T r() const noexcept { return x; }
    constexpr T g() const noexcept { return y; }

    template <std::size_t... Indices>
        requires(sizeof...(Indices) >= 2 && sizeof...(Indices) <= 4 &&
                 ((Indices < ComponentCount) && ...))
    constexpr Vector<T, sizeof...(Indices)> Swizzle() const noexcept {
        return Vector<T, sizeof...(Indices)>((*this)[Indices]...);
    }

    template <std::size_t... Indices>
        requires(sizeof...(Indices) >= 2 && sizeof...(Indices) <= 4 &&
                 ((Indices < ComponentCount) && ...) &&
                 detail::IndicesAreUnique<Indices...>())
    constexpr void SetSwizzle(const Vector<T, sizeof...(Indices)>& value) noexcept {
        std::size_t source = 0;
        (((*this)[Indices] = value[source++]), ...);
    }

    MATH_DEFINE_SWIZZLES_2_COMPONENTS(x, 0, y, 1)
    MATH_DEFINE_SWIZZLES_2_COMPONENTS(r, 0, g, 1)
};

template <Scalar T>
struct Vector<T, 3> {
    using ValueType = T;
    static constexpr std::size_t ComponentCount = 3;

    T x{};
    T y{};
    T z{};

    constexpr Vector() noexcept = default;
    explicit constexpr Vector(T scalar) noexcept
        : x(scalar), y(scalar), z(scalar) {
    }
    constexpr Vector(T xValue, T yValue, T zValue) noexcept
        : x(xValue), y(yValue), z(zValue) {
    }
    constexpr Vector(const Vector<T, 2>& xyValue, T zValue) noexcept
        : x(xyValue.x), y(xyValue.y), z(zValue) {
    }

    template <Scalar U>
    explicit constexpr Vector(const Vector<U, 3>& other) noexcept
        : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)), z(static_cast<T>(other.z)) {
    }

    template <Scalar U>
    explicit constexpr Vector(const Vector<U, 4>& other) noexcept
        : x(static_cast<T>(other.x)), y(static_cast<T>(other.y)), z(static_cast<T>(other.z)) {
    }

    constexpr T& operator[](std::size_t index) noexcept {
        assert(index < ComponentCount);
        if (index == 0) {
            return x;
        }
        return index == 1 ? y : z;
    }

    constexpr const T& operator[](std::size_t index) const noexcept {
        assert(index < ComponentCount);
        if (index == 0) {
            return x;
        }
        return index == 1 ? y : z;
    }

    // float3 常用于 RGB 颜色，因此提供对应的单分量读取接口。
    constexpr T r() const noexcept { return x; }
    constexpr T g() const noexcept { return y; }
    constexpr T b() const noexcept { return z; }

    template <std::size_t... Indices>
        requires(sizeof...(Indices) >= 2 && sizeof...(Indices) <= 4 &&
                 ((Indices < ComponentCount) && ...))
    constexpr Vector<T, sizeof...(Indices)> Swizzle() const noexcept {
        return Vector<T, sizeof...(Indices)>((*this)[Indices]...);
    }

    template <std::size_t... Indices>
        requires(sizeof...(Indices) >= 2 && sizeof...(Indices) <= 3 &&
                 ((Indices < ComponentCount) && ...) &&
                 detail::IndicesAreUnique<Indices...>())
    constexpr void SetSwizzle(const Vector<T, sizeof...(Indices)>& value) noexcept {
        std::size_t source = 0;
        (((*this)[Indices] = value[source++]), ...);
    }

    MATH_DEFINE_SWIZZLES_3_COMPONENTS(x, 0, y, 1, z, 2)
    MATH_DEFINE_SWIZZLES_3_COMPONENTS(r, 0, g, 1, b, 2)
};

template <Scalar T>
struct Vector<T, 4> {
    using ValueType = T;
    static constexpr std::size_t ComponentCount = 4;

    T x{};
    T y{};
    T z{};
    T w{};

    constexpr Vector() noexcept = default;
    explicit constexpr Vector(T scalar) noexcept
        : x(scalar), y(scalar), z(scalar), w(scalar) {
    }
    constexpr Vector(T xValue, T yValue, T zValue, T wValue) noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue) {
    }
    constexpr Vector(const Vector<T, 2>& xyValue, const Vector<T, 2>& zwValue) noexcept
        : x(xyValue.x), y(xyValue.y), z(zwValue.x), w(zwValue.y) {
    }
    constexpr Vector(const Vector<T, 3>& xyzValue, T wValue) noexcept
        : x(xyzValue.x), y(xyzValue.y), z(xyzValue.z), w(wValue) {
    }

    template <Scalar U>
    explicit constexpr Vector(const Vector<U, 4>& other) noexcept
        : x(static_cast<T>(other.x)),
          y(static_cast<T>(other.y)),
          z(static_cast<T>(other.z)),
          w(static_cast<T>(other.w)) {
    }

    constexpr T& operator[](std::size_t index) noexcept {
        assert(index < ComponentCount);
        if (index == 0) {
            return x;
        }
        if (index == 1) {
            return y;
        }
        return index == 2 ? z : w;
    }

    constexpr const T& operator[](std::size_t index) const noexcept {
        assert(index < ComponentCount);
        if (index == 0) {
            return x;
        }
        if (index == 1) {
            return y;
        }
        return index == 2 ? z : w;
    }

    // RGBA 与 XYZW 共享同一份数据；这些函数只提供命名别名，不增加任何成员。
    constexpr T r() const noexcept { return x; }
    constexpr T g() const noexcept { return y; }
    constexpr T b() const noexcept { return z; }
    constexpr T a() const noexcept { return w; }

    template <std::size_t... Indices>
        requires(sizeof...(Indices) >= 2 && sizeof...(Indices) <= 4 &&
                 ((Indices < ComponentCount) && ...))
    constexpr Vector<T, sizeof...(Indices)> Swizzle() const noexcept {
        return Vector<T, sizeof...(Indices)>((*this)[Indices]...);
    }

    template <std::size_t... Indices>
        requires(sizeof...(Indices) >= 2 && sizeof...(Indices) <= 4 &&
                 ((Indices < ComponentCount) && ...) &&
                 detail::IndicesAreUnique<Indices...>())
    constexpr void SetSwizzle(const Vector<T, sizeof...(Indices)>& value) noexcept {
        std::size_t source = 0;
        (((*this)[Indices] = value[source++]), ...);
    }

    MATH_DEFINE_SWIZZLES_4_COMPONENTS(x, 0, y, 1, z, 2, w, 3)
    MATH_DEFINE_SWIZZLES_4_COMPONENTS(r, 0, g, 1, b, 2, a, 3)
};

#undef MATH_DEFINE_SWIZZLES_4_COMPONENTS
#undef MATH_SWIZZLE_4_ROW_4
#undef MATH_SWIZZLE_4_PAIR_4
#undef MATH_SWIZZLE_4_TRIPLE_4
#undef MATH_SWIZZLE_3_ROW_4
#undef MATH_SWIZZLE_3_PAIR_4
#undef MATH_SWIZZLE_2_ROW_4
#undef MATH_DEFINE_SWIZZLES_3_COMPONENTS
#undef MATH_SWIZZLE_4_ROW_3
#undef MATH_SWIZZLE_4_PAIR_3
#undef MATH_SWIZZLE_4_TRIPLE_3
#undef MATH_SWIZZLE_3_ROW_3
#undef MATH_SWIZZLE_3_PAIR_3
#undef MATH_SWIZZLE_2_ROW_3
#undef MATH_DEFINE_SWIZZLES_2_COMPONENTS
#undef MATH_SWIZZLE_4_ROW_2
#undef MATH_SWIZZLE_4_PAIR_2
#undef MATH_SWIZZLE_4_TRIPLE_2
#undef MATH_SWIZZLE_3_ROW_2
#undef MATH_SWIZZLE_3_PAIR_2
#undef MATH_SWIZZLE_2_ROW_2
#undef MATH_DEFINE_SWIZZLE_4
#undef MATH_DEFINE_SWIZZLE_3
#undef MATH_DEFINE_SWIZZLE_2

template <Scalar T, std::size_t N>
constexpr bool operator==(const Vector<T, N>& lhs, const Vector<T, N>& rhs) noexcept {
    for (std::size_t index = 0; index < N; ++index) {
        if (lhs[index] != rhs[index]) {
            return false;
        }
    }
    return true;
}

template <Scalar T, std::size_t N>
constexpr bool operator!=(const Vector<T, N>& lhs, const Vector<T, N>& rhs) noexcept {
    return !(lhs == rhs);
}

#define MATH_DEFINE_VECTOR_BINARY_OPERATOR(OPERATOR)                                                    \
    template <ArithmeticScalar L, ArithmeticScalar R, std::size_t N>                                    \
    constexpr auto operator OPERATOR(                                                                   \
        const Vector<L, N>& lhs,                                                                        \
        const Vector<R, N>& rhs) noexcept {                                                             \
        using Result = std::common_type_t<L, R>;                                                        \
        Vector<Result, N> output{};                                                                     \
        for (std::size_t index = 0; index < N; ++index) {                                               \
            output[index] = static_cast<Result>(lhs[index]) OPERATOR static_cast<Result>(rhs[index]);   \
        }                                                                                               \
        return output;                                                                                  \
    }                                                                                                   \
    template <ArithmeticScalar L, ArithmeticScalar R, std::size_t N>                                    \
    constexpr auto operator OPERATOR(const Vector<L, N>& lhs, R rhs) noexcept {                         \
        return lhs OPERATOR Vector<R, N>(rhs);                                                          \
    }                                                                                                   \
    template <ArithmeticScalar L, ArithmeticScalar R, std::size_t N>                                    \
    constexpr auto operator OPERATOR(L lhs, const Vector<R, N>& rhs) noexcept {                         \
        return Vector<L, N>(lhs) OPERATOR rhs;                                                          \
    }

MATH_DEFINE_VECTOR_BINARY_OPERATOR(+)
MATH_DEFINE_VECTOR_BINARY_OPERATOR(-)
MATH_DEFINE_VECTOR_BINARY_OPERATOR(*)
MATH_DEFINE_VECTOR_BINARY_OPERATOR(/)

#undef MATH_DEFINE_VECTOR_BINARY_OPERATOR

// 上面的宏只生成重复度很高的逐分量运算：vector OP vector、vector OP scalar 和反向形式。
// 宏在使用后立即 undef，避免污染包含本头文件的业务代码。

template <ArithmeticScalar T, std::size_t N>
constexpr Vector<T, N> operator-(const Vector<T, N>& value) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = -value[index];
    }
    return output;
}

#define MATH_DEFINE_VECTOR_COMPOUND_OPERATOR(OPERATOR)                                                  \
    template <ArithmeticScalar T, ArithmeticScalar U, std::size_t N>                                    \
    constexpr Vector<T, N>& operator OPERATOR(Vector<T, N>& lhs, const Vector<U, N>& rhs) noexcept {    \
        for (std::size_t index = 0; index < N; ++index) {                                               \
            lhs[index] OPERATOR static_cast<T>(rhs[index]);                                             \
        }                                                                                               \
        return lhs;                                                                                     \
    }                                                                                                   \
    template <ArithmeticScalar T, ArithmeticScalar U, std::size_t N>                                    \
    constexpr Vector<T, N>& operator OPERATOR(Vector<T, N>& lhs, U rhs) noexcept {                      \
        return lhs OPERATOR Vector<U, N>(rhs);                                                          \
    }

MATH_DEFINE_VECTOR_COMPOUND_OPERATOR(+=)
MATH_DEFINE_VECTOR_COMPOUND_OPERATOR(-=)
MATH_DEFINE_VECTOR_COMPOUND_OPERATOR(*=)
MATH_DEFINE_VECTOR_COMPOUND_OPERATOR(/=)

#undef MATH_DEFINE_VECTOR_COMPOUND_OPERATOR

#define MATH_DEFINE_VECTOR_INTEGRAL_OPERATOR(OPERATOR)                                                  \
    template <IntegralScalar L, IntegralScalar R, std::size_t N>                                        \
    constexpr auto operator OPERATOR(                                                                   \
        const Vector<L, N>& lhs,                                                                        \
        const Vector<R, N>& rhs) noexcept {                                                             \
        using Result = std::common_type_t<L, R>;                                                        \
        Vector<Result, N> output{};                                                                     \
        for (std::size_t index = 0; index < N; ++index) {                                               \
            output[index] = static_cast<Result>(lhs[index]) OPERATOR static_cast<Result>(rhs[index]);   \
        }                                                                                               \
        return output;                                                                                  \
    }

MATH_DEFINE_VECTOR_INTEGRAL_OPERATOR(%)
MATH_DEFINE_VECTOR_INTEGRAL_OPERATOR(&)
MATH_DEFINE_VECTOR_INTEGRAL_OPERATOR(|)
MATH_DEFINE_VECTOR_INTEGRAL_OPERATOR(^)

#undef MATH_DEFINE_VECTOR_INTEGRAL_OPERATOR

template <IntegralScalar T, std::size_t N>
constexpr Vector<T, N> operator~(const Vector<T, N>& value) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = static_cast<T>(~value[index]);
    }
    return output;
}

template <std::size_t N>
constexpr Vector<bool, N> operator!(const Vector<bool, N>& value) noexcept {
    Vector<bool, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = !value[index];
    }
    return output;
}

template <std::size_t N>
constexpr bool Any(const Vector<bool, N>& value) noexcept {
    // 比较函数返回 boolN；Any/All/None 再把逐分量条件归约为一个流程控制条件。
    for (std::size_t index = 0; index < N; ++index) {
        if (value[index]) {
            return true;
        }
    }
    return false;
}

template <std::size_t N>
constexpr bool All(const Vector<bool, N>& value) noexcept {
    for (std::size_t index = 0; index < N; ++index) {
        if (!value[index]) {
            return false;
        }
    }
    return true;
}

template <std::size_t N>
constexpr bool None(const Vector<bool, N>& value) noexcept {
    return !Any(value);
}

#define MATH_DEFINE_VECTOR_COMPARISON(NAME, OPERATOR)                                                   \
    template <Scalar L, Scalar R, std::size_t N>                                                        \
    constexpr Vector<bool, N> NAME(                                                                     \
        const Vector<L, N>& lhs,                                                                        \
        const Vector<R, N>& rhs) noexcept {                                                             \
        Vector<bool, N> output{};                                                                       \
        for (std::size_t index = 0; index < N; ++index) {                                               \
            output[index] = lhs[index] OPERATOR rhs[index];                                             \
        }                                                                                               \
        return output;                                                                                  \
    }

MATH_DEFINE_VECTOR_COMPARISON(Equal, ==)
MATH_DEFINE_VECTOR_COMPARISON(NotEqual, !=)
MATH_DEFINE_VECTOR_COMPARISON(Less, <)
MATH_DEFINE_VECTOR_COMPARISON(LessEqual, <=)
MATH_DEFINE_VECTOR_COMPARISON(Greater, >)
MATH_DEFINE_VECTOR_COMPARISON(GreaterEqual, >=)

#undef MATH_DEFINE_VECTOR_COMPARISON

template <Scalar To, Scalar From, std::size_t N>
constexpr Vector<To, N> VectorCast(const Vector<From, N>& value) noexcept {
    return Vector<To, N>(value);
}

template <Scalar T, std::size_t N>
constexpr std::array<T, N> ToArray(const Vector<T, N>& value) noexcept {
    std::array<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = value[index];
    }
    return output;
}

template <Scalar T, std::size_t N>
constexpr Vector<T, N> FromArray(const std::array<T, N>& value) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = value[index];
    }
    return output;
}

template <ArithmeticScalar L, ArithmeticScalar R, std::size_t N>
constexpr auto Dot(const Vector<L, N>& lhs, const Vector<R, N>& rhs) noexcept {
    // 点积 a·b = Σ(ai*bi)。单位向量点积等于夹角余弦，可判断朝向和明暗。
    using Result = std::common_type_t<L, R>;
    Result output{};
    for (std::size_t index = 0; index < N; ++index) {
        output += static_cast<Result>(lhs[index]) * static_cast<Result>(rhs[index]);
    }
    return output;
}

// 高频 float 点积直接展开。DirectXMath 的 SIMD 接口同样为固定维度提供专门实现；这里的
// 标量展开能让 MSVC 合并乘加并消除动态 operator[] 分支，同时保持 float3 的 12 字节布局。
MATH_FORCE_INLINE constexpr float Dot(const Vector<float, 2>& lhs, const Vector<float, 2>& rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y;
}

MATH_FORCE_INLINE constexpr float Dot(const Vector<float, 3>& lhs, const Vector<float, 3>& rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

MATH_FORCE_INLINE constexpr float Dot(const Vector<float, 4>& lhs, const Vector<float, 4>& rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

template <ArithmeticScalar L, ArithmeticScalar R>
constexpr auto Cross(const Vector<L, 3>& lhs, const Vector<R, 3>& rhs) noexcept {
    // 叉积得到同时垂直于 lhs/rhs 的向量，方向遵循右手定则，长度等于平行四边形面积。
    using Result = std::common_type_t<L, R>;
    return Vector<Result, 3>(
        static_cast<Result>(lhs.y) * static_cast<Result>(rhs.z) -
            static_cast<Result>(lhs.z) * static_cast<Result>(rhs.y),
        static_cast<Result>(lhs.z) * static_cast<Result>(rhs.x) -
            static_cast<Result>(lhs.x) * static_cast<Result>(rhs.z),
        static_cast<Result>(lhs.x) * static_cast<Result>(rhs.y) -
            static_cast<Result>(lhs.y) * static_cast<Result>(rhs.x));
}

MATH_FORCE_INLINE constexpr Vector<float, 3> Cross(const Vector<float, 3>& lhs, const Vector<float, 3>& rhs) noexcept {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

template <ArithmeticScalar T, std::size_t N>
constexpr auto LengthSquared(const Vector<T, N>& value) noexcept {
    return Dot(value, value);
}

template <ArithmeticScalar T, std::size_t N>
inline detail::FloatingResult<T> Length(const Vector<T, N>& value) noexcept {
    using Result = detail::FloatingResult<T>;
    return std::sqrt(static_cast<Result>(LengthSquared(value)));
}

MATH_FORCE_INLINE float Length(const Vector<float, 3>& value) noexcept {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

template <ArithmeticScalar T, std::size_t N>
inline detail::FloatingResult<T> Distance(const Vector<T, N>& lhs, const Vector<T, N>& rhs) noexcept {
    return Length(lhs - rhs);
}

template <ArithmeticScalar T, std::size_t N>
inline Vector<detail::FloatingResult<T>, N> Normalize(const Vector<T, N>& value) noexcept {
    // 单位化 v/|v| 只保留方向。零向量没有方向，因此这里返回零而不是制造 NaN。
    using Result = detail::FloatingResult<T>;
    const Result length = Length(value);
    if (length <= std::numeric_limits<Result>::epsilon()) {
        return Vector<Result, N>(static_cast<Result>(0));
    }
    return Vector<Result, N>(value) / length;
}

MATH_FORCE_INLINE Vector<float, 3> Normalize(const Vector<float, 3>& value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSquared <= std::numeric_limits<float>::epsilon() *
                             std::numeric_limits<float>::epsilon()) {
        return Vector<float, 3>(0.0F);
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength};
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> NormalizeSafe(
    const Vector<T, N>& value,
    const Vector<T, N>& fallback,
    T epsilon = std::numeric_limits<T>::epsilon() * static_cast<T>(8)) noexcept {
    // 渲染中法线/光线不能变成零向量，调用者可用 fallback 指定退化时的稳定方向。
    const T lengthSquared = LengthSquared(value);
    return lengthSquared <= epsilon * epsilon ? fallback : value / std::sqrt(lengthSquared);
}

/**
 * float3 是法线、方向和位置计算最常见的类型。直接展开三个分量可避免通用 Dot、除法运算符
 * 和动态 operator[] 没有被 MSVC 内联时产生的函数调用；计算公式与通用版本保持一致。
 */
MATH_FORCE_INLINE Vector<float, 3> NormalizeSafe(
    const Vector<float, 3>& value,
    const Vector<float, 3>& fallback,
    float epsilon = std::numeric_limits<float>::epsilon() * 8.0F) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (lengthSquared <= epsilon * epsilon) {
        return fallback;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return {
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength};
}

template <ArithmeticScalar T, std::size_t N>
constexpr Vector<T, N> Abs(const Vector<T, N>& value) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = Abs(value[index]);
    }
    return output;
}

template <ArithmeticScalar T, std::size_t N>
constexpr Vector<T, N> Min(const Vector<T, N>& lhs, const Vector<T, N>& rhs) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = Min(lhs[index], rhs[index]);
    }
    return output;
}

template <ArithmeticScalar T, std::size_t N>
constexpr Vector<T, N> Max(const Vector<T, N>& lhs, const Vector<T, N>& rhs) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = Max(lhs[index], rhs[index]);
    }
    return output;
}

template <ArithmeticScalar T, std::size_t N>
constexpr Vector<T, N> Clamp(const Vector<T, N>& value, const Vector<T, N>& minimum, const Vector<T, N>& maximum) noexcept {
    return Min(Max(value, minimum), maximum);
}

template <ArithmeticScalar T, std::size_t N>
constexpr Vector<T, N> Saturate(const Vector<T, N>& value) noexcept {
    return Clamp(value, Vector<T, N>(static_cast<T>(0)), Vector<T, N>(static_cast<T>(1)));
}

template <ArithmeticScalar T, ArithmeticScalar U, std::size_t N>
constexpr auto Lerp(const Vector<T, N>& start, const Vector<T, N>& end, U amount) noexcept {
    return start + (end - start) * amount;
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Floor(const Vector<T, N>& value) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = std::floor(value[index]);
    }
    return output;
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Ceil(const Vector<T, N>& value) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = std::ceil(value[index]);
    }
    return output;
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Round(const Vector<T, N>& value) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = std::round(value[index]);
    }
    return output;
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Fract(const Vector<T, N>& value) noexcept {
    return value - Floor(value);
}

template <FloatingScalar T, std::size_t N>
constexpr Vector<T, N> SmoothStep(const Vector<T, N>& edge0, const Vector<T, N>& edge1, const Vector<T, N>& value) noexcept {
    Vector<T, N> output{};
    for (std::size_t index = 0; index < N; ++index) {
        output[index] = SmoothStep(edge0[index], edge1[index], value[index]);
    }
    return output;
}

template <FloatingScalar T, std::size_t N>
constexpr Vector<T, N> Reflect(const Vector<T, N>& incident, const Vector<T, N>& normal) noexcept {
    // 镜面反射：从入射向量中减去两倍法线投影。normal 应为单位向量。
    return incident - static_cast<T>(2) * Dot(incident, normal) * normal;
}

template <FloatingScalar T, std::size_t N>
inline Vector<T, N> Refract(const Vector<T, N>& incident, const Vector<T, N>& normal, T eta) noexcept {
    // Snell 定律的向量形式，eta=n1/n2。判别式小于 0 表示全反射，此时返回零向量。
    const T normalDotIncident = Dot(normal, incident);
    const T discriminant = static_cast<T>(1) -
                           eta * eta *
                               (static_cast<T>(1) - normalDotIncident * normalDotIncident);
    if (discriminant < static_cast<T>(0)) {
        return Vector<T, N>(static_cast<T>(0));
    }
    return eta * incident -
           (eta * normalDotIncident + std::sqrt(discriminant)) * normal;
}

template <FloatingScalar T, std::size_t N>
constexpr Vector<T, N> Project(const Vector<T, N>& value, const Vector<T, N>& onto) noexcept {
    // value 在 onto 上的投影：(value·onto / onto·onto) * onto。
    const T denominator = Dot(onto, onto);
    return denominator == static_cast<T>(0)
               ? Vector<T, N>(static_cast<T>(0))
               : onto * (Dot(value, onto) / denominator);
}

template <FloatingScalar T, std::size_t N>
constexpr Vector<T, N> Reject(const Vector<T, N>& value, const Vector<T, N>& from) noexcept {
    return value - Project(value, from);
}

template <FloatingScalar T, std::size_t N>
inline T AngleBetween(const Vector<T, N>& lhs, const Vector<T, N>& rhs) noexcept {
    const T denominator = Length(lhs) * Length(rhs);
    if (denominator <= std::numeric_limits<T>::epsilon()) {
        return static_cast<T>(0);
    }
    return std::acos(Clamp(Dot(lhs, rhs) / denominator, static_cast<T>(-1), static_cast<T>(1)));
}

using bool2 = Vector<bool, 2>;
using bool3 = Vector<bool, 3>;
using bool4 = Vector<bool, 4>;
using int2 = Vector<std::int32_t, 2>;
using int3 = Vector<std::int32_t, 3>;
using int4 = Vector<std::int32_t, 4>;
using uint2 = Vector<std::uint32_t, 2>;
using uint3 = Vector<std::uint32_t, 3>;
using uint4 = Vector<std::uint32_t, 4>;
using float2 = Vector<float, 2>;
using float3 = Vector<float, 3>;
using float4 = Vector<float, 4>;
using double2 = Vector<double, 2>;
using double3 = Vector<double, 3>;
using double4 = Vector<double, 4>;

} // namespace math

/**
 * 默认把向量公共 API 导出到全局命名空间，因此包含本头文件后可以直接写 float3、Dot、
 * Normalize 等名称。定义 MATH_DISABLE_GLOBAL_NAMESPACE_EXPORTS 可关闭这些 using 声明，
 * 让大型项目继续强制使用 math:: 前缀。
 */
#if !defined(MATH_DISABLE_GLOBAL_NAMESPACE_EXPORTS)
using math::Vector;
using math::bool2;
using math::bool3;
using math::bool4;
using math::int2;
using math::int3;
using math::int4;
using math::uint2;
using math::uint3;
using math::uint4;
using math::float2;
using math::float3;
using math::float4;
using math::double2;
using math::double3;
using math::double4;

using math::Abs;
using math::All;
using math::AngleBetween;
using math::Any;
using math::Ceil;
using math::Clamp;
using math::Cross;
using math::Distance;
using math::Dot;
using math::Equal;
using math::Floor;
using math::Fract;
using math::FromArray;
using math::Greater;
using math::GreaterEqual;
using math::Length;
using math::LengthSquared;
using math::Lerp;
using math::Less;
using math::LessEqual;
using math::Max;
using math::Min;
using math::None;
using math::Normalize;
using math::NormalizeSafe;
using math::NotEqual;
using math::Project;
using math::Reflect;
using math::Refract;
using math::Reject;
using math::Round;
using math::Saturate;
using math::SmoothStep;
using math::ToArray;
using math::VectorCast;
#endif
