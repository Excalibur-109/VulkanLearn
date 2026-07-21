#pragma once

/**
 * @file Curves.hpp
 * @brief 参数曲线、样条、曲率、弧长近似和动画缓动函数。
 *
 * 参数曲线把 t 映射到一个值或空间位置。Bezier/Hermite/Catmull-Rom 默认不截断 t，通常在
 * [0,1] 内求值，也允许调用方主动外插。Ease* 面向动画时间，会先把 t 限制到 [0,1]。
 *
 * 推荐学习顺序：
 * 1. LinearBezier：两点线性插值；
 * 2. QuadraticBezier/CubicBezier：Bernstein 多项式形式；
 * 3. Bezier：De Casteljau 算法求任意阶曲线；
 * 4. Hermite/Catmull-Rom：使用端点切线或相邻点控制曲线；
 * 5. RationalBezier：增加权重，可精确表示圆锥曲线。
 */

#include "Math/Functions.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace math {

namespace detail {

template <typename Value, FloatingScalar T>
constexpr Value CurveLerp(const Value& start, const Value& end, T amount) noexcept {
    return start + (end - start) * amount;
}

template <FloatingScalar T>
MATH_FORCE_INLINE constexpr Vector<T, 2> CurveLerp(const Vector<T, 2>& start, const Vector<T, 2>& end, T amount) noexcept {
    return {
        start.x + (end.x - start.x) * amount,
        start.y + (end.y - start.y) * amount};
}

template <FloatingScalar T>
MATH_FORCE_INLINE constexpr Vector<T, 3> CurveLerp(const Vector<T, 3>& start, const Vector<T, 3>& end, T amount) noexcept {
    return {
        start.x + (end.x - start.x) * amount,
        start.y + (end.y - start.y) * amount,
        start.z + (end.z - start.z) * amount};
}

template <FloatingScalar T>
MATH_FORCE_INLINE constexpr Vector<T, 4> CurveLerp(const Vector<T, 4>& start, const Vector<T, 4>& end, T amount) noexcept {
    return {
        start.x + (end.x - start.x) * amount,
        start.y + (end.y - start.y) * amount,
        start.z + (end.z - start.z) * amount,
        start.w + (end.w - start.w) * amount};
}

template <typename Value, FloatingScalar T>
MATH_FORCE_INLINE constexpr Value EvaluateBezierLevel(Value* level, std::size_t count, T amount) noexcept {
    for (std::size_t activeCount = count; activeCount > 1; --activeCount) {
        for (std::size_t index = 0; index + 1 < activeCount; ++index) {
            level[index] = CurveLerp(level[index], level[index + 1], amount);
        }
    }
    return level[0];
}

template <ArithmeticScalar T>
constexpr T CurveDistance(T lhs, T rhs) noexcept {
    return Abs(lhs - rhs);
}

template <ArithmeticScalar T, std::size_t N>
inline auto CurveDistance(const Vector<T, N>& lhs, const Vector<T, N>& rhs) noexcept {
    return Distance(lhs, rhs);
}

template <typename Value, FloatingScalar T>
constexpr Value KnotLerp(const Value& start, const Value& end, T startKnot, T endKnot, T knot) noexcept {
    const T range = endKnot - startKnot;
    return Abs(range) <= std::numeric_limits<T>::epsilon()
               ? start
               : CurveLerp(start, end, (knot - startKnot) / range);
}

} // namespace detail

// -----------------------------------------------------------------------------
// Bezier curves
// -----------------------------------------------------------------------------

/// 一次 Bezier 就是线段：B(t)=(1-t)P0+tP1。
template <typename Value, FloatingScalar T>
constexpr Value LinearBezier(const Value& point0, const Value& point1, T amount) noexcept {
    return detail::CurveLerp(point0, point1, amount);
}

/**
 * 二次 Bezier：B(t)=(1-t)^2 P0 + 2(1-t)t P1 + t^2 P2。
 * P0/P2 是端点；P1 通常不在曲线上，它决定两个端点的切线方向。
 */
template <typename Value, FloatingScalar T>
constexpr Value QuadraticBezier(const Value& point0, const Value& point1, const Value& point2, T amount) noexcept {
    const T inverse = static_cast<T>(1) - amount;
    return point0 * (inverse * inverse) +
           point1 * (static_cast<T>(2) * inverse * amount) +
           point2 * (amount * amount);
}

/**
 * 三次 Bezier 是图形软件最常见的形式，四个控制点可独立控制起点/终点及两端切线。
 * B(t)=(1-t)^3P0 + 3(1-t)^2tP1 + 3(1-t)t^2P2 + t^3P3。
 */
template <typename Value, FloatingScalar T>
constexpr Value CubicBezier(const Value& point0, const Value& point1, const Value& point2, const Value& point3, T amount) noexcept {
    const T inverse = static_cast<T>(1) - amount;
    const T inverseSquared = inverse * inverse;
    const T amountSquared = amount * amount;
    return point0 * (inverseSquared * inverse) +
           point1 * (static_cast<T>(3) * inverseSquared * amount) +
           point2 * (static_cast<T>(3) * inverse * amountSquared) +
           point3 * (amountSquared * amount);
}

/// 二次 Bezier 一阶导数；t=0 时为 2(P1-P0)，t=1 时为 2(P2-P1)。
template <typename Value, FloatingScalar T>
constexpr Value QuadraticBezierDerivative(const Value& point0, const Value& point1, const Value& point2, T amount) noexcept {
    return ((point1 - point0) * (static_cast<T>(1) - amount) +
            (point2 - point1) * amount) *
           static_cast<T>(2);
}

template <typename Value>
constexpr auto QuadraticBezierSecondDerivative(const Value& point0, const Value& point1, const Value& point2) noexcept {
    return (point2 - point1 * 2 + point0) * 2;
}

/// 三次 Bezier 的导数本身是一条由三个差分控制点构成的二次 Bezier。
template <typename Value, FloatingScalar T>
constexpr Value CubicBezierDerivative(const Value& point0, const Value& point1, const Value& point2, const Value& point3, T amount) noexcept {
    const T inverse = static_cast<T>(1) - amount;
    return ((point1 - point0) * (inverse * inverse) +
            (point2 - point1) * (static_cast<T>(2) * inverse * amount) +
            (point3 - point2) * (amount * amount)) *
           static_cast<T>(3);
}

template <typename Value, FloatingScalar T>
constexpr Value CubicBezierSecondDerivative(const Value& point0, const Value& point1, const Value& point2, const Value& point3, T amount) noexcept {
    return ((point2 - point1 * static_cast<T>(2) + point0) *
                (static_cast<T>(1) - amount) +
            (point3 - point2 * static_cast<T>(2) + point1) * amount) *
           static_cast<T>(6);
}

template <typename Value>
constexpr auto CubicBezierThirdDerivative(const Value& point0, const Value& point1, const Value& point2, const Value& point3) noexcept {
    return (point3 - point2 * 3 + point1 * 3 - point0) * 6;
}

/**
 * De Casteljau 算法：每一层对相邻控制点做线性插值，直到只剩一个点。
 * 它比直接展开高阶 Bernstein 多项式更稳定，并且自然展示了 Bezier 的几何构造过程。
 * 空控制点集合返回 Value{}；一个控制点表示常量曲线。
 */
template <typename Value, FloatingScalar T>
inline Value Bezier(std::span<const Value> controlPoints, T amount) {
    if (controlPoints.empty()) {
        return Value{};
    }

    // 游戏曲线通常只有 2 到 6 个控制点。小缓冲放在栈上，避免每次求值都进入堆分配器；
    // 极高阶曲线仍回退到 vector，因而不会限制原接口支持的控制点数量。
    constexpr std::size_t stackCapacity = 8;
    if (controlPoints.size() <= stackCapacity) {
        std::array<Value, stackCapacity> level{};
        for (std::size_t index = 0; index < controlPoints.size(); ++index) {
            level[index] = controlPoints[index];
        }
        return detail::EvaluateBezierLevel(level.data(), controlPoints.size(), amount);
    }

    std::vector<Value> level(controlPoints.begin(), controlPoints.end());
    return detail::EvaluateBezierLevel(level.data(), level.size(), amount);
}

template <typename Value, std::size_t Count, FloatingScalar T>
constexpr Value Bezier(const std::array<Value, Count>& controlPoints, T amount) noexcept {
    if constexpr (Count == 0) {
        return Value{};
    } else {
        std::array<Value, Count> level = controlPoints;
        return detail::EvaluateBezierLevel(level.data(), level.size(), amount);
    }
}

/// 第 degree 阶 Bernstein 基函数：C(degree,index)t^index(1-t)^(degree-index)。
template <FloatingScalar T>
inline T BernsteinBasis(std::size_t degree, std::size_t index, T amount) noexcept {
    if (index > degree) {
        return static_cast<T>(0);
    }

    T binomial = static_cast<T>(1);
    const std::size_t symmetricIndex = Min(index, degree - index);
    for (std::size_t factor = 1; factor <= symmetricIndex; ++factor) {
        binomial *= static_cast<T>(degree - symmetricIndex + factor) /
                    static_cast<T>(factor);
    }
    return binomial *
           std::pow(amount, static_cast<T>(index)) *
           std::pow(static_cast<T>(1) - amount, static_cast<T>(degree - index));
}

/**
 * Rational Bezier 在每个 Bernstein 基函数上增加权重 wi：
 * B(t)=sum(Bi*wi*Pi)/sum(Bi*wi)。普通 Bezier 等价于所有权重均为 1。
 */
template <typename Value, FloatingScalar T>
inline Value RationalBezier(std::span<const Value> controlPoints, std::span<const T> weights, T amount) noexcept {
    if (controlPoints.empty() || controlPoints.size() != weights.size()) {
        return Value{};
    }

    Value numerator{};
    T denominator = static_cast<T>(0);
    const std::size_t degree = controlPoints.size() - 1;
    for (std::size_t index = 0; index < controlPoints.size(); ++index) {
        const T weightedBasis = BernsteinBasis(degree, index, amount) * weights[index];
        numerator += controlPoints[index] * weightedBasis;
        denominator += weightedBasis;
    }
    return Abs(denominator) <= std::numeric_limits<T>::epsilon()
               ? Value{}
               : numerator / denominator;
}

/// 二次 Rational Bezier 常用于精确表示圆弧；90 度圆弧的中间权重为 sqrt(2)/2。
template <typename Value, FloatingScalar T>
constexpr Value RationalQuadraticBezier(const Value& point0, const Value& point1, const Value& point2, T weight0, T weight1, T weight2, T amount) noexcept {
    const T inverse = static_cast<T>(1) - amount;
    const T basis0 = inverse * inverse * weight0;
    const T basis1 = static_cast<T>(2) * inverse * amount * weight1;
    const T basis2 = amount * amount * weight2;
    const T denominator = basis0 + basis1 + basis2;
    return Abs(denominator) <= std::numeric_limits<T>::epsilon()
               ? Value{}
               : (point0 * basis0 + point1 * basis1 + point2 * basis2) / denominator;
}

/**
 * 在 t 处把二次 Bezier 精确切为左右两段。两段连接点相同，形状合并后与原曲线完全一致。
 */
template <typename Value, FloatingScalar T>
constexpr std::array<std::array<Value, 3>, 2> SplitQuadraticBezier(const Value& point0, const Value& point1, const Value& point2, T amount) noexcept {
    const Value point01 = detail::CurveLerp(point0, point1, amount);
    const Value point12 = detail::CurveLerp(point1, point2, amount);
    const Value middle = detail::CurveLerp(point01, point12, amount);
    return {{{point0, point01, middle}, {middle, point12, point2}}};
}

template <typename Value, FloatingScalar T>
constexpr std::array<std::array<Value, 4>, 2> SplitCubicBezier(
    const Value& point0,
    const Value& point1,
    const Value& point2,
    const Value& point3,
    T amount) noexcept {
    const Value point01 = detail::CurveLerp(point0, point1, amount);
    const Value point12 = detail::CurveLerp(point1, point2, amount);
    const Value point23 = detail::CurveLerp(point2, point3, amount);
    const Value point012 = detail::CurveLerp(point01, point12, amount);
    const Value point123 = detail::CurveLerp(point12, point23, amount);
    const Value middle = detail::CurveLerp(point012, point123, amount);
    return {{{point0, point01, point012, middle},
             {middle, point123, point23, point3}}};
}

// -----------------------------------------------------------------------------
// Hermite and spline curves
// -----------------------------------------------------------------------------

/**
 * Cubic Hermite 由两个端点和两个端点切线定义。切线是对归一化参数 t 的导数，长度也会
 * 影响曲线形状。Bezier 与 Hermite 可以互转：m0=3(P1-P0)，m1=3(P3-P2)。
 */
template <typename Value, FloatingScalar T>
constexpr Value CubicHermite(const Value& point0, const Value& tangent0, const Value& point1, const Value& tangent1, T amount) noexcept {
    const T amountSquared = amount * amount;
    const T amountCubed = amountSquared * amount;
    const T h00 = static_cast<T>(2) * amountCubed - static_cast<T>(3) * amountSquared +
                  static_cast<T>(1);
    const T h10 = amountCubed - static_cast<T>(2) * amountSquared + amount;
    const T h01 = -static_cast<T>(2) * amountCubed + static_cast<T>(3) * amountSquared;
    const T h11 = amountCubed - amountSquared;
    return point0 * h00 + tangent0 * h10 + point1 * h01 + tangent1 * h11;
}

template <typename Value, FloatingScalar T>
constexpr Value CubicHermiteDerivative(const Value& point0, const Value& tangent0, const Value& point1, const Value& tangent1, T amount) noexcept {
    const T amountSquared = amount * amount;
    const T h00 = static_cast<T>(6) * amountSquared - static_cast<T>(6) * amount;
    const T h10 = static_cast<T>(3) * amountSquared - static_cast<T>(4) * amount +
                  static_cast<T>(1);
    const T h01 = -static_cast<T>(6) * amountSquared + static_cast<T>(6) * amount;
    const T h11 = static_cast<T>(3) * amountSquared - static_cast<T>(2) * amount;
    return point0 * h00 + tangent0 * h10 + point1 * h01 + tangent1 * h11;
}

/**
 * Cardinal spline 使用相邻点自动估计切线。tension=0 是标准 Catmull-Rom；tension 越接近 1，
 * 切线越短，曲线越接近每段端点间的平滑停靠。
 */
template <typename Value, FloatingScalar T>
constexpr Value CardinalSpline(
    const Value& point0,
    const Value& point1,
    const Value& point2,
    const Value& point3,
    T amount,
    T tension = static_cast<T>(0)) noexcept {
    const T tangentScale = (static_cast<T>(1) - tension) * static_cast<T>(0.5);
    const Value tangent1 = (point2 - point0) * tangentScale;
    const Value tangent2 = (point3 - point1) * tangentScale;
    return CubicHermite(point1, tangent1, point2, tangent2, amount);
}

/// 标准均匀 Catmull-Rom 段经过 point1 和 point2，point0/point3 用于估计两端切线。
template <typename Value, FloatingScalar T>
constexpr Value CatmullRom(const Value& point0, const Value& point1, const Value& point2, const Value& point3, T amount) noexcept {
    return CardinalSpline(point0, point1, point2, point3, amount, static_cast<T>(0));
}

/**
 * 非均匀 Catmull-Rom 根据控制点距离分配 knot。alpha=0 是均匀，0.5 是 centripetal，1 是 chordal。
 * Centripetal 形式能明显减少控制点间距差异很大时的尖角、自交和回环。
 */
template <FloatingScalar T, std::size_t N>
inline Vector<T, N> CatmullRomNonUniform(
    const Vector<T, N>& point0,
    const Vector<T, N>& point1,
    const Vector<T, N>& point2,
    const Vector<T, N>& point3,
    T amount,
    T alpha = static_cast<T>(0.5)) noexcept {
    alpha = Saturate(alpha);
    const auto nextKnot = [alpha](T knot, const Vector<T, N>& lhs, const Vector<T, N>& rhs) {
        return knot + std::pow(Distance(lhs, rhs), alpha);
    };

    const T knot0 = static_cast<T>(0);
    const T knot1 = nextKnot(knot0, point0, point1);
    const T knot2 = nextKnot(knot1, point1, point2);
    const T knot3 = nextKnot(knot2, point2, point3);
    const T knot = Lerp(knot1, knot2, amount);

    const Vector<T, N> a1 = detail::KnotLerp(point0, point1, knot0, knot1, knot);
    const Vector<T, N> a2 = detail::KnotLerp(point1, point2, knot1, knot2, knot);
    const Vector<T, N> a3 = detail::KnotLerp(point2, point3, knot2, knot3, knot);
    const Vector<T, N> b1 = detail::KnotLerp(a1, a2, knot0, knot2, knot);
    const Vector<T, N> b2 = detail::KnotLerp(a2, a3, knot1, knot3, knot);
    return detail::KnotLerp(b1, b2, knot1, knot2, knot);
}

/**
 * Kochanek-Bartels(TCB) 在 Catmull-Rom 基础上提供三个艺术控制参数：
 * tension 控制松紧，bias 控制偏向前/后控制点，continuity 控制连接处切线是否连续。
 */
template <typename Value, FloatingScalar T>
constexpr Value KochanekBartels(
    const Value& point0,
    const Value& point1,
    const Value& point2,
    const Value& point3,
    T amount,
    T tension,
    T bias,
    T continuity) noexcept {
    const T scale = (static_cast<T>(1) - tension) * static_cast<T>(0.5);
    const Value outgoing =
        (point1 - point0) *
            (scale * (static_cast<T>(1) + bias) * (static_cast<T>(1) + continuity)) +
        (point2 - point1) *
            (scale * (static_cast<T>(1) - bias) * (static_cast<T>(1) - continuity));
    const Value incoming =
        (point2 - point1) *
            (scale * (static_cast<T>(1) + bias) * (static_cast<T>(1) - continuity)) +
        (point3 - point2) *
            (scale * (static_cast<T>(1) - bias) * (static_cast<T>(1) + continuity));
    return CubicHermite(point1, outgoing, point2, incoming, amount);
}

/**
 * 均匀三次 B-Spline 每段受四个控制点影响，但一般不穿过控制点。相邻段共享控制点，天然
 * 获得 C2 连续性，适合相机轨迹和需要平滑加速度的路径。
 */
template <typename Value, FloatingScalar T>
constexpr Value CubicBSpline(const Value& point0, const Value& point1, const Value& point2, const Value& point3, T amount) noexcept {
    const T amountSquared = amount * amount;
    const T amountCubed = amountSquared * amount;
    const T basis0 = -amountCubed + static_cast<T>(3) * amountSquared -
                     static_cast<T>(3) * amount + static_cast<T>(1);
    const T basis1 = static_cast<T>(3) * amountCubed - static_cast<T>(6) * amountSquared +
                     static_cast<T>(4);
    const T basis2 = -static_cast<T>(3) * amountCubed + static_cast<T>(3) * amountSquared +
                     static_cast<T>(3) * amount + static_cast<T>(1);
    const T basis3 = amountCubed;
    return (point0 * basis0 + point1 * basis1 + point2 * basis2 + point3 * basis3) /
           static_cast<T>(6);
}

// -----------------------------------------------------------------------------
// Differential geometry and length
// -----------------------------------------------------------------------------

/// 平面参数曲线曲率 k=|x'y''-y'x''|/|P'|^3；直线曲率为 0，单位圆曲率为 1。
template <FloatingScalar T>
inline T Curvature(const Vector<T, 2>& firstDerivative, const Vector<T, 2>& secondDerivative) noexcept {
    const T speedSquared = LengthSquared(firstDerivative);
    if (speedSquared <= std::numeric_limits<T>::epsilon()) {
        return static_cast<T>(0);
    }
    const T cross = firstDerivative.x * secondDerivative.y -
                    firstDerivative.y * secondDerivative.x;
    return Abs(cross) / (speedSquared * std::sqrt(speedSquared));
}

/// 三维参数曲线曲率 k=|P' x P''|/|P'|^3。
template <FloatingScalar T>
inline T Curvature(const Vector<T, 3>& firstDerivative, const Vector<T, 3>& secondDerivative) noexcept {
    const T speedSquared = LengthSquared(firstDerivative);
    if (speedSquared <= std::numeric_limits<T>::epsilon()) {
        return static_cast<T>(0);
    }
    return Length(Cross(firstDerivative, secondDerivative)) /
           (speedSquared * std::sqrt(speedSquared));
}

/**
 * 用等参数折线近似弧长。细分越多越准确，但 t 等间隔不代表空间距离等间隔；需要恒速运动时，
 * 可离线构建“累计弧长 -> t”的查找表，再按距离反查参数。
 */
template <typename Function, FloatingScalar T = float>
inline auto ApproximateCurveLength(Function&& curve, std::size_t subdivisions = 64) {
    auto previous = curve(static_cast<T>(0));
    using DistanceType = decltype(detail::CurveDistance(previous, previous));
    DistanceType total{};
    if (subdivisions == 0) {
        return total;
    }

    for (std::size_t index = 1; index <= subdivisions; ++index) {
        const T amount = static_cast<T>(index) / static_cast<T>(subdivisions);
        const auto current = curve(amount);
        total += detail::CurveDistance(previous, current);
        previous = current;
    }
    return total;
}

// -----------------------------------------------------------------------------
// Easing curves
// -----------------------------------------------------------------------------

enum class EaseCurve {
    Linear,
    InSine,
    OutSine,
    InOutSine,
    InQuadratic,
    OutQuadratic,
    InOutQuadratic,
    InCubic,
    OutCubic,
    InOutCubic,
    InQuartic,
    OutQuartic,
    InOutQuartic,
    InQuintic,
    OutQuintic,
    InOutQuintic,
    InExponential,
    OutExponential,
    InOutExponential,
    InCircular,
    OutCircular,
    InOutCircular,
    InBack,
    OutBack,
    InOutBack,
    InElastic,
    OutElastic,
    InOutElastic,
    InBounce,
    OutBounce,
    InOutBounce
};

template <FloatingScalar T>
constexpr T EaseLinear(T amount) noexcept {
    return Saturate(amount);
}

template <FloatingScalar T>
inline T EaseInSine(T amount) noexcept {
    amount = Saturate(amount);
    return static_cast<T>(1) - std::cos(amount * HalfPi<T>);
}

template <FloatingScalar T>
inline T EaseOutSine(T amount) noexcept {
    return std::sin(Saturate(amount) * HalfPi<T>);
}

template <FloatingScalar T>
inline T EaseInOutSine(T amount) noexcept {
    amount = Saturate(amount);
    return -(std::cos(Pi<T> * amount) - static_cast<T>(1)) * static_cast<T>(0.5);
}

template <FloatingScalar T>
inline T EaseInPower(T amount, T exponent) noexcept {
    return std::pow(Saturate(amount), exponent);
}

template <FloatingScalar T>
inline T EaseOutPower(T amount, T exponent) noexcept {
    return static_cast<T>(1) - std::pow(static_cast<T>(1) - Saturate(amount), exponent);
}

template <FloatingScalar T>
inline T EaseInOutPower(T amount, T exponent) noexcept {
    amount = Saturate(amount);
    return amount < static_cast<T>(0.5)
               ? std::pow(static_cast<T>(2) * amount, exponent) * static_cast<T>(0.5)
               : static_cast<T>(1) -
                     std::pow(static_cast<T>(2) * (static_cast<T>(1) - amount), exponent) *
                         static_cast<T>(0.5);
}

#define MATH_DEFINE_POWER_EASE(NAME, EXPONENT)                                                          \
    template <FloatingScalar T>                                                                         \
    inline T EaseIn##NAME(T amount) noexcept {                                                          \
        return EaseInPower(amount, static_cast<T>(EXPONENT));                                           \
    }                                                                                                   \
    template <FloatingScalar T>                                                                         \
    inline T EaseOut##NAME(T amount) noexcept {                                                         \
        return EaseOutPower(amount, static_cast<T>(EXPONENT));                                          \
    }                                                                                                   \
    template <FloatingScalar T>                                                                         \
    inline T EaseInOut##NAME(T amount) noexcept {                                                       \
        return EaseInOutPower(amount, static_cast<T>(EXPONENT));                                        \
    }

MATH_DEFINE_POWER_EASE(Quadratic, 2)
MATH_DEFINE_POWER_EASE(Cubic, 3)
MATH_DEFINE_POWER_EASE(Quartic, 4)
MATH_DEFINE_POWER_EASE(Quintic, 5)

#undef MATH_DEFINE_POWER_EASE

template <FloatingScalar T>
inline T EaseInExponential(T amount) noexcept {
    amount = Saturate(amount);
    return amount == static_cast<T>(0)
               ? static_cast<T>(0)
               : std::pow(static_cast<T>(2), static_cast<T>(10) * amount - static_cast<T>(10));
}

template <FloatingScalar T>
inline T EaseOutExponential(T amount) noexcept {
    amount = Saturate(amount);
    return amount == static_cast<T>(1)
               ? static_cast<T>(1)
               : static_cast<T>(1) - std::pow(static_cast<T>(2), -static_cast<T>(10) * amount);
}

template <FloatingScalar T>
inline T EaseInOutExponential(T amount) noexcept {
    amount = Saturate(amount);
    if (amount == static_cast<T>(0) || amount == static_cast<T>(1)) {
        return amount;
    }
    return amount < static_cast<T>(0.5)
               ? std::pow(static_cast<T>(2), static_cast<T>(20) * amount - static_cast<T>(10)) *
                     static_cast<T>(0.5)
               : (static_cast<T>(2) -
                  std::pow(static_cast<T>(2), -static_cast<T>(20) * amount + static_cast<T>(10))) *
                     static_cast<T>(0.5);
}

template <FloatingScalar T>
inline T EaseInCircular(T amount) noexcept {
    amount = Saturate(amount);
    return static_cast<T>(1) - std::sqrt(static_cast<T>(1) - amount * amount);
}

template <FloatingScalar T>
inline T EaseOutCircular(T amount) noexcept {
    amount = Saturate(amount) - static_cast<T>(1);
    return std::sqrt(static_cast<T>(1) - amount * amount);
}

template <FloatingScalar T>
inline T EaseInOutCircular(T amount) noexcept {
    amount = Saturate(amount);
    if (amount < static_cast<T>(0.5)) {
        const T doubled = static_cast<T>(2) * amount;
        return (static_cast<T>(1) - std::sqrt(static_cast<T>(1) - doubled * doubled)) *
               static_cast<T>(0.5);
    }
    const T shifted = -static_cast<T>(2) * amount + static_cast<T>(2);
    return (std::sqrt(static_cast<T>(1) - shifted * shifted) + static_cast<T>(1)) *
           static_cast<T>(0.5);
}

template <FloatingScalar T>
constexpr T EaseInBack(T amount) noexcept {
    amount = Saturate(amount);
    constexpr T overshoot = static_cast<T>(1.70158);
    return (overshoot + static_cast<T>(1)) * amount * amount * amount -
           overshoot * amount * amount;
}

template <FloatingScalar T>
constexpr T EaseOutBack(T amount) noexcept {
    amount = Saturate(amount) - static_cast<T>(1);
    constexpr T overshoot = static_cast<T>(1.70158);
    return static_cast<T>(1) +
           (overshoot + static_cast<T>(1)) * amount * amount * amount +
           overshoot * amount * amount;
}

template <FloatingScalar T>
constexpr T EaseInOutBack(T amount) noexcept {
    amount = Saturate(amount);
    constexpr T overshoot = static_cast<T>(1.70158) * static_cast<T>(1.525);
    return amount < static_cast<T>(0.5)
               ? Square(static_cast<T>(2) * amount) *
                     ((overshoot + static_cast<T>(1)) * static_cast<T>(2) * amount - overshoot) *
                     static_cast<T>(0.5)
               : (Square(static_cast<T>(2) * amount - static_cast<T>(2)) *
                          ((overshoot + static_cast<T>(1)) *
                               (amount * static_cast<T>(2) - static_cast<T>(2)) +
                           overshoot) +
                      static_cast<T>(2)) *
                     static_cast<T>(0.5);
}

template <FloatingScalar T>
inline T EaseInElastic(T amount) noexcept {
    amount = Saturate(amount);
    if (amount == static_cast<T>(0) || amount == static_cast<T>(1)) {
        return amount;
    }
    const T phase = TwoPi<T> / static_cast<T>(3);
    return -std::pow(static_cast<T>(2), static_cast<T>(10) * amount - static_cast<T>(10)) *
           std::sin((amount * static_cast<T>(10) - static_cast<T>(10.75)) * phase);
}

template <FloatingScalar T>
inline T EaseOutElastic(T amount) noexcept {
    amount = Saturate(amount);
    if (amount == static_cast<T>(0) || amount == static_cast<T>(1)) {
        return amount;
    }
    const T phase = TwoPi<T> / static_cast<T>(3);
    return std::pow(static_cast<T>(2), -static_cast<T>(10) * amount) *
               std::sin((amount * static_cast<T>(10) - static_cast<T>(0.75)) * phase) +
           static_cast<T>(1);
}

template <FloatingScalar T>
inline T EaseInOutElastic(T amount) noexcept {
    amount = Saturate(amount);
    if (amount == static_cast<T>(0) || amount == static_cast<T>(1)) {
        return amount;
    }
    const T phase = TwoPi<T> / static_cast<T>(4.5);
    return amount < static_cast<T>(0.5)
               ? -std::pow(static_cast<T>(2), static_cast<T>(20) * amount - static_cast<T>(10)) *
                     std::sin((static_cast<T>(20) * amount - static_cast<T>(11.125)) * phase) *
                     static_cast<T>(0.5)
               : std::pow(static_cast<T>(2), -static_cast<T>(20) * amount + static_cast<T>(10)) *
                         std::sin((static_cast<T>(20) * amount - static_cast<T>(11.125)) * phase) *
                         static_cast<T>(0.5) +
                     static_cast<T>(1);
}

template <FloatingScalar T>
constexpr T EaseOutBounce(T amount) noexcept {
    amount = Saturate(amount);
    constexpr T scale = static_cast<T>(7.5625);
    constexpr T interval = static_cast<T>(2.75);
    if (amount < static_cast<T>(1) / interval) {
        return scale * amount * amount;
    }
    if (amount < static_cast<T>(2) / interval) {
        amount -= static_cast<T>(1.5) / interval;
        return scale * amount * amount + static_cast<T>(0.75);
    }
    if (amount < static_cast<T>(2.5) / interval) {
        amount -= static_cast<T>(2.25) / interval;
        return scale * amount * amount + static_cast<T>(0.9375);
    }
    amount -= static_cast<T>(2.625) / interval;
    return scale * amount * amount + static_cast<T>(0.984375);
}

template <FloatingScalar T>
constexpr T EaseInBounce(T amount) noexcept {
    return static_cast<T>(1) - EaseOutBounce(static_cast<T>(1) - Saturate(amount));
}

template <FloatingScalar T>
constexpr T EaseInOutBounce(T amount) noexcept {
    amount = Saturate(amount);
    return amount < static_cast<T>(0.5)
               ? (static_cast<T>(1) - EaseOutBounce(static_cast<T>(1) -
                                                    static_cast<T>(2) * amount)) *
                     static_cast<T>(0.5)
               : (static_cast<T>(1) + EaseOutBounce(static_cast<T>(2) * amount -
                                                    static_cast<T>(1))) *
                     static_cast<T>(0.5);
}

/// 运行时选择 easing；适合动画曲线类型由编辑器下拉框或资源数据决定的场景。
template <FloatingScalar T>
inline T EvaluateEase(EaseCurve curve, T amount) noexcept {
    switch (curve) {
    case EaseCurve::Linear:           return EaseLinear(amount);
    case EaseCurve::InSine:           return EaseInSine(amount);
    case EaseCurve::OutSine:          return EaseOutSine(amount);
    case EaseCurve::InOutSine:        return EaseInOutSine(amount);
    case EaseCurve::InQuadratic:      return EaseInQuadratic(amount);
    case EaseCurve::OutQuadratic:     return EaseOutQuadratic(amount);
    case EaseCurve::InOutQuadratic:   return EaseInOutQuadratic(amount);
    case EaseCurve::InCubic:          return EaseInCubic(amount);
    case EaseCurve::OutCubic:         return EaseOutCubic(amount);
    case EaseCurve::InOutCubic:       return EaseInOutCubic(amount);
    case EaseCurve::InQuartic:        return EaseInQuartic(amount);
    case EaseCurve::OutQuartic:       return EaseOutQuartic(amount);
    case EaseCurve::InOutQuartic:     return EaseInOutQuartic(amount);
    case EaseCurve::InQuintic:        return EaseInQuintic(amount);
    case EaseCurve::OutQuintic:       return EaseOutQuintic(amount);
    case EaseCurve::InOutQuintic:     return EaseInOutQuintic(amount);
    case EaseCurve::InExponential:    return EaseInExponential(amount);
    case EaseCurve::OutExponential:   return EaseOutExponential(amount);
    case EaseCurve::InOutExponential: return EaseInOutExponential(amount);
    case EaseCurve::InCircular:       return EaseInCircular(amount);
    case EaseCurve::OutCircular:      return EaseOutCircular(amount);
    case EaseCurve::InOutCircular:    return EaseInOutCircular(amount);
    case EaseCurve::InBack:           return EaseInBack(amount);
    case EaseCurve::OutBack:          return EaseOutBack(amount);
    case EaseCurve::InOutBack:        return EaseInOutBack(amount);
    case EaseCurve::InElastic:        return EaseInElastic(amount);
    case EaseCurve::OutElastic:       return EaseOutElastic(amount);
    case EaseCurve::InOutElastic:     return EaseInOutElastic(amount);
    case EaseCurve::InBounce:         return EaseInBounce(amount);
    case EaseCurve::OutBounce:        return EaseOutBounce(amount);
    case EaseCurve::InOutBounce:      return EaseInOutBounce(amount);
    }
    return EaseLinear(amount);
}

} // namespace math
