#pragma once

/**
 * @file Scalar.hpp
 * @brief 数学库最底层：类型约束、常量和不依赖向量/矩阵的标量算法。
 *
 * 建议先读本文件，再读 Vector.hpp 和 Matrix.hpp。这里大量函数标记为 constexpr，表示在输入
 * 是编译期常量时可由编译器提前求值；普通运行时输入仍会生成正常机器指令。
 */

#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

// DirectXMath 使用强制内联让短小的寄存器运算进入调用方热循环。本库只在经过基准确认的
// 热路径使用该标记，普通函数仍交给编译器自行判断，避免无节制内联扩大指令缓存压力。
#if !defined(MATH_FORCE_INLINE)
#if defined(_MSC_VER)
#define MATH_FORCE_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
#define MATH_FORCE_INLINE inline __attribute__((always_inline))
#else
#define MATH_FORCE_INLINE inline
#endif
#endif

namespace math {

// Concepts 把模板允许的数值类别写在接口上，使错误在调用点暴露，而不是进入函数体后才报错。
// bool 虽然属于 C++ arithmetic 类型，但不适合参与加减乘除，因此单独排除。
template <typename T>
concept Scalar = std::is_arithmetic_v<T>;

template <typename T>
concept ArithmeticScalar = Scalar<T> && !std::same_as<std::remove_cv_t<T>, bool>;

template <typename T>
concept FloatingScalar = std::floating_point<T>;

template <typename T>
concept IntegralScalar = std::integral<T> && !std::same_as<std::remove_cv_t<T>, bool>;

// 使用变量模板而不是宏：Pi<float> 与 Pi<double> 会保留各自类型和对应精度。
template <FloatingScalar T>
inline constexpr T Pi = static_cast<T>(3.141592653589793238462643383279502884L);

template <FloatingScalar T>
inline constexpr T TwoPi = Pi<T> * static_cast<T>(2);

template <FloatingScalar T>
inline constexpr T HalfPi = Pi<T> * static_cast<T>(0.5);

template <FloatingScalar T>
inline constexpr T QuarterPi = Pi<T> * static_cast<T>(0.25);

template <FloatingScalar T>
inline constexpr T InvPi = static_cast<T>(1) / Pi<T>;

template <FloatingScalar T>
inline constexpr T SqrtTwo = static_cast<T>(1.414213562373095048801688724209698079L);

template <FloatingScalar T>
inline constexpr T GoldenRatio = static_cast<T>(1.618033988749894848204586834365638118L);

// 基础范围函数不依赖 <algorithm>，并且可以同时服务标量与满足比较运算的自定义类型。
template <typename T>
constexpr T Min(const T& lhs, const T& rhs) noexcept {
    return rhs < lhs ? rhs : lhs;
}

template <typename T>
constexpr T Max(const T& lhs, const T& rhs) noexcept {
    return lhs < rhs ? rhs : lhs;
}

template <typename T>
constexpr T Clamp(T value, T minimum, T maximum) noexcept {
    return value < minimum ? minimum : (maximum < value ? maximum : value);
}

template <ArithmeticScalar T>
constexpr T Saturate(T value) noexcept {
    return Clamp(value, static_cast<T>(0), static_cast<T>(1));
}

template <ArithmeticScalar T, ArithmeticScalar U>
constexpr auto Lerp(T start, T end, U amount) noexcept {
    // 线性插值公式：start + (end - start) * t。t 不被限制在 [0,1]，因此也支持外插。
    using Result = std::common_type_t<T, U>;
    const Result t = static_cast<Result>(amount);
    return static_cast<Result>(start) +
           (static_cast<Result>(end) - static_cast<Result>(start)) * t;
}

template <FloatingScalar T>
constexpr T InverseLerp(T start, T end, T value) noexcept {
    return start == end ? static_cast<T>(0) : (value - start) / (end - start);
}

template <FloatingScalar T>
constexpr T Remap(
    T inputMinimum,
    T inputMaximum,
    T outputMinimum,
    T outputMaximum,
    T value) noexcept {
    return Lerp(
        outputMinimum,
        outputMaximum,
        InverseLerp(inputMinimum, inputMaximum, value));
}

// Square/Cube 比 std::pow 更清楚，整数可用，并且通常只需一次或两次乘法。
template <ArithmeticScalar T>
constexpr T Square(T value) noexcept {
    return value * value;
}

template <ArithmeticScalar T>
constexpr T Cube(T value) noexcept {
    return value * value * value;
}

template <ArithmeticScalar T>
constexpr T Abs(T value) noexcept {
    if constexpr (std::unsigned_integral<T>) {
        return value;
    } else {
        return value < static_cast<T>(0) ? -value : value;
    }
}

template <ArithmeticScalar T>
constexpr int Sign(T value) noexcept {
    return (static_cast<T>(0) < value) - (value < static_cast<T>(0));
}

template <FloatingScalar T>
constexpr T Degrees(T radians) noexcept {
    return radians * (static_cast<T>(180) / Pi<T>);
}

template <FloatingScalar T>
constexpr T Radians(T degrees) noexcept {
    return degrees * (Pi<T> / static_cast<T>(180));
}

template <FloatingScalar T>
inline T Fract(T value) noexcept {
    return value - std::floor(value);
}

template <FloatingScalar T>
inline T Mod(T value, T divisor) noexcept {
    return std::fmod(value, divisor);
}

template <IntegralScalar T>
constexpr T Mod(T value, T divisor) noexcept {
    return value % divisor;
}

template <FloatingScalar T>
inline T Repeat(T value, T length) noexcept {
    // 与 fmod 不同，这里把负数也映射到 [0,length)，适合 UV、角度和循环动画。
    if (length <= static_cast<T>(0)) {
        return static_cast<T>(0);
    }
    return value - std::floor(value / length) * length;
}

template <FloatingScalar T>
inline T Wrap(T value, T minimum, T maximum) noexcept {
    const T range = maximum - minimum;
    return range <= static_cast<T>(0) ? minimum : minimum + Repeat(value - minimum, range);
}

template <FloatingScalar T>
inline T PingPong(T value, T length) noexcept {
    const T repeated = Repeat(value, length * static_cast<T>(2));
    return length - Abs(repeated - length);
}

template <ArithmeticScalar T>
constexpr T Step(T edge, T value) noexcept {
    return value < edge ? static_cast<T>(0) : static_cast<T>(1);
}

template <FloatingScalar T>
constexpr T SmoothStep(T edge0, T edge1, T value) noexcept {
    // 三次 Hermite 曲线 3t^2-2t^3：两端一阶导数为 0，过渡不会突然改变速度。
    const T t = Saturate(InverseLerp(edge0, edge1, value));
    return t * t * (static_cast<T>(3) - static_cast<T>(2) * t);
}

template <FloatingScalar T>
constexpr T SmootherStep(T edge0, T edge1, T value) noexcept {
    // 五次曲线 6t^5-15t^4+10t^3：两端一阶、二阶导数均为 0，比 SmoothStep 更平滑。
    const T t = Saturate(InverseLerp(edge0, edge1, value));
    return t * t * t *
           (t * (t * static_cast<T>(6) - static_cast<T>(15)) + static_cast<T>(10));
}

template <FloatingScalar T>
constexpr bool NearlyEqual(
    T lhs,
    T rhs,
    T absoluteEpsilon = std::numeric_limits<T>::epsilon() * static_cast<T>(4),
    T relativeEpsilon = std::numeric_limits<T>::epsilon() * static_cast<T>(8)) noexcept {
    // 接近 0 时看绝对误差，数值很大时看相对误差；只使用一种误差会在另一端失效。
    const T difference = Abs(lhs - rhs);
    if (difference <= absoluteEpsilon) {
        return true;
    }
    return difference <= Max(Abs(lhs), Abs(rhs)) * relativeEpsilon;
}

template <FloatingScalar T>
inline bool IsFinite(T value) noexcept {
    return std::isfinite(value);
}

template <FloatingScalar T>
inline bool IsNaN(T value) noexcept {
    return std::isnan(value);
}

template <std::unsigned_integral T>
constexpr bool IsPowerOfTwo(T value) noexcept {
    // 2 的幂在二进制中只有一个 bit 为 1；减一后该 bit 以下全部为 1。
    return value != 0 && (value & (value - 1)) == 0;
}

template <std::unsigned_integral T>
constexpr T NextPowerOfTwo(T value) noexcept {
    return value <= 1 ? static_cast<T>(1) : std::bit_ceil(value);
}

template <std::unsigned_integral T>
constexpr T PreviousPowerOfTwo(T value) noexcept {
    return value == 0 ? static_cast<T>(0) : std::bit_floor(value);
}

template <std::unsigned_integral T>
constexpr T AlignDown(T value, T alignment) noexcept {
    return alignment == 0 ? value : value - value % alignment;
}

template <std::unsigned_integral T>
constexpr T AlignUp(T value, T alignment) noexcept {
    // alignment 不要求必须是 2 的幂，因此使用取余而不是位掩码。
    if (alignment == 0) {
        return value;
    }
    const T remainder = value % alignment;
    return remainder == 0 ? value : value + (alignment - remainder);
}

template <typename T>
constexpr T Select(bool condition, const T& whenTrue, const T& whenFalse) {
    return condition ? whenTrue : whenFalse;
}

} // namespace math
