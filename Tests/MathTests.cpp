#include "Math.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <type_traits>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename T>
bool Near(T lhs, T rhs, T epsilon = static_cast<T>(1.0e-5)) {
    return math::Abs(lhs - rhs) <= epsilon;
}

template <typename T, std::size_t N>
bool Near(
    const math::Vector<T, N>& lhs,
    const math::Vector<T, N>& rhs,
    T epsilon = static_cast<T>(1.0e-5)) {
    for (std::size_t index = 0; index < N; ++index) {
        if (!Near(lhs[index], rhs[index], epsilon)) {
            return false;
        }
    }
    return true;
}

template <typename T, std::size_t R, std::size_t C>
bool Near(
    const math::Matrix<T, R, C>& lhs,
    const math::Matrix<T, R, C>& rhs,
    T epsilon = static_cast<T>(1.0e-5)) {
    for (std::size_t row = 0; row < R; ++row) {
        if (!Near(lhs[row], rhs[row], epsilon)) {
            return false;
        }
    }
    return true;
}

void TestScalarFunctions() {
    Check(Near(math::Radians(180.0F), math::Pi<float>), "Radians conversion failed");
    Check(Near(math::Degrees(math::Pi<double>), 180.0), "Degrees conversion failed");
    Check(Near(math::SmoothStep(0.0F, 1.0F, 0.5F), 0.5F), "SmoothStep failed");
    Check(Near(math::SmootherStep(0.0, 1.0, 0.5), 0.5), "SmootherStep failed");
    Check(Near(math::Wrap(-1.0F, 0.0F, 4.0F), 3.0F), "Wrap failed");
    Check(Near(math::PingPong(5.0F, 3.0F), 1.0F), "PingPong failed");
    Check(math::IsPowerOfTwo(1024U), "IsPowerOfTwo rejected a power of two");
    Check(!math::IsPowerOfTwo(1023U), "IsPowerOfTwo accepted a non-power of two");
    Check(math::NextPowerOfTwo(1025U) == 2048U, "NextPowerOfTwo failed");
    Check(math::AlignUp(1025U, 256U) == 1280U, "AlignUp failed");
    Check(math::AlignDown(1025U, 256U) == 1024U, "AlignDown failed");
    Check(math::NearlyEqual(1.0F, 1.0F + 1.0e-7F), "NearlyEqual failed");
}

void TestGlobalNamespaceExports() {
    static_assert(std::is_same_v<Vector<float, 3>, float3>);
    static_assert(std::is_same_v<Matrix<float, 4, 4>, float4x4>);
    static_assert(std::is_same_v<Color, float4>);

    const float3 direction = Normalize(float3(0.0F, 3.0F, 4.0F));
    Check(Near(direction, float3(0.0F, 0.6F, 0.8F)), "Global Vector API failed");
    Check(Near(Dot(direction, direction), 1.0F), "Global Dot API failed");

    const float4x4 translation = TranslationMatrix(float3(2.0F, 3.0F, 4.0F));
    Check(
        Near(TransformPoint(translation, float3(1.0F)), float3(3.0F, 4.0F, 5.0F)),
        "Global Matrix API failed");

    const Color color(0.25F, 0.5F, 1.0F, 1.0F);
    const Color mapped(ToneMapACES(color.rgb()), color.w);
    Check(
        All(GreaterEqual(mapped, Color(0.0F))) && All(LessEqual(mapped, Color(1.0F))),
        "Global Color API failed");
}

void TestVectorAndSwizzle() {
    static_assert(std::is_standard_layout_v<math::float2>);
    static_assert(std::is_standard_layout_v<math::float3>);
    static_assert(std::is_standard_layout_v<math::float4>);
    static_assert(sizeof(math::float2) == sizeof(float) * 2);
    static_assert(sizeof(math::float3) == sizeof(float) * 3);
    static_assert(sizeof(math::float4) == sizeof(float) * 4);

    // 命名 swizzle 的返回维度只由名称长度决定，与原向量维度无关。
    static_assert(std::is_same_v<decltype(math::float2{}.gr()), math::float2>);
    static_assert(std::is_same_v<decltype(math::float3{}.bgr()), math::float3>);
    static_assert(std::is_same_v<decltype(math::float4{}.ragr()), math::float4>);

    const math::float4 value(1.0F, 2.0F, 3.0F, 4.0F);
    Check(value.xy() == math::float2(1.0F, 2.0F), "xy swizzle failed");
    Check(value.zyx() == math::float3(3.0F, 2.0F, 1.0F), "zyx swizzle failed");
    Check(value.xyzz() == math::float4(1.0F, 2.0F, 3.0F, 3.0F), "xyzz swizzle failed");
    Check(value.rgba() == value, "rgba swizzle failed");
    Check(value.bgra() == math::float4(3.0F, 2.0F, 1.0F, 4.0F), "bgra swizzle failed");
    Check(value.ar() == math::float2(4.0F, 1.0F), "ar swizzle failed");
    Check(value.bga() == math::float3(3.0F, 2.0F, 4.0F), "bga swizzle failed");
    Check(value.ragr() == math::float4(1.0F, 4.0F, 2.0F, 1.0F), "ragr swizzle failed");
    Check(value.aaaa() == math::float4(4.0F), "aaaa swizzle failed");
    Check(value.wwxy() == math::float4(4.0F, 4.0F, 1.0F, 2.0F), "wwxy swizzle failed");
    Check(
        value.Swizzle<3, 0, 2, 1>() == math::float4(4.0F, 1.0F, 3.0F, 2.0F),
        "Generic swizzle failed");

    const math::float3 rgb(1.0F, 2.0F, 3.0F);
    Check(rgb.br() == math::float2(3.0F, 1.0F), "float3 br swizzle failed");
    Check(rgb.gbr() == math::float3(2.0F, 3.0F, 1.0F), "float3 gbr swizzle failed");
    Check(rgb.bgrb() == math::float4(3.0F, 2.0F, 1.0F, 3.0F), "float3 bgrb swizzle failed");

    const math::float2 rg(1.0F, 2.0F);
    Check(rg.gr() == math::float2(2.0F, 1.0F), "float2 gr swizzle failed");
    Check(rg.rgg() == math::float3(1.0F, 2.0F, 2.0F), "float2 rgg swizzle failed");
    Check(rg.grrg() == math::float4(2.0F, 1.0F, 1.0F, 2.0F), "float2 grrg swizzle failed");

    math::int3 writable(1, 2, 3);
    writable.SetSwizzle<1, 0>(math::int2(8, 9));
    Check(writable == math::int3(9, 8, 3), "SetSwizzle failed");

    const math::float3 lhs(1.0F, 2.0F, 3.0F);
    const math::float3 rhs(4.0F, 5.0F, 6.0F);
    Check(lhs + rhs == math::float3(5.0F, 7.0F, 9.0F), "Vector addition failed");
    Check(lhs * 2.0F == math::float3(2.0F, 4.0F, 6.0F), "Vector scalar multiply failed");
    Check(Near(math::Dot(lhs, rhs), 32.0F), "Dot product failed");
    Check(
        math::Cross(math::float3(1.0F, 0.0F, 0.0F), math::float3(0.0F, 1.0F, 0.0F)) ==
            math::float3(0.0F, 0.0F, 1.0F),
        "Cross product failed");
    Check(Near(math::Length(math::float3(3.0F, 4.0F, 0.0F)), 5.0F), "Vector length failed");
    Check(
        Near(math::Normalize(math::float3(0.0F, 3.0F, 4.0F)), math::float3(0.0F, 0.6F, 0.8F)),
        "Normalize failed");
    Check(
        math::All(math::Less(math::float3(1.0F), math::float3(2.0F))),
        "Component comparison or All failed");
    Check(
        math::VectorCast<int>(math::float3(1.9F, -2.1F, 3.0F)) == math::int3(1, -2, 3),
        "VectorCast failed");
    Check(
        Near(
            math::Reflect(math::float3(1.0F, -1.0F, 0.0F), math::float3(0.0F, 1.0F, 0.0F)),
            math::float3(1.0F, 1.0F, 0.0F)),
        "Reflect failed");
    Check(
        Near(math::Sin(math::float2(0.0F, math::HalfPi<float>)), math::float2(0.0F, 1.0F)),
        "Component-wise Sin failed");
    Check(math::Sum(math::int4(1, 2, 3, 4)) == 10, "Vector Sum failed");
    Check(math::Product(math::int3(2, 3, 4)) == 24, "Vector Product failed");
    Check(
        math::Select(
            math::bool3(true, false, true),
            math::int3(1, 2, 3),
            math::int3(4, 5, 6)) == math::int3(1, 5, 3),
        "Vector Select failed");
}

void TestMatrixArithmeticAndInverse() {
    const math::float2x3 lhs(
        1.0F, 2.0F, 3.0F,
        4.0F, 5.0F, 6.0F);
    const math::float3x2 rhs(
        7.0F, 8.0F,
        9.0F, 10.0F,
        11.0F, 12.0F);
    const math::float2x2 product = lhs * rhs;
    Check(
        Near(product, math::float2x2(58.0F, 64.0F, 139.0F, 154.0F)),
        "Rectangular matrix multiplication failed");
    Check(
        Near(math::Transpose(lhs), math::float3x2(1.0F, 4.0F, 2.0F, 5.0F, 3.0F, 6.0F)),
        "Matrix transpose failed");

    const math::float3x3 matrix(
        1.0F, 2.0F, 3.0F,
        0.0F, 1.0F, 4.0F,
        5.0F, 6.0F, 0.0F);
    Check(Near(math::Determinant(matrix), 1.0F), "3x3 determinant failed");
    const std::optional<math::float3x3> inverse = math::Inverse(matrix);
    Check(inverse.has_value(), "An invertible matrix was reported singular");
    Check(
        Near(matrix * *inverse, math::float3x3::Identity(), 1.0e-4F),
        "Matrix inverse failed");

    const math::float2x2 singular(1.0F, 2.0F, 2.0F, 4.0F);
    Check(!math::Inverse(singular).has_value(), "A singular matrix produced an inverse");
    Check(
        !math::TryInverse(singular, static_cast<math::float2x2*>(nullptr)),
        "TryInverse accepted a null output pointer");

    const math::float4x4 determinant4(
        1.0F, 0.0F, 0.0F, 0.0F,
        0.0F, 2.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 3.0F, 0.0F,
        0.0F, 0.0F, 0.0F, 4.0F);
    Check(Near(math::Determinant(determinant4), 24.0F), "4x4 determinant failed");

    const math::double3x3 converted = math::MatrixCast<double>(matrix);
    Check(Near(converted[2][1], 6.0), "MatrixCast failed");
    const math::float4x4 resized = math::ResizeMatrix<float, 4, 4>(matrix);
    Check(Near(resized[3][3], 1.0F), "ResizeMatrix did not initialize added diagonal");
}

void TestTransformsAndProjection() {
    const math::float4x4 transform =
        math::TranslationMatrix(math::float3(2.0F, 3.0F, 4.0F)) *
        math::ScaleMatrix(math::float3(2.0F, 2.0F, 2.0F));
    Check(
        Near(
            math::TransformPoint(transform, math::float3(1.0F, 1.0F, 1.0F)),
            math::float3(4.0F, 5.0F, 6.0F)),
        "TransformPoint failed");
    Check(
        Near(
            math::TransformVector(transform, math::float3(1.0F, 1.0F, 1.0F)),
            math::float3(2.0F, 2.0F, 2.0F)),
        "TransformVector incorrectly applied translation");

    const math::float4x4 view = math::LookAtRH(
        math::float3(0.0F, 0.0F, 5.0F),
        math::float3(0.0F, 0.0F, 0.0F),
        math::float3(0.0F, 1.0F, 0.0F));
    Check(
        Near(math::TransformPoint(view, math::float3(0.0F)), math::float3(0.0F, 0.0F, -5.0F)),
        "LookAtRH failed");

    const math::float4x4 projection =
        math::PerspectiveRH_ZO(math::Radians(60.0F), 16.0F / 9.0F, 0.1F, 100.0F);
    const math::float4 nearClip = projection * math::float4(0.0F, 0.0F, -0.1F, 1.0F);
    const math::float4 farClip = projection * math::float4(0.0F, 0.0F, -100.0F, 1.0F);
    Check(Near(nearClip.z / nearClip.w, 0.0F, 1.0e-5F), "RH_ZO near plane mapping failed");
    Check(Near(farClip.z / farClip.w, 1.0F, 1.0e-5F), "RH_ZO far plane mapping failed");

    const math::float4x4 vulkanProjection =
        math::PerspectiveVulkanRH_ZO(math::Radians(60.0F), 1.0F, 0.1F, 100.0F);
    const math::float4x4 regularProjection =
        math::PerspectiveRH_ZO(math::Radians(60.0F), 1.0F, 0.1F, 100.0F);
    Check(
        Near(vulkanProjection[1][1], -regularProjection[1][1]),
        "Vulkan projection did not flip clip-space Y");
}

void TestQuaternion() {
    const math::floatQuaternion rotation = math::QuaternionFromAxisAngle(
        math::float3(0.0F, 0.0F, 1.0F),
        math::HalfPi<float>);
    Check(
        Near(
            math::Rotate(rotation, math::float3(1.0F, 0.0F, 0.0F)),
            math::float3(0.0F, 1.0F, 0.0F),
            1.0e-5F),
        "Quaternion vector rotation failed");

    const math::float3x3 rotationMatrix = math::Matrix3x3FromQuaternion(rotation);
    const math::floatQuaternion roundTrip = math::QuaternionFromMatrix(rotationMatrix);
    Check(
        Near(
            math::Rotate(roundTrip, math::float3(1.0F, 0.0F, 0.0F)),
            math::float3(0.0F, 1.0F, 0.0F),
            1.0e-5F),
        "Quaternion/matrix conversion failed");

    const math::floatQuaternion halfway =
        math::Slerp(math::floatQuaternion::Identity(), rotation, 0.5F);
    const math::float3 halfwayVector = math::Rotate(halfway, math::float3(1.0F, 0.0F, 0.0F));
    const float sqrtHalf = std::sqrt(0.5F);
    Check(
        Near(halfwayVector, math::float3(sqrtHalf, sqrtHalf, 0.0F), 1.0e-5F),
        "Quaternion Slerp failed");

    const math::float4x4 trs = math::TRSMatrix(
        math::float3(2.0F, 0.0F, 0.0F),
        rotation,
        math::float3(2.0F));
    Check(
        Near(
            math::TransformPoint(trs, math::float3(1.0F, 0.0F, 0.0F)),
            math::float3(2.0F, 2.0F, 0.0F),
            1.0e-5F),
        "TRS matrix composition failed");
}

void TestBezierCurves() {
    Check(
        Near(math::LinearBezier(2.0F, 6.0F, 0.25F), 3.0F),
        "Linear Bezier failed");
    Check(
        Near(math::QuadraticBezier(0.0F, 1.0F, 0.0F, 0.5F), 0.5F),
        "Quadratic Bezier failed");
    Check(
        Near(math::CubicBezier(0.0F, 1.0F, 1.0F, 0.0F, 0.5F), 0.75F),
        "Cubic Bezier failed");

    float bernsteinSum = 0.0F;
    for (std::size_t index = 0; index <= 5; ++index) {
        bernsteinSum += math::BernsteinBasis(5, index, 0.37F);
    }
    Check(Near(bernsteinSum, 1.0F), "Bernstein basis does not form a partition of unity");

    const std::array<math::float2, 4> controls{
        math::float2(0.0F, 0.0F),
        math::float2(1.0F, 2.0F),
        math::float2(3.0F, 2.0F),
        math::float2(4.0F, 0.0F)};
    for (int step = 0; step <= 20; ++step) {
        const float amount = static_cast<float>(step) / 20.0F;
        Check(
            Near(
                math::Bezier(std::span<const math::float2>(controls), amount),
                math::CubicBezier(
                    controls[0], controls[1], controls[2], controls[3], amount),
                1.0e-5F),
            "De Casteljau and cubic Bezier disagree");
    }

    Check(
        Near(
            math::CubicBezierDerivative(
                controls[0], controls[1], controls[2], controls[3], 0.0F),
            (controls[1] - controls[0]) * 3.0F),
        "Cubic Bezier start derivative failed");
    Check(
        Near(
            math::CubicBezierDerivative(
                controls[0], controls[1], controls[2], controls[3], 1.0F),
            (controls[3] - controls[2]) * 3.0F),
        "Cubic Bezier end derivative failed");
    const math::float2 cubicSecond = math::CubicBezierSecondDerivative(
        controls[0], controls[1], controls[2], controls[3], 0.25F);
    const math::float2 cubicThird = math::CubicBezierThirdDerivative(
        controls[0], controls[1], controls[2], controls[3]);
    Check(
        std::isfinite(cubicSecond.x) && std::isfinite(cubicSecond.y) &&
            std::isfinite(cubicThird.x) && std::isfinite(cubicThird.y),
        "Cubic Bezier higher derivative is not finite");

    const math::float2 quadraticSecond = math::QuadraticBezierSecondDerivative(
        math::float2(0.0F, 0.0F),
        math::float2(1.0F, 2.0F),
        math::float2(3.0F, 0.0F));
    Check(
        Near(quadraticSecond, math::float2(2.0F, -8.0F)),
        "Quadratic Bezier second derivative failed");

    const auto split = math::SplitCubicBezier(
        controls[0], controls[1], controls[2], controls[3], 0.35F);
    const math::float2 originalMiddle = math::CubicBezier(
        controls[0], controls[1], controls[2], controls[3], 0.35F);
    Check(
        Near(split[0][3], originalMiddle) && Near(split[1][0], originalMiddle),
        "Split cubic Bezier is not continuous");
    Check(
        Near(math::CubicBezier(
                 split[0][0], split[0][1], split[0][2], split[0][3], 1.0F),
             originalMiddle),
        "Left split Bezier has the wrong endpoint");
    const auto quadraticSplit = math::SplitQuadraticBezier(
        math::float2(0.0F, 0.0F),
        math::float2(1.0F, 2.0F),
        math::float2(3.0F, 0.0F),
        0.4F);
    Check(
        Near(quadraticSplit[0][2], quadraticSplit[1][0]),
        "Split quadratic Bezier is not continuous");

    const float circleWeight = std::sqrt(0.5F);
    const math::float2 quarterCircle = math::RationalQuadraticBezier(
        math::float2(1.0F, 0.0F),
        math::float2(1.0F, 1.0F),
        math::float2(0.0F, 1.0F),
        1.0F,
        circleWeight,
        1.0F,
        0.5F);
    Check(
        Near(quarterCircle, math::float2(circleWeight), 1.0e-5F),
        "Rational quadratic Bezier did not reproduce a quarter circle");
    Check(Near(math::Length(quarterCircle), 1.0F), "Rational Bezier point left the unit circle");

    const std::array<float, 3> rationalPoints{0.0F, 1.0F, 0.0F};
    const std::array<float, 3> unitWeights{1.0F, 1.0F, 1.0F};
    Check(
        Near(
            math::RationalBezier(
                std::span<const float>(rationalPoints),
                std::span<const float>(unitWeights),
                0.5F),
            0.5F),
        "Unit-weight rational Bezier differs from ordinary Bezier");
}

void TestSplineAndCurveGeometry() {
    const math::float2 point0(-1.0F, 0.0F);
    const math::float2 point1(0.0F, 1.0F);
    const math::float2 point2(2.0F, 1.0F);
    const math::float2 point3(4.0F, 0.0F);

    Check(
        Near(math::CatmullRom(point0, point1, point2, point3, 0.0F), point1),
        "Catmull-Rom does not start at point1");
    Check(
        Near(math::CatmullRom(point0, point1, point2, point3, 1.0F), point2),
        "Catmull-Rom does not end at point2");
    Check(
        Near(
            math::KochanekBartels(
                point0, point1, point2, point3, 0.4F, 0.0F, 0.0F, 0.0F),
            math::CatmullRom(point0, point1, point2, point3, 0.4F)),
        "Zero TCB parameters do not match Catmull-Rom");
    Check(
        Near(
            math::CatmullRomNonUniform(point0, point1, point2, point3, 0.0F),
            point1),
        "Centripetal Catmull-Rom start failed");
    Check(
        Near(
            math::CatmullRomNonUniform(point0, point1, point2, point3, 1.0F),
            point2),
        "Centripetal Catmull-Rom end failed");

    const math::float2 hermiteStart(0.0F, 0.0F);
    const math::float2 hermiteEnd(2.0F, 0.0F);
    const math::float2 startTangent(1.0F, 2.0F);
    const math::float2 endTangent(1.0F, -2.0F);
    Check(
        Near(math::CubicHermite(
                 hermiteStart, startTangent, hermiteEnd, endTangent, 0.0F),
             hermiteStart),
        "Hermite start point failed");
    Check(
        Near(math::CubicHermiteDerivative(
                 hermiteStart, startTangent, hermiteEnd, endTangent, 0.0F),
             startTangent),
        "Hermite start tangent failed");

    const math::float2 constant(3.0F, -2.0F);
    Check(
        Near(math::CubicBSpline(constant, constant, constant, constant, 0.37F), constant),
        "Constant cubic B-Spline changed value");

    Check(
        Near(
            math::Curvature(math::float2(1.0F, 0.0F), math::float2(0.0F, 1.0F)),
            1.0F),
        "2D curvature failed");
    Check(
        Near(
            math::Curvature(math::float3(1.0F, 0.0F, 0.0F),
                            math::float3(0.0F, 1.0F, 0.0F)),
            1.0F),
        "3D curvature failed");

    const float lineLength = math::ApproximateCurveLength(
        [](float amount) { return math::float3(3.0F, 4.0F, 0.0F) * amount; },
        32);
    Check(Near(lineLength, 5.0F), "Curve length approximation failed on a line");
    const float scalarLength = math::ApproximateCurveLength(
        [](float amount) { return -2.0F + amount * 7.0F; },
        16);
    Check(Near(scalarLength, 7.0F), "Scalar curve length approximation failed");
}

void TestEasingCurves() {
    const std::array<math::EaseCurve, 31> curves{
        math::EaseCurve::Linear,
        math::EaseCurve::InSine,
        math::EaseCurve::OutSine,
        math::EaseCurve::InOutSine,
        math::EaseCurve::InQuadratic,
        math::EaseCurve::OutQuadratic,
        math::EaseCurve::InOutQuadratic,
        math::EaseCurve::InCubic,
        math::EaseCurve::OutCubic,
        math::EaseCurve::InOutCubic,
        math::EaseCurve::InQuartic,
        math::EaseCurve::OutQuartic,
        math::EaseCurve::InOutQuartic,
        math::EaseCurve::InQuintic,
        math::EaseCurve::OutQuintic,
        math::EaseCurve::InOutQuintic,
        math::EaseCurve::InExponential,
        math::EaseCurve::OutExponential,
        math::EaseCurve::InOutExponential,
        math::EaseCurve::InCircular,
        math::EaseCurve::OutCircular,
        math::EaseCurve::InOutCircular,
        math::EaseCurve::InBack,
        math::EaseCurve::OutBack,
        math::EaseCurve::InOutBack,
        math::EaseCurve::InElastic,
        math::EaseCurve::OutElastic,
        math::EaseCurve::InOutElastic,
        math::EaseCurve::InBounce,
        math::EaseCurve::OutBounce,
        math::EaseCurve::InOutBounce};

    for (const math::EaseCurve curve : curves) {
        Check(Near(math::EvaluateEase(curve, 0.0F), 0.0F), "Easing start is not zero");
        Check(Near(math::EvaluateEase(curve, 1.0F), 1.0F), "Easing end is not one");
        for (int step = 0; step <= 20; ++step) {
            const float value = math::EvaluateEase(curve, static_cast<float>(step) / 20.0F);
            Check(std::isfinite(value), "Easing produced a non-finite value");
        }
    }
    Check(Near(math::EaseLinear(-1.0F), 0.0F), "Easing did not clamp below zero");
    Check(Near(math::EaseLinear(2.0F), 1.0F), "Easing did not clamp above one");
}

void TestColorMath() {
    const math::float3 srgb(0.02F, 0.5F, 1.0F);
    Check(
        Near(math::LinearToSRGB(math::SRGBToLinear(srgb)), srgb, 1.0e-5F),
        "sRGB/linear round trip failed");

    const math::float3 rgb(0.2F, 0.7F, 0.4F);
    Check(
        Near(math::HSVToRGB(math::RGBToHSV(rgb)), rgb, 1.0e-5F),
        "RGB/HSV round trip failed");
    Check(
        Near(math::Luminance(math::float3(1.0F)), 1.0F, 1.0e-5F),
        "Rec.709 luminance weights do not sum to one");
    Check(
        Near(math::ApplyExposure(math::float3(0.25F), 2.0F), math::float3(1.0F)),
        "Exposure in stops failed");

    const math::float3 aces = math::ToneMapACES(math::float3(0.0F, 1.0F, 100.0F));
    Check(
        math::All(math::GreaterEqual(aces, math::float3(0.0F))) &&
            math::All(math::LessEqual(aces, math::float3(1.0F))),
        "ACES tone mapping left the display range");

    const math::float4 rgba(0.0F, 0.5F, 1.0F, 0.25F);
    const math::float4 unpacked = math::UnpackRGBA8UNorm(math::PackRGBA8UNorm(rgba));
    Check(Near(unpacked, rgba, 1.0F / 255.0F), "RGBA8 pack/unpack failed");
    Check(
        Near(
            math::UnpremultiplyAlpha(math::PremultiplyAlpha(rgba)),
            rgba,
            1.0e-5F),
        "Premultiplied alpha round trip failed");
}

void TestRenderingMath() {
    const math::float3 normal = math::Normalize(math::float3(0.3F, -0.4F, 0.8F));
    const math::floatTangentFrame frame = math::BuildTangentFrame(normal);
    Check(Near(math::Length(frame.tangent), 1.0F), "Tangent is not normalized");
    Check(Near(math::Length(frame.bitangent), 1.0F), "Bitangent is not normalized");
    Check(Near(math::Dot(frame.tangent, frame.normal), 0.0F), "Tangent is not orthogonal");
    Check(Near(math::Dot(frame.bitangent, frame.normal), 0.0F), "Bitangent is not orthogonal");
    Check(
        Near(
            math::DecodeNormalOctahedral(math::EncodeNormalOctahedral(normal)),
            normal,
            1.0e-5F),
        "Octahedral normal round trip failed");
    const std::array<math::float3, 6> cardinalNormals{
        math::float3(1.0F, 0.0F, 0.0F),
        math::float3(-1.0F, 0.0F, 0.0F),
        math::float3(0.0F, 1.0F, 0.0F),
        math::float3(0.0F, -1.0F, 0.0F),
        math::float3(0.0F, 0.0F, 1.0F),
        math::float3(0.0F, 0.0F, -1.0F)};
    for (const math::float3& cardinalNormal : cardinalNormals) {
        Check(
            Near(
                math::DecodeNormalOctahedral(math::EncodeNormalOctahedral(cardinalNormal)),
                cardinalNormal,
                1.0e-5F),
            "Octahedral encoding failed on a cardinal axis");
    }

    const math::float4x4 nonUniformScale =
        math::ScaleMatrix(math::float3(2.0F, 1.0F, 0.5F));
    const std::optional<math::float3x3> normalMatrix = math::NormalMatrix(nonUniformScale);
    Check(normalMatrix.has_value(), "Normal matrix rejected an invertible transform");
    const math::float3 localNormal = math::Normalize(math::float3(1.0F, 1.0F, 0.0F));
    const math::float3 expectedNormal = math::Normalize(math::float3(0.5F, 1.0F, 0.0F));
    Check(
        Near(math::TransformNormal(nonUniformScale, localNormal), expectedNormal),
        "Inverse-transpose normal transform failed");

    math::floatPBRMaterialSample material{};
    material.baseColor = math::float3(0.8F, 0.3F, 0.1F);
    material.metallic = 0.2F;
    material.roughness = 0.45F;
    const math::float3 brdf = math::EvaluateCookTorrance(
        material,
        math::float3(0.0F, 0.0F, 1.0F),
        math::float3(0.0F, 0.0F, 1.0F),
        math::Normalize(math::float3(1.0F, 0.0F, 1.0F)));
    Check(
        std::isfinite(brdf.x) && std::isfinite(brdf.y) && std::isfinite(brdf.z),
        "Cook-Torrance produced a non-finite value");
    Check(
        math::All(math::GreaterEqual(brdf, math::float3(0.0F))),
        "Cook-Torrance produced negative radiance");
    Check(math::DistributionGGX(1.0F, 0.5F) > 0.0F, "GGX distribution is invalid");
    Check(
        Near(
            math::FresnelSchlick(1.0F, math::float3(0.04F)),
            math::float3(0.04F)),
        "Schlick Fresnel normal-incidence value failed");

    math::floatPointLightData pointLight{};
    pointLight.position = math::float3(0.0F, 0.0F, 2.0F);
    pointLight.range = 10.0F;
    const math::floatLightSample nearPoint =
        math::SampleLight(pointLight, math::float3(0.0F, 0.0F, 1.0F));
    const math::floatLightSample farPoint =
        math::SampleLight(pointLight, math::float3(0.0F, 0.0F, -2.0F));
    Check(nearPoint.radiance.x > farPoint.radiance.x, "Point-light attenuation is reversed");

    Check(
        Near(
            math::SpotConeAttenuation(
                math::float3(0.0F, 0.0F, -1.0F),
                math::float3(0.0F, 0.0F, -1.0F),
                math::Radians(20.0F),
                math::Radians(30.0F)),
            1.0F),
        "Spot-light center should have full intensity");
    Check(
        Near(
            math::SpotConeAttenuation(
                math::float3(1.0F, 0.0F, 0.0F),
                math::float3(0.0F, 0.0F, -1.0F),
                math::Radians(20.0F),
                math::Radians(30.0F)),
            0.0F),
        "Spot-light outside cone should have zero intensity");

    Check(Near(math::ShadowBias(1.0F, 0.001F, 0.01F, 0.02F), 0.001F), "Shadow bias failed");
    Check(
        math::VarianceShadowVisibility(0.4F, 0.5F, 0.26F) == 1.0F,
        "VSM incorrectly shadowed a front receiver");
    Check(
        Near(math::FogTransmittanceLinear(5.0F, 0.0F, 10.0F), 0.5F),
        "Linear fog transmittance failed");
}

void TestRenderingReconstructionAndSampling() {
    const math::float4x4 projection =
        math::PerspectiveRH_ZO(math::Radians(60.0F), 1.0F, 0.1F, 100.0F);
    const math::float4 nearClip = projection * math::float4(0.0F, 0.0F, -0.1F, 1.0F);
    const math::float4 farClip = projection * math::float4(0.0F, 0.0F, -100.0F, 1.0F);
    Check(
        Near(math::ReconstructViewZ(nearClip.z / nearClip.w, projection), -0.1F),
        "Near view-Z reconstruction failed");
    Check(
        Near(math::ReconstructViewZ(farClip.z / farClip.w, projection), -100.0F, 1.0e-2F),
        "Far view-Z reconstruction failed");

    const std::optional<math::float4x4> inverseProjection = math::Inverse(projection);
    Check(inverseProjection.has_value(), "Projection matrix inversion failed");
    Check(
        Near(
            math::ReconstructViewPosition(math::float2(0.0F), 0.0F, *inverseProjection),
            math::float3(0.0F, 0.0F, -0.1F),
            1.0e-5F),
        "View-position reconstruction failed");
    Check(
        Near(
            math::PixelCenterToNDC(math::float2(49.5F), math::float2(100.0F)),
            math::float2(0.0F)),
        "Pixel-center to NDC conversion failed");

    for (std::uint32_t index = 0; index < 32; ++index) {
        const math::float2 sample = math::Hammersley2D(index, 32);
        Check(
            sample.x >= 0.0F && sample.x < 1.0F && sample.y >= 0.0F && sample.y < 1.0F,
            "Hammersley sample left the unit square");
        Check(
            Near(math::Length(math::CosineSampleHemisphere(sample)), 1.0F, 1.0e-5F),
            "Cosine hemisphere sample is not normalized");
        Check(
            Near(
                math::Length(math::ImportanceSampleGGX(sample, 0.5F, math::float3(0, 0, 1))),
                1.0F,
                1.0e-5F),
            "GGX importance sample is not normalized");
    }

    math::floatSH9Color coefficients{};
    coefficients[0] = math::float3(2.0F, 3.0F, 4.0F);
    const math::float3 shDirection = math::Normalize(math::float3(1.0F, 2.0F, 3.0F));
    Check(
        Near(
            math::EvaluateSphericalHarmonics9(coefficients, shDirection),
            coefficients[0] * 0.282095F),
        "SH9 constant coefficient evaluation failed");
}

void TestRandom() {
    math::Random first(123456U, 99U);
    math::Random second(123456U, 99U);
    for (int index = 0; index < 64; ++index) {
        Check(first.UInt() == second.UInt(), "Equal Random seeds produced different sequences");
    }

    math::Random random(42U, 7U);
    double normalSum = 0.0;
    for (int index = 0; index < 20000; ++index) {
        const std::uint32_t unsignedValue = random.UInt(10U, 20U);
        const std::int32_t signedValue = random.Int(-20, -10);
        const float unit = random.Float01();
        Check(unsignedValue >= 10U && unsignedValue < 20U, "UInt range is incorrect");
        Check(signedValue >= -20 && signedValue < -10, "Int range is incorrect");
        Check(unit >= 0.0F && unit < 1.0F, "Float01 range is incorrect");
        normalSum += random.Normal();
    }
    Check(math::Abs(normalSum / 20000.0) < 0.04, "Normal distribution mean is implausible");

    for (int index = 0; index < 256; ++index) {
        Check(Near(math::Length(random.OnUnitSphere()), 1.0F, 1.0e-5F), "OnUnitSphere failed");
        Check(math::LengthSquared(random.InsideUnitSphere()) <= 1.00001F, "InsideUnitSphere failed");
        Check(Near(math::Length(random.UniformQuaternion()), 1.0F, 1.0e-5F), "UniformQuaternion failed");
        const math::float3 barycentric = random.TriangleBarycentric();
        Check(
            barycentric.x >= 0.0F && barycentric.y >= 0.0F && barycentric.z >= 0.0F &&
                Near(barycentric.x + barycentric.y + barycentric.z, 1.0F),
            "Triangle barycentric sampling failed");
    }

    std::array<int, 8> values{0, 1, 2, 3, 4, 5, 6, 7};
    random.Shuffle<int>(values);
    std::array<int, 8> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    Check(sorted == std::array<int, 8>{0, 1, 2, 3, 4, 5, 6, 7}, "Shuffle lost values");
    Check(random.Choose<int>(values) != nullptr, "Choose failed on a non-empty span");

    const std::array<float, 3> weights{0.0F, 1.0F, 0.0F};
    Check(random.WeightedIndex(weights) == 1, "WeightedIndex ignored deterministic weights");

    math::SplitMix64 splitA(123U);
    math::SplitMix64 splitB(123U);
    Check(splitA.NextU64() == splitB.NextU64(), "SplitMix64 is not deterministic");
    math::XorShift64Star xorShift(123U);
    Check(xorShift.NextU64() != 0, "XorShift64Star produced an invalid zero stream");
    Check(math::Hash32(123U) == math::Hash32(123U), "Hash32 is not deterministic");
    Check(math::HashFloat01(123U) < 1.0F, "HashFloat01 range is incorrect");
}

} // namespace

int main() {
    try {
        TestScalarFunctions();
        TestGlobalNamespaceExports();
        TestVectorAndSwizzle();
        TestMatrixArithmeticAndInverse();
        TestTransformsAndProjection();
        TestQuaternion();
        TestBezierCurves();
        TestSplineAndCurveGeometry();
        TestEasingCurves();
        TestColorMath();
        TestRenderingMath();
        TestRenderingReconstructionAndSampling();
        TestRandom();
        std::cout << "All Math tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
