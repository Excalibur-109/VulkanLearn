#pragma once

/**
 * @file Matrix.hpp
 * @brief 2x2 到 4x4 的矩阵、线性代数运算以及相机/物体变换矩阵。
 *
 * 本库统一采用“行主序存储 + 列向量右乘”：p' = M * p。组合矩阵 A * B 代表先应用 B，
 * 再应用 A。CPU 内存行列顺序与 shader 的矩阵解释是两个问题，上传 HLSL/GLSL 时应让
 * shader 布局声明、转置策略和这里的约定保持一致。
 */

#include "Math/Vector.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <utility>

namespace math {

/**
 * @brief R 行 C 列的行主序矩阵。
 *
 * Matrix[row][column] 与数学书写一致，矩阵乘列向量：result = matrix * vector。
 * `float3x4` 因此明确表示 3 行 4 列，而不是把 API 的内存布局概念混进类型名。
 */
template <Scalar T, std::size_t R, std::size_t C>
    requires(R >= 2 && R <= 4 && C >= 2 && C <= 4)
struct Matrix {
    using ValueType = T;
    static constexpr std::size_t RowCount = R;
    static constexpr std::size_t ColumnCount = C;

    std::array<Vector<T, C>, R> rows{};

    constexpr Matrix() noexcept = default;

    /// 方阵标量构造表示对角矩阵；例如 float4x4(1) 是单位矩阵。
    explicit constexpr Matrix(T diagonal) noexcept
        requires(R == C)
    {
        for (std::size_t index = 0; index < R; ++index) {
            rows[index][index] = diagonal;
        }
    }

    explicit constexpr Matrix(const std::array<Vector<T, C>, R>& rowValues) noexcept
        : rows(rowValues) {
    }

    template <typename... Values>
        requires(sizeof...(Values) == R * C &&
                 (std::convertible_to<Values, T> && ...))
    explicit constexpr Matrix(Values... values) noexcept {
        // 参数按数学阅读顺序逐行填写：m00,m01,...,m10,m11,...。
        const std::array<T, R * C> flattened{static_cast<T>(values)...};
        for (std::size_t row = 0; row < R; ++row) {
            for (std::size_t column = 0; column < C; ++column) {
                rows[row][column] = flattened[row * C + column];
            }
        }
    }

    template <Scalar U>
    explicit constexpr Matrix(const Matrix<U, R, C>& other) noexcept {
        for (std::size_t row = 0; row < R; ++row) {
            rows[row] = Vector<T, C>(other[row]);
        }
    }

    static constexpr Matrix Identity() noexcept
        requires(R == C)
    {
        return Matrix(static_cast<T>(1));
    }

    constexpr Vector<T, C>& operator[](std::size_t row) noexcept {
        assert(row < R);
        return rows[row];
    }

    constexpr const Vector<T, C>& operator[](std::size_t row) const noexcept {
        assert(row < R);
        return rows[row];
    }

    constexpr Vector<T, R> Column(std::size_t column) const noexcept {
        assert(column < C);
        Vector<T, R> output{};
        for (std::size_t row = 0; row < R; ++row) {
            output[row] = rows[row][column];
        }
        return output;
    }

    constexpr void SetColumn(std::size_t column, const Vector<T, R>& value) noexcept {
        assert(column < C);
        for (std::size_t row = 0; row < R; ++row) {
            rows[row][column] = value[row];
        }
    }
};

template <Scalar T, std::size_t R, std::size_t C>
constexpr bool operator==(
    const Matrix<T, R, C>& lhs,
    const Matrix<T, R, C>& rhs) noexcept {
    for (std::size_t row = 0; row < R; ++row) {
        if (lhs[row] != rhs[row]) {
            return false;
        }
    }
    return true;
}

template <Scalar T, std::size_t R, std::size_t C>
constexpr bool operator!=(
    const Matrix<T, R, C>& lhs,
    const Matrix<T, R, C>& rhs) noexcept {
    return !(lhs == rhs);
}

#define MATH_DEFINE_MATRIX_BINARY_OPERATOR(OPERATOR)                              \
    template <ArithmeticScalar L, ArithmeticScalar Rhs, std::size_t R, std::size_t C> \
    constexpr auto operator OPERATOR(                               \
        const Matrix<L, R, C>& lhs,                                               \
        const Matrix<Rhs, R, C>& rhs) noexcept {                                  \
        using Result = std::common_type_t<L, Rhs>;                                \
        Matrix<Result, R, C> output{};                                            \
        for (std::size_t row = 0; row < R; ++row) {                               \
            output[row] = lhs[row] OPERATOR rhs[row];                             \
        }                                                                          \
        return output;                                                             \
    }

MATH_DEFINE_MATRIX_BINARY_OPERATOR(+)
MATH_DEFINE_MATRIX_BINARY_OPERATOR(-)

#undef MATH_DEFINE_MATRIX_BINARY_OPERATOR

template <ArithmeticScalar T, std::size_t R, std::size_t C>
constexpr Matrix<T, R, C> operator-(const Matrix<T, R, C>& value) noexcept {
    Matrix<T, R, C> output{};
    for (std::size_t row = 0; row < R; ++row) {
        output[row] = -value[row];
    }
    return output;
}

template <ArithmeticScalar T, ArithmeticScalar U, std::size_t R, std::size_t C>
constexpr auto operator*(const Matrix<T, R, C>& matrix, U scalar) noexcept {
    using Result = std::common_type_t<T, U>;
    Matrix<Result, R, C> output{};
    for (std::size_t row = 0; row < R; ++row) {
        output[row] = matrix[row] * scalar;
    }
    return output;
}

template <ArithmeticScalar T, ArithmeticScalar U, std::size_t R, std::size_t C>
constexpr auto operator*(U scalar, const Matrix<T, R, C>& matrix) noexcept {
    return matrix * scalar;
}

template <ArithmeticScalar T, ArithmeticScalar U, std::size_t R, std::size_t C>
constexpr auto operator/(const Matrix<T, R, C>& matrix, U scalar) noexcept {
    using Result = std::common_type_t<T, U>;
    Matrix<Result, R, C> output{};
    for (std::size_t row = 0; row < R; ++row) {
        output[row] = matrix[row] / scalar;
    }
    return output;
}

template <ArithmeticScalar T, ArithmeticScalar U, std::size_t R, std::size_t C>
constexpr Matrix<T, R, C>& operator+=(Matrix<T, R, C>& lhs, const Matrix<U, R, C>& rhs) noexcept {
    for (std::size_t row = 0; row < R; ++row) {
        lhs[row] += rhs[row];
    }
    return lhs;
}

template <ArithmeticScalar T, ArithmeticScalar U, std::size_t R, std::size_t C>
constexpr Matrix<T, R, C>& operator-=(Matrix<T, R, C>& lhs, const Matrix<U, R, C>& rhs) noexcept {
    for (std::size_t row = 0; row < R; ++row) {
        lhs[row] -= rhs[row];
    }
    return lhs;
}

template <ArithmeticScalar T, ArithmeticScalar U, std::size_t R, std::size_t C>
constexpr Matrix<T, R, C>& operator*=(Matrix<T, R, C>& lhs, U scalar) noexcept {
    for (std::size_t row = 0; row < R; ++row) {
        lhs[row] *= scalar;
    }
    return lhs;
}

template <ArithmeticScalar T, ArithmeticScalar U, std::size_t R, std::size_t C>
constexpr Matrix<T, R, C>& operator/=(Matrix<T, R, C>& lhs, U scalar) noexcept {
    for (std::size_t row = 0; row < R; ++row) {
        lhs[row] /= scalar;
    }
    return lhs;
}

template <ArithmeticScalar L, ArithmeticScalar Rhs, std::size_t R, std::size_t C>
constexpr auto operator*(
    const Matrix<L, R, C>& matrix,
    const Vector<Rhs, C>& vector) noexcept {
    // 结果的第 row 项就是该行与列向量的点积。
    using Result = std::common_type_t<L, Rhs>;
    Vector<Result, R> output{};
    for (std::size_t row = 0; row < R; ++row) {
        output[row] = Dot(matrix[row], vector);
    }
    return output;
}

template <
    ArithmeticScalar L,
    ArithmeticScalar Rhs,
    std::size_t R,
    std::size_t Shared,
    std::size_t C>
constexpr auto operator*(
    const Matrix<L, R, Shared>& lhs,
    const Matrix<Rhs, Shared, C>& rhs) noexcept {
    // 矩阵乘法要求 lhs 列数 == rhs 行数；每个输出元素是 lhs 行与 rhs 列的点积。
    using Result = std::common_type_t<L, Rhs>;
    Matrix<Result, R, C> output{};
    for (std::size_t row = 0; row < R; ++row) {
        for (std::size_t column = 0; column < C; ++column) {
            output[row][column] = Dot(lhs[row], rhs.Column(column));
        }
    }
    return output;
}

template <ArithmeticScalar T, std::size_t R, std::size_t C>
constexpr Matrix<T, C, R> Transpose(const Matrix<T, R, C>& matrix) noexcept {
    // 转置交换行列。旋转矩阵为正交矩阵时，转置也等于逆矩阵。
    Matrix<T, C, R> output{};
    for (std::size_t row = 0; row < R; ++row) {
        for (std::size_t column = 0; column < C; ++column) {
            output[column][row] = matrix[row][column];
        }
    }
    return output;
}

template <ArithmeticScalar L, ArithmeticScalar Rhs, std::size_t R, std::size_t C>
constexpr auto Hadamard(
    const Matrix<L, R, C>& lhs,
    const Matrix<Rhs, R, C>& rhs) noexcept {
    using Result = std::common_type_t<L, Rhs>;
    Matrix<Result, R, C> output{};
    for (std::size_t row = 0; row < R; ++row) {
        output[row] = lhs[row] * rhs[row];
    }
    return output;
}

template <ArithmeticScalar T, std::size_t N>
constexpr T Trace(const Matrix<T, N, N>& matrix) noexcept {
    T output{};
    for (std::size_t index = 0; index < N; ++index) {
        output += matrix[index][index];
    }
    return output;
}

template <ArithmeticScalar T>
constexpr T Determinant(const Matrix<T, 2, 2>& matrix) noexcept {
    return matrix[0][0] * matrix[1][1] - matrix[0][1] * matrix[1][0];
}

template <ArithmeticScalar T>
constexpr T Determinant(const Matrix<T, 3, 3>& matrix) noexcept {
    return matrix[0][0] *
               (matrix[1][1] * matrix[2][2] - matrix[1][2] * matrix[2][1]) -
           matrix[0][1] *
               (matrix[1][0] * matrix[2][2] - matrix[1][2] * matrix[2][0]) +
           matrix[0][2] *
               (matrix[1][0] * matrix[2][1] - matrix[1][1] * matrix[2][0]);
}

template <ArithmeticScalar T>
constexpr T Determinant(const Matrix<T, 4, 4>& matrix) noexcept {
    // 沿第一行做 Laplace 展开；符号依次为 + - + -。
    const auto minor3 = [&](std::size_t skippedColumn) constexpr {
        Matrix<T, 3, 3> minor{};
        for (std::size_t row = 1; row < 4; ++row) {
            std::size_t outputColumn = 0;
            for (std::size_t column = 0; column < 4; ++column) {
                if (column != skippedColumn) {
                    minor[row - 1][outputColumn++] = matrix[row][column];
                }
            }
        }
        return Determinant(minor);
    };
    return matrix[0][0] * minor3(0) -
           matrix[0][1] * minor3(1) +
           matrix[0][2] * minor3(2) -
           matrix[0][3] * minor3(3);
}

/**
 * @brief Gauss-Jordan 带主元消去求逆。
 *
 * 每列选择绝对值最大的主元，比直接套伴随矩阵更能抵抗浮点误差。奇异矩阵返回 false，
 * 且不会修改 output，调用方不会无意得到 NaN/Inf 矩阵。
 */
template <FloatingScalar T, std::size_t N>
inline bool TryInverse(
    const Matrix<T, N, N>& matrix,
    Matrix<T, N, N>* output,
    T epsilon = std::numeric_limits<T>::epsilon() * static_cast<T>(16)) noexcept {
    if (output == nullptr) {
        return false;
    }
    Matrix<T, N, N> left = matrix;
    Matrix<T, N, N> right = Matrix<T, N, N>::Identity();

    for (std::size_t pivotColumn = 0; pivotColumn < N; ++pivotColumn) {
        std::size_t pivotRow = pivotColumn;
        T pivotMagnitude = Abs(left[pivotRow][pivotColumn]);
        for (std::size_t row = pivotColumn + 1; row < N; ++row) {
            const T candidateMagnitude = Abs(left[row][pivotColumn]);
            if (candidateMagnitude > pivotMagnitude) {
                pivotRow = row;
                pivotMagnitude = candidateMagnitude;
            }
        }
        if (pivotMagnitude <= epsilon) {
            return false;
        }

        if (pivotRow != pivotColumn) {
            std::swap(left[pivotRow], left[pivotColumn]);
            std::swap(right[pivotRow], right[pivotColumn]);
        }

        const T inversePivot = static_cast<T>(1) / left[pivotColumn][pivotColumn];
        left[pivotColumn] *= inversePivot;
        right[pivotColumn] *= inversePivot;

        for (std::size_t row = 0; row < N; ++row) {
            if (row == pivotColumn) {
                continue;
            }
            const T factor = left[row][pivotColumn];
            left[row] -= left[pivotColumn] * factor;
            right[row] -= right[pivotColumn] * factor;
        }
    }

    *output = right;
    return true;
}

template <FloatingScalar T, std::size_t N>
inline std::optional<Matrix<T, N, N>> Inverse(
    const Matrix<T, N, N>& matrix,
    T epsilon = std::numeric_limits<T>::epsilon() * static_cast<T>(16)) noexcept {
    Matrix<T, N, N> output{};
    if (!TryInverse(matrix, &output, epsilon)) {
        return std::nullopt;
    }
    return output;
}

template <Scalar To, Scalar From, std::size_t R, std::size_t C>
constexpr Matrix<To, R, C> MatrixCast(const Matrix<From, R, C>& matrix) noexcept {
    return Matrix<To, R, C>(matrix);
}

template <
    Scalar To,
    std::size_t NewRows,
    std::size_t NewColumns,
    Scalar From,
    std::size_t OldRows,
    std::size_t OldColumns>
constexpr Matrix<To, NewRows, NewColumns> ResizeMatrix(
    const Matrix<From, OldRows, OldColumns>& matrix,
    To addedDiagonal = static_cast<To>(1)) noexcept {
    // 扩展 3x3 旋转到 4x4 时，新对角元素设 1，齐次坐标 w 才能保持不变。
    Matrix<To, NewRows, NewColumns> output{};
    for (std::size_t index = 0; index < Min(NewRows, NewColumns); ++index) {
        output[index][index] = addedDiagonal;
    }
    for (std::size_t row = 0; row < Min(NewRows, OldRows); ++row) {
        for (std::size_t column = 0; column < Min(NewColumns, OldColumns); ++column) {
            output[row][column] = static_cast<To>(matrix[row][column]);
        }
    }
    return output;
}

template <FloatingScalar T>
constexpr Matrix<T, 4, 4> TranslationMatrix(const Vector<T, 3>& translation) noexcept {
    // 列向量约定下，平移位于最后一列；最后一行保持 (0,0,0,1)。
    Matrix<T, 4, 4> output = Matrix<T, 4, 4>::Identity();
    output[0][3] = translation.x;
    output[1][3] = translation.y;
    output[2][3] = translation.z;
    return output;
}

template <FloatingScalar T>
constexpr Matrix<T, 4, 4> ScaleMatrix(const Vector<T, 3>& scale) noexcept {
    Matrix<T, 4, 4> output{};
    output[0][0] = scale.x;
    output[1][1] = scale.y;
    output[2][2] = scale.z;
    output[3][3] = static_cast<T>(1);
    return output;
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> RotationXMatrix(T radians) noexcept {
    const T cosine = std::cos(radians);
    const T sine = std::sin(radians);
    return Matrix<T, 4, 4>(
        1, 0, 0, 0,
        0, cosine, -sine, 0,
        0, sine, cosine, 0,
        0, 0, 0, 1);
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> RotationYMatrix(T radians) noexcept {
    const T cosine = std::cos(radians);
    const T sine = std::sin(radians);
    return Matrix<T, 4, 4>(
        cosine, 0, sine, 0,
        0, 1, 0, 0,
        -sine, 0, cosine, 0,
        0, 0, 0, 1);
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> RotationZMatrix(T radians) noexcept {
    const T cosine = std::cos(radians);
    const T sine = std::sin(radians);
    return Matrix<T, 4, 4>(
        cosine, -sine, 0, 0,
        sine, cosine, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1);
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> RotationAxisMatrix(
    const Vector<T, 3>& axis,
    T radians) noexcept {
    // Rodrigues 旋转公式把“单位轴 + 角度”直接展开为 3x3 旋转块。
    const Vector<T, 3> unitAxis = NormalizeSafe(axis, Vector<T, 3>(1, 0, 0));
    const T x = unitAxis.x;
    const T y = unitAxis.y;
    const T z = unitAxis.z;
    const T cosine = std::cos(radians);
    const T sine = std::sin(radians);
    const T oneMinusCosine = static_cast<T>(1) - cosine;
    return Matrix<T, 4, 4>(
        cosine + x * x * oneMinusCosine,
        x * y * oneMinusCosine - z * sine,
        x * z * oneMinusCosine + y * sine,
        0,
        y * x * oneMinusCosine + z * sine,
        cosine + y * y * oneMinusCosine,
        y * z * oneMinusCosine - x * sine,
        0,
        z * x * oneMinusCosine - y * sine,
        z * y * oneMinusCosine + x * sine,
        cosine + z * z * oneMinusCosine,
        0,
        0,
        0,
        0,
        1);
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> LookAtRH(
    const Vector<T, 3>& eye,
    const Vector<T, 3>& target,
    const Vector<T, 3>& worldUp) noexcept {
    // View 矩阵不是相机世界矩阵，而是其逆：先投影到相机 right/up/forward 基，再消除 eye 平移。
    const Vector<T, 3> forward = NormalizeSafe(target - eye, Vector<T, 3>(0, 0, -1));
    const Vector<T, 3> right = NormalizeSafe(Cross(forward, worldUp), Vector<T, 3>(1, 0, 0));
    const Vector<T, 3> up = Cross(right, forward);
    return Matrix<T, 4, 4>(
        right.x, right.y, right.z, -Dot(right, eye),
        up.x, up.y, up.z, -Dot(up, eye),
        -forward.x, -forward.y, -forward.z, Dot(forward, eye),
        0, 0, 0, 1);
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> LookAtLH(
    const Vector<T, 3>& eye,
    const Vector<T, 3>& target,
    const Vector<T, 3>& worldUp) noexcept {
    const Vector<T, 3> forward = NormalizeSafe(target - eye, Vector<T, 3>(0, 0, 1));
    const Vector<T, 3> right = NormalizeSafe(Cross(worldUp, forward), Vector<T, 3>(1, 0, 0));
    const Vector<T, 3> up = Cross(forward, right);
    return Matrix<T, 4, 4>(
        right.x, right.y, right.z, -Dot(right, eye),
        up.x, up.y, up.z, -Dot(up, eye),
        forward.x, forward.y, forward.z, -Dot(forward, eye),
        0, 0, 0, 1);
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> PerspectiveRH_ZO(
    T verticalFovRadians,
    T aspectRatio,
    T nearPlane,
    T farPlane) noexcept {
    // ZO 表示透视除法后 NDC.z 属于 [0,1]；RH 相机前方在 view space 的 -Z。
    // focalLength=cot(fov/2)，它把视锥边界映射到 NDC 的 +/-1。
    const T focalLength = static_cast<T>(1) / std::tan(verticalFovRadians * static_cast<T>(0.5));
    const T inverseDepth = static_cast<T>(1) / (nearPlane - farPlane);
    Matrix<T, 4, 4> output{};
    output[0][0] = focalLength / aspectRatio;
    output[1][1] = focalLength;
    output[2][2] = farPlane * inverseDepth;
    output[2][3] = farPlane * nearPlane * inverseDepth;
    output[3][2] = static_cast<T>(-1);
    return output;
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> PerspectiveLH_ZO(
    T verticalFovRadians,
    T aspectRatio,
    T nearPlane,
    T farPlane) noexcept {
    const T focalLength = static_cast<T>(1) / std::tan(verticalFovRadians * static_cast<T>(0.5));
    const T inverseDepth = static_cast<T>(1) / (farPlane - nearPlane);
    Matrix<T, 4, 4> output{};
    output[0][0] = focalLength / aspectRatio;
    output[1][1] = focalLength;
    output[2][2] = farPlane * inverseDepth;
    output[2][3] = -farPlane * nearPlane * inverseDepth;
    output[3][2] = static_cast<T>(1);
    return output;
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> PerspectiveRH_NO(
    T verticalFovRadians,
    T aspectRatio,
    T nearPlane,
    T farPlane) noexcept {
    // NO 表示 NDC.z 属于 [-1,1]，是传统 OpenGL 的深度范围。
    const T focalLength = static_cast<T>(1) / std::tan(verticalFovRadians * static_cast<T>(0.5));
    const T inverseDepth = static_cast<T>(1) / (nearPlane - farPlane);
    Matrix<T, 4, 4> output{};
    output[0][0] = focalLength / aspectRatio;
    output[1][1] = focalLength;
    output[2][2] = (farPlane + nearPlane) * inverseDepth;
    output[2][3] = static_cast<T>(2) * farPlane * nearPlane * inverseDepth;
    output[3][2] = static_cast<T>(-1);
    return output;
}

template <FloatingScalar T>
inline Matrix<T, 4, 4> PerspectiveVulkanRH_ZO(
    T verticalFovRadians,
    T aspectRatio,
    T nearPlane,
    T farPlane) noexcept {
    // Vulkan 深度同样是 [0,1]；这里额外翻转 clip-space Y 以匹配本库采用的视口方向。
    Matrix<T, 4, 4> output =
        PerspectiveRH_ZO(verticalFovRadians, aspectRatio, nearPlane, farPlane);
    output[1][1] = -output[1][1];
    return output;
}

template <FloatingScalar T>
constexpr Matrix<T, 4, 4> OrthographicRH_ZO(
    T left,
    T right,
    T bottom,
    T top,
    T nearPlane,
    T farPlane) noexcept {
    return Matrix<T, 4, 4>(
        static_cast<T>(2) / (right - left),
        0,
        0,
        -(right + left) / (right - left),
        0,
        static_cast<T>(2) / (top - bottom),
        0,
        -(top + bottom) / (top - bottom),
        0,
        0,
        static_cast<T>(1) / (nearPlane - farPlane),
        nearPlane / (nearPlane - farPlane),
        0,
        0,
        0,
        1);
}

template <FloatingScalar T>
constexpr Matrix<T, 4, 4> OrthographicLH_ZO(
    T left,
    T right,
    T bottom,
    T top,
    T nearPlane,
    T farPlane) noexcept {
    return Matrix<T, 4, 4>(
        static_cast<T>(2) / (right - left),
        0,
        0,
        -(right + left) / (right - left),
        0,
        static_cast<T>(2) / (top - bottom),
        0,
        -(top + bottom) / (top - bottom),
        0,
        0,
        static_cast<T>(1) / (farPlane - nearPlane),
        -nearPlane / (farPlane - nearPlane),
        0,
        0,
        0,
        1);
}

template <FloatingScalar T>
constexpr Vector<T, 3> TransformPoint(
    const Matrix<T, 4, 4>& matrix,
    const Vector<T, 3>& point) noexcept {
    // 点使用齐次 w=1，所以平移生效；投影矩阵后还要除以 w 完成 perspective divide。
    const Vector<T, 4> homogeneous = matrix * Vector<T, 4>(point, static_cast<T>(1));
    return homogeneous.w == static_cast<T>(0)
               ? homogeneous.xyz()
               : homogeneous.xyz() / homogeneous.w;
}

template <FloatingScalar T>
constexpr Vector<T, 3> TransformVector(
    const Matrix<T, 4, 4>& matrix,
    const Vector<T, 3>& vector) noexcept {
    // 方向使用齐次 w=0，平移列对结果没有贡献。
    return (matrix * Vector<T, 4>(vector, static_cast<T>(0))).xyz();
}

using bool2x2 = Matrix<bool, 2, 2>;
using bool2x3 = Matrix<bool, 2, 3>;
using bool2x4 = Matrix<bool, 2, 4>;
using bool3x2 = Matrix<bool, 3, 2>;
using bool3x3 = Matrix<bool, 3, 3>;
using bool3x4 = Matrix<bool, 3, 4>;
using bool4x2 = Matrix<bool, 4, 2>;
using bool4x3 = Matrix<bool, 4, 3>;
using bool4x4 = Matrix<bool, 4, 4>;
using int2x2 = Matrix<std::int32_t, 2, 2>;
using int2x3 = Matrix<std::int32_t, 2, 3>;
using int2x4 = Matrix<std::int32_t, 2, 4>;
using int3x2 = Matrix<std::int32_t, 3, 2>;
using int3x3 = Matrix<std::int32_t, 3, 3>;
using int3x4 = Matrix<std::int32_t, 3, 4>;
using int4x2 = Matrix<std::int32_t, 4, 2>;
using int4x3 = Matrix<std::int32_t, 4, 3>;
using int4x4 = Matrix<std::int32_t, 4, 4>;
using uint2x2 = Matrix<std::uint32_t, 2, 2>;
using uint2x3 = Matrix<std::uint32_t, 2, 3>;
using uint2x4 = Matrix<std::uint32_t, 2, 4>;
using uint3x2 = Matrix<std::uint32_t, 3, 2>;
using uint3x3 = Matrix<std::uint32_t, 3, 3>;
using uint3x4 = Matrix<std::uint32_t, 3, 4>;
using uint4x2 = Matrix<std::uint32_t, 4, 2>;
using uint4x3 = Matrix<std::uint32_t, 4, 3>;
using uint4x4 = Matrix<std::uint32_t, 4, 4>;
using float2x2 = Matrix<float, 2, 2>;
using float2x3 = Matrix<float, 2, 3>;
using float2x4 = Matrix<float, 2, 4>;
using float3x2 = Matrix<float, 3, 2>;
using float3x3 = Matrix<float, 3, 3>;
using float3x4 = Matrix<float, 3, 4>;
using float4x2 = Matrix<float, 4, 2>;
using float4x3 = Matrix<float, 4, 3>;
using float4x4 = Matrix<float, 4, 4>;
using double2x2 = Matrix<double, 2, 2>;
using double2x3 = Matrix<double, 2, 3>;
using double2x4 = Matrix<double, 2, 4>;
using double3x2 = Matrix<double, 3, 2>;
using double3x3 = Matrix<double, 3, 3>;
using double3x4 = Matrix<double, 3, 4>;
using double4x2 = Matrix<double, 4, 2>;
using double4x3 = Matrix<double, 4, 3>;
using double4x4 = Matrix<double, 4, 4>;

} // namespace math

// Matrix 与常用变换函数默认可直接使用；关闭方式与 Vector.hpp 相同。
#if !defined(MATH_DISABLE_GLOBAL_NAMESPACE_EXPORTS)
using math::Matrix;
using math::bool2x2;
using math::bool2x3;
using math::bool2x4;
using math::bool3x2;
using math::bool3x3;
using math::bool3x4;
using math::bool4x2;
using math::bool4x3;
using math::bool4x4;
using math::int2x2;
using math::int2x3;
using math::int2x4;
using math::int3x2;
using math::int3x3;
using math::int3x4;
using math::int4x2;
using math::int4x3;
using math::int4x4;
using math::uint2x2;
using math::uint2x3;
using math::uint2x4;
using math::uint3x2;
using math::uint3x3;
using math::uint3x4;
using math::uint4x2;
using math::uint4x3;
using math::uint4x4;
using math::float2x2;
using math::float2x3;
using math::float2x4;
using math::float3x2;
using math::float3x3;
using math::float3x4;
using math::float4x2;
using math::float4x3;
using math::float4x4;
using math::double2x2;
using math::double2x3;
using math::double2x4;
using math::double3x2;
using math::double3x3;
using math::double3x4;
using math::double4x2;
using math::double4x3;
using math::double4x4;

using math::Determinant;
using math::Hadamard;
using math::Inverse;
using math::LookAtLH;
using math::LookAtRH;
using math::MatrixCast;
using math::OrthographicLH_ZO;
using math::OrthographicRH_ZO;
using math::PerspectiveLH_ZO;
using math::PerspectiveRH_NO;
using math::PerspectiveRH_ZO;
using math::PerspectiveVulkanRH_ZO;
using math::ResizeMatrix;
using math::RotationAxisMatrix;
using math::RotationXMatrix;
using math::RotationYMatrix;
using math::RotationZMatrix;
using math::ScaleMatrix;
using math::Trace;
using math::TransformPoint;
using math::TransformVector;
using math::TranslationMatrix;
using math::Transpose;
using math::TryInverse;
#endif
