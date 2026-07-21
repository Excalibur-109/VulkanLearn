#pragma once

/**
 * @file Quaternion.hpp
 * @brief 用四元数表示三维旋转，并提供矩阵转换和旋转插值。
 *
 * 旋转四元数 q=(x,y,z,w)，其中 xyz 是虚部，w 是实部。单位四元数只有 3 个自由度，
 * 但组合稳定、没有欧拉角万向节锁。q 与 -q 表示完全相同的空间旋转。
 */

#include "Math/Matrix.hpp"

#include <cmath>
#include <limits>

namespace math {

/**
 * 轴角旋转可写成 q=(axis*sin(theta/2), cos(theta/2))。
 * 默认值 (0,0,0,1) 是单位旋转，所有用于旋转的四元数都应保持单位长度。
 */
template <FloatingScalar T>
struct Quaternion {
    T x = static_cast<T>(0);
    T y = static_cast<T>(0);
    T z = static_cast<T>(0);
    T w = static_cast<T>(1);

    constexpr Quaternion() noexcept = default;
    constexpr Quaternion(T xValue, T yValue, T zValue, T wValue) noexcept
        : x(xValue), y(yValue), z(zValue), w(wValue) {
    }
    explicit constexpr Quaternion(const Vector<T, 4>& value) noexcept
        : x(value.x), y(value.y), z(value.z), w(value.w) {
    }

    template <FloatingScalar U>
    explicit constexpr Quaternion(const Quaternion<U>& other) noexcept
        : x(static_cast<T>(other.x)),
          y(static_cast<T>(other.y)),
          z(static_cast<T>(other.z)),
          w(static_cast<T>(other.w)) {
    }

    static constexpr Quaternion Identity() noexcept {
        return {};
    }

    constexpr Vector<T, 4> xyzw() const noexcept {
        return {x, y, z, w};
    }
};

template <FloatingScalar T>
constexpr bool operator==(const Quaternion<T>& lhs, const Quaternion<T>& rhs) noexcept {
    return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
}

template <FloatingScalar T>
constexpr Quaternion<T> operator-(const Quaternion<T>& value) noexcept {
    return {-value.x, -value.y, -value.z, -value.w};
}

template <FloatingScalar T>
constexpr Quaternion<T> operator+(const Quaternion<T>& lhs, const Quaternion<T>& rhs) noexcept {
    return {lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w};
}

template <FloatingScalar T>
MATH_FORCE_INLINE constexpr Quaternion<T> operator*(const Quaternion<T>& value, T scalar) noexcept {
    return {value.x * scalar, value.y * scalar, value.z * scalar, value.w * scalar};
}

template <FloatingScalar T>
MATH_FORCE_INLINE constexpr Quaternion<T> operator/(const Quaternion<T>& value, T scalar) noexcept {
    return {value.x / scalar, value.y / scalar, value.z / scalar, value.w / scalar};
}

/** Hamilton product：lhs * rhs 表示先应用 rhs 旋转，再应用 lhs 旋转。 */
template <FloatingScalar T>
MATH_FORCE_INLINE constexpr Quaternion<T> operator*(const Quaternion<T>& lhs, const Quaternion<T>& rhs) noexcept {
    return {
        lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.w * rhs.y - lhs.x * rhs.z + lhs.y * rhs.w + lhs.z * rhs.x,
        lhs.w * rhs.z + lhs.x * rhs.y - lhs.y * rhs.x + lhs.z * rhs.w,
        lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z};
}

template <FloatingScalar T>
MATH_FORCE_INLINE constexpr T Dot(const Quaternion<T>& lhs, const Quaternion<T>& rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z + lhs.w * rhs.w;
}

template <FloatingScalar T>
MATH_FORCE_INLINE T Length(const Quaternion<T>& value) noexcept {
    return std::sqrt(Dot(value, value));
}

template <FloatingScalar T>
constexpr Quaternion<T> Conjugate(const Quaternion<T>& value) noexcept {
    // 共轭翻转旋转轴方向；单位四元数的逆恰好等于共轭。
    return {-value.x, -value.y, -value.z, value.w};
}

template <FloatingScalar T>
MATH_FORCE_INLINE Quaternion<T> Normalize(const Quaternion<T>& value) noexcept {
    const T length = Length(value);
    return length <= std::numeric_limits<T>::epsilon()
               ? Quaternion<T>::Identity()
               : value / length;
}

template <FloatingScalar T>
constexpr Quaternion<T> Inverse(const Quaternion<T>& value) noexcept {
    const T lengthSquared = Dot(value, value);
    return lengthSquared <= std::numeric_limits<T>::epsilon()
               ? Quaternion<T>::Identity()
               : Conjugate(value) / lengthSquared;
}

template <FloatingScalar T>
inline Quaternion<T> QuaternionFromAxisAngle(const Vector<T, 3>& axis, T radians) noexcept {
    // 使用半角是因为四元数在四维单位球面上以两倍覆盖表示三维旋转。
    const Vector<T, 3> unitAxis = NormalizeSafe(axis, Vector<T, 3>(1, 0, 0));
    const T halfAngle = radians * static_cast<T>(0.5);
    const T sine = std::sin(halfAngle);
    return {unitAxis.x * sine, unitAxis.y * sine, unitAxis.z * sine, std::cos(halfAngle)};
}

template <FloatingScalar T>
inline Quaternion<T> QuaternionFromEulerXYZ(const Vector<T, 3>& radians) noexcept {
    // 组合结果 z*y*x 作用于列向量时，实际应用顺序为 X -> Y -> Z。
    const Quaternion<T> xRotation = QuaternionFromAxisAngle(Vector<T, 3>(1, 0, 0), radians.x);
    const Quaternion<T> yRotation = QuaternionFromAxisAngle(Vector<T, 3>(0, 1, 0), radians.y);
    const Quaternion<T> zRotation = QuaternionFromAxisAngle(Vector<T, 3>(0, 0, 1), radians.z);
    return Normalize(zRotation * yRotation * xRotation);
}

template <FloatingScalar T>
MATH_FORCE_INLINE constexpr Vector<T, 3> Rotate(const Quaternion<T>& rotation, const Vector<T, 3>& vector) noexcept {
    // DirectXMath 同样展开 q*v*q^-1。把两次叉积直接写成标量，避免多个 Vector 运算符
    // 在编译器拒绝内联时形成调用链；单位四元数的数学结果与原实现完全相同。
    const T twiceCrossX = static_cast<T>(2) *
                          (rotation.y * vector.z - rotation.z * vector.y);
    const T twiceCrossY = static_cast<T>(2) *
                          (rotation.z * vector.x - rotation.x * vector.z);
    const T twiceCrossZ = static_cast<T>(2) *
                          (rotation.x * vector.y - rotation.y * vector.x);
    return {
        vector.x + rotation.w * twiceCrossX +
            (rotation.y * twiceCrossZ - rotation.z * twiceCrossY),
        vector.y + rotation.w * twiceCrossY +
            (rotation.z * twiceCrossX - rotation.x * twiceCrossZ),
        vector.z + rotation.w * twiceCrossZ +
            (rotation.x * twiceCrossY - rotation.y * twiceCrossX)};
}

template <FloatingScalar T>
constexpr Matrix<T, 3, 3> Matrix3x3FromQuaternion(const Quaternion<T>& input) noexcept {
    // 先归一化可去掉累计误差，再展开 q*v*q^-1 得到旋转矩阵的九个元素。
    const Quaternion<T> q = Normalize(input);
    const T xx = q.x * q.x;
    const T yy = q.y * q.y;
    const T zz = q.z * q.z;
    const T xy = q.x * q.y;
    const T xz = q.x * q.z;
    const T yz = q.y * q.z;
    const T wx = q.w * q.x;
    const T wy = q.w * q.y;
    const T wz = q.w * q.z;
    return Matrix<T, 3, 3>(
        1 - 2 * (yy + zz), 2 * (xy - wz), 2 * (xz + wy),
        2 * (xy + wz), 1 - 2 * (xx + zz), 2 * (yz - wx),
        2 * (xz - wy), 2 * (yz + wx), 1 - 2 * (xx + yy));
}

template <FloatingScalar T>
constexpr Matrix<T, 4, 4> Matrix4x4FromQuaternion(const Quaternion<T>& rotation) noexcept {
    return ResizeMatrix<T, 4, 4>(Matrix3x3FromQuaternion(rotation));
}

template <FloatingScalar T>
inline Quaternion<T> QuaternionFromMatrix(const Matrix<T, 3, 3>& matrix) noexcept {
    // 根据 trace 和最大对角元素选择数值最稳定的分支，避免某个分母接近 0。
    Quaternion<T> output{};
    const T trace = Trace(matrix);
    if (trace > static_cast<T>(0)) {
        const T scale = std::sqrt(trace + static_cast<T>(1)) * static_cast<T>(2);
        output.w = static_cast<T>(0.25) * scale;
        output.x = (matrix[2][1] - matrix[1][2]) / scale;
        output.y = (matrix[0][2] - matrix[2][0]) / scale;
        output.z = (matrix[1][0] - matrix[0][1]) / scale;
    } else if (matrix[0][0] > matrix[1][1] && matrix[0][0] > matrix[2][2]) {
        const T scale = std::sqrt(
                            static_cast<T>(1) + matrix[0][0] - matrix[1][1] - matrix[2][2]) *
                        static_cast<T>(2);
        output.w = (matrix[2][1] - matrix[1][2]) / scale;
        output.x = static_cast<T>(0.25) * scale;
        output.y = (matrix[0][1] + matrix[1][0]) / scale;
        output.z = (matrix[0][2] + matrix[2][0]) / scale;
    } else if (matrix[1][1] > matrix[2][2]) {
        const T scale = std::sqrt(
                            static_cast<T>(1) + matrix[1][1] - matrix[0][0] - matrix[2][2]) *
                        static_cast<T>(2);
        output.w = (matrix[0][2] - matrix[2][0]) / scale;
        output.x = (matrix[0][1] + matrix[1][0]) / scale;
        output.y = static_cast<T>(0.25) * scale;
        output.z = (matrix[1][2] + matrix[2][1]) / scale;
    } else {
        const T scale = std::sqrt(
                            static_cast<T>(1) + matrix[2][2] - matrix[0][0] - matrix[1][1]) *
                        static_cast<T>(2);
        output.w = (matrix[1][0] - matrix[0][1]) / scale;
        output.x = (matrix[0][2] + matrix[2][0]) / scale;
        output.y = (matrix[1][2] + matrix[2][1]) / scale;
        output.z = static_cast<T>(0.25) * scale;
    }
    return Normalize(output);
}

template <FloatingScalar T>
inline Quaternion<T> Nlerp(const Quaternion<T>& start, const Quaternion<T>& end, T amount) noexcept {
    // Nlerp 便宜且连续，但角速度不恒定。点积小于 0 时翻转 end，选择四维球面上的短弧。
    const Quaternion<T> adjustedEnd = Dot(start, end) < static_cast<T>(0) ? -end : end;
    return Normalize(start * (static_cast<T>(1) - amount) + adjustedEnd * amount);
}

template <FloatingScalar T>
MATH_FORCE_INLINE Quaternion<T> Slerp(const Quaternion<T>& start, const Quaternion<T>& end, T amount) noexcept {
    // Slerp 沿四维单位球的大圆插值，三维旋转角速度恒定；夹角很小时退化为 Nlerp。
    Quaternion<T> adjustedEnd = end;
    T cosine = Dot(start, adjustedEnd);
    if (cosine < static_cast<T>(0)) {
        cosine = -cosine;
        adjustedEnd = -adjustedEnd;
    }
    if (cosine > static_cast<T>(0.9995)) {
        return Nlerp(start, adjustedEnd, amount);
    }

    cosine = Clamp(cosine, static_cast<T>(-1), static_cast<T>(1));
    const T sine = std::sqrt(Max(
        static_cast<T>(0),
        static_cast<T>(1) - cosine * cosine));
    const T angle = std::atan2(sine, cosine);
    const T startWeight = std::sin((static_cast<T>(1) - amount) * angle) / sine;
    const T endWeight = std::sin(amount * angle) / sine;
    // 单位四元数的球面插值结果理论上仍为单位四元数。与 DirectXMath 相同，这里不再追加
    // Normalize，从热路径移除一次 sqrt 和除法；非单位输入应先由调用方显式 Normalize。
    return {
        start.x * startWeight + adjustedEnd.x * endWeight,
        start.y * startWeight + adjustedEnd.y * endWeight,
        start.z * startWeight + adjustedEnd.z * endWeight,
        start.w * startWeight + adjustedEnd.w * endWeight};
}

template <FloatingScalar T>
constexpr Matrix<T, 4, 4> TRSMatrix(const Vector<T, 3>& translation, const Quaternion<T>& rotation, const Vector<T, 3>& scale) noexcept {
    // 列向量约定下 T*R*S 表示顶点先缩放、再旋转、最后平移。
    return TranslationMatrix(translation) *
           Matrix4x4FromQuaternion(rotation) *
           ScaleMatrix(scale);
}

using floatQuaternion = Quaternion<float>;
using doubleQuaternion = Quaternion<double>;

} // namespace math
