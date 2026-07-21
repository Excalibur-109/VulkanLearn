#include "Math.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <DirectXMath.h>
#endif

namespace {

using Clock = std::chrono::steady_clock;

// 最终结果写入 volatile，保证编译器不能把只有数学运算、没有外部副作用的循环整体删除。
volatile double benchmarkSink = 0.0;

struct BenchmarkResult {
    std::string_view name;
    double medianNanoseconds = 0.0;
    double minimumNanoseconds = 0.0;
    std::uint64_t iterations = 0;
};

void Consume(double value) noexcept {
    std::atomic_signal_fence(std::memory_order_seq_cst);
    benchmarkSink = value;
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

template <typename Operation, typename Checksum>
double MeasureNanoseconds(
    Operation& operation,
    Checksum& checksum,
    std::uint64_t iterations) {
    const auto start = Clock::now();
    for (std::uint64_t index = 0; index < iterations; ++index) {
        operation();
    }
    const auto end = Clock::now();
    Consume(static_cast<double>(checksum()));
    return std::chrono::duration<double, std::nano>(end - start).count();
}

template <typename Operation, typename Checksum>
BenchmarkResult RunBenchmark(
    std::string_view name,
    Operation operation,
    Checksum checksum) {
    constexpr double targetCalibrationNanoseconds = 40'000'000.0;
    constexpr std::uint64_t maximumIterations = 1ULL << 31U;
    constexpr std::size_t sampleCount = 7;

    for (std::size_t index = 0; index < 4096; ++index) {
        operation();
    }
    Consume(static_cast<double>(checksum()));

    std::uint64_t iterations = 1024;
    for (;;) {
        const double elapsed = MeasureNanoseconds(operation, checksum, iterations);
        if (elapsed >= targetCalibrationNanoseconds || iterations >= maximumIterations) {
            break;
        }

        const double estimate = targetCalibrationNanoseconds / std::max(elapsed, 1.0);
        const auto multiplier = static_cast<std::uint64_t>(std::clamp(estimate, 2.0, 16.0));
        iterations = std::min(maximumIterations, iterations * multiplier);
    }

    std::array<double, sampleCount> samples{};
    for (double& sample : samples) {
        sample = MeasureNanoseconds(operation, checksum, iterations) /
                 static_cast<double>(iterations);
    }
    std::sort(samples.begin(), samples.end());

    return {
        name,
        samples[sampleCount / 2],
        samples.front(),
        iterations};
}

void PrintResult(const BenchmarkResult& result) {
    const double millionOperationsPerSecond = 1000.0 / result.medianNanoseconds;
    std::cout << std::left << std::setw(30) << result.name
              << std::right << std::setw(12) << std::fixed << std::setprecision(3)
              << result.medianNanoseconds
              << std::setw(12) << result.minimumNanoseconds
              << std::setw(14) << std::setprecision(2) << millionOperationsPerSecond
              << std::setw(14) << result.iterations << '\n';
}

} // namespace

int main() {
    std::vector<BenchmarkResult> results;
    results.reserve(26);

    float scalar = 0.25F;
    results.push_back(RunBenchmark(
        "Scalar recurrence (baseline)",
        [&] {
            scalar = scalar * 1.000001F + 0.000003F;
            if (scalar > 8.0F) {
                scalar = 0.25F;
            }
        },
        [&] { return scalar; }));

    float3 vectorAdd(1.0F, 2.0F, 3.0F);
    const float3 vectorDelta(0.0001F, 0.0002F, 0.0003F);
    results.push_back(RunBenchmark(
        "float3 add",
        [&] {
            vectorAdd += vectorDelta;
            if (vectorAdd.x > 16.0F) {
                vectorAdd -= float3(15.0F);
            }
        },
        [&] { return vectorAdd.x + vectorAdd.y + vectorAdd.z; }));

    float3 dotInput(0.25F, 0.5F, 0.75F);
    const float3 dotOther(0.37F, 0.61F, 0.83F);
    float dotResult = 0.0F;
    results.push_back(RunBenchmark(
        "float3 Dot",
        [&] {
            dotResult = Dot(dotInput, dotOther);
            dotInput.x = dotResult * 0.125F + 0.1F;
        },
        [&] { return dotResult + dotInput.x; }));

    float4 swizzleValue(1.0F, 2.0F, 3.0F, 4.0F);
    results.push_back(RunBenchmark(
        "float4 bgar swizzle",
        [&] { swizzleValue = swizzleValue.bgar(); },
        [&] { return swizzleValue.x + swizzleValue.w; }));

    float3 normalValue(0.37F, 0.61F, 0.83F);
    const float3 normalDelta(0.00001F, -0.00002F, 0.00003F);
    results.push_back(RunBenchmark(
        "float3 NormalizeSafe",
        [&] { normalValue = NormalizeSafe(normalValue + normalDelta, float3(0.0F, 0.0F, 1.0F)); },
        [&] { return normalValue.x + normalValue.y + normalValue.z; }));

#if defined(_WIN32)
    DirectX::XMVECTOR dxNormal = DirectX::XMVectorSet(0.37F, 0.61F, 0.83F, 0.0F);
    const DirectX::XMVECTOR dxNormalDelta =
        DirectX::XMVectorSet(0.00001F, -0.00002F, 0.00003F, 0.0F);
    results.push_back(RunBenchmark(
        "DirectX XMVector3Normalize",
        [&] { dxNormal = DirectX::XMVector3Normalize(DirectX::XMVectorAdd(dxNormal, dxNormalDelta)); },
        [&] { return DirectX::XMVectorGetX(dxNormal) + DirectX::XMVectorGetY(dxNormal); }));
#endif

    const float4x4 matrixVectorTransform =
        RotationYMatrix(0.0007F) * RotationXMatrix(0.0003F);
    float4 transformedVector(0.25F, 0.5F, 0.75F, 1.0F);
    results.push_back(RunBenchmark(
        "float4x4 * float4",
        [&] { transformedVector = matrixVectorTransform * transformedVector; },
        [&] { return transformedVector.x + transformedVector.y + transformedVector.z; }));

#if defined(_WIN32)
    const DirectX::XMMATRIX dxMatrixVectorTransform =
        DirectX::XMMatrixRotationY(0.0007F) * DirectX::XMMatrixRotationX(0.0003F);
    DirectX::XMVECTOR dxTransformedVector = DirectX::XMVectorSet(0.25F, 0.5F, 0.75F, 1.0F);
    results.push_back(RunBenchmark(
        "DirectX XMVector4Transform",
        [&] {
            dxTransformedVector =
                DirectX::XMVector4Transform(dxTransformedVector, dxMatrixVectorTransform);
        },
        [&] {
            return DirectX::XMVectorGetX(dxTransformedVector) +
                   DirectX::XMVectorGetY(dxTransformedVector);
        }));
#endif

    const float4x4 incrementalRotation =
        RotationZMatrix(0.0002F) * RotationYMatrix(0.0003F);
    float4x4 multipliedMatrix = float4x4::Identity();
    results.push_back(RunBenchmark(
        "float4x4 * float4x4",
        [&] { multipliedMatrix = multipliedMatrix * incrementalRotation; },
        [&] { return multipliedMatrix[0][0] + multipliedMatrix[2][3]; }));

#if defined(_WIN32)
    const DirectX::XMMATRIX dxIncrementalRotation =
        DirectX::XMMatrixRotationZ(0.0002F) * DirectX::XMMatrixRotationY(0.0003F);
    DirectX::XMMATRIX dxMultipliedMatrix = DirectX::XMMatrixIdentity();
    results.push_back(RunBenchmark(
        "DirectX XMMatrixMultiply",
        [&] { dxMultipliedMatrix = DirectX::XMMatrixMultiply(dxMultipliedMatrix, dxIncrementalRotation); },
        [&] { return DirectX::XMVectorGetX(dxMultipliedMatrix.r[0]); }));
#endif

    float4x4 inverseInput =
        TranslationMatrix(float3(2.0F, 3.0F, 4.0F)) *
        RotationYMatrix(0.7F) *
        ScaleMatrix(float3(1.2F, 0.8F, 1.5F));
    float4x4 inverseOutput{};
    bool inverseSucceeded = false;
    results.push_back(RunBenchmark(
        "TryInverse float4x4",
        [&] {
            inverseSucceeded = TryInverse(inverseInput, &inverseOutput);
            inverseInput = inverseOutput;
        },
        [&] { return inverseSucceeded ? inverseOutput[0][0] : 0.0F; }));

#if defined(_WIN32)
    DirectX::XMMATRIX dxInverseInput =
        DirectX::XMMatrixScaling(1.2F, 0.8F, 1.5F) *
        DirectX::XMMatrixRotationY(0.7F) *
        DirectX::XMMatrixTranslation(2.0F, 3.0F, 4.0F);
    DirectX::XMVECTOR dxDeterminant{};
    results.push_back(RunBenchmark(
        "DirectX XMMatrixInverse",
        [&] { dxInverseInput = DirectX::XMMatrixInverse(&dxDeterminant, dxInverseInput); },
        [&] { return DirectX::XMVectorGetX(dxInverseInput.r[0]); }));
#endif

    float4x4 projectiveInverseInput =
        math::PerspectiveRH_ZO(math::Radians(67.0F), 16.0F / 9.0F, 0.1F, 500.0F);
    float4x4 projectiveInverseOutput{};
    bool projectiveInverseSucceeded = false;
    results.push_back(RunBenchmark(
        "TryInverse projective 4x4",
        [&] {
            projectiveInverseSucceeded =
                TryInverse(projectiveInverseInput, &projectiveInverseOutput);
            projectiveInverseInput = projectiveInverseOutput;
        },
        [&] {
            return projectiveInverseSucceeded ? projectiveInverseOutput[0][0] : 0.0F;
        }));

#if defined(_WIN32)
    DirectX::XMMATRIX dxProjectiveInverseInput =
        DirectX::XMMatrixPerspectiveFovRH(DirectX::XMConvertToRadians(67.0F), 16.0F / 9.0F, 0.1F, 500.0F);
    DirectX::XMVECTOR dxProjectiveDeterminant{};
    results.push_back(RunBenchmark(
        "DirectX projective inverse",
        [&] {
            dxProjectiveInverseInput = DirectX::XMMatrixInverse(
                &dxProjectiveDeterminant,
                dxProjectiveInverseInput);
        },
        [&] { return DirectX::XMVectorGetX(dxProjectiveInverseInput.r[0]); }));
#endif

    const math::floatQuaternion rotation =
        math::QuaternionFromAxisAngle(Normalize(float3(1.0F, 2.0F, 3.0F)), 0.001F);
    float3 rotatedVector(0.3F, 0.5F, 0.7F);
    results.push_back(RunBenchmark(
        "Quaternion Rotate float3",
        [&] { rotatedVector = math::Rotate(rotation, rotatedVector); },
        [&] { return rotatedVector.x + rotatedVector.y + rotatedVector.z; }));

#if defined(_WIN32)
    const DirectX::XMVECTOR dxRotationAxis =
        DirectX::XMVector3Normalize(DirectX::XMVectorSet(1.0F, 2.0F, 3.0F, 0.0F));
    const DirectX::XMVECTOR dxRotation =
        DirectX::XMQuaternionRotationAxis(dxRotationAxis, 0.001F);
    DirectX::XMVECTOR dxRotatedVector = DirectX::XMVectorSet(0.3F, 0.5F, 0.7F, 0.0F);
    results.push_back(RunBenchmark(
        "DirectX XMVector3Rotate",
        [&] { dxRotatedVector = DirectX::XMVector3Rotate(dxRotatedVector, dxRotation); },
        [&] {
            return DirectX::XMVectorGetX(dxRotatedVector) +
                   DirectX::XMVectorGetY(dxRotatedVector);
        }));
#endif

    const math::floatQuaternion slerpStart =
        math::QuaternionFromEulerXYZ(float3(0.1F, 0.2F, 0.3F));
    const math::floatQuaternion slerpEnd =
        math::QuaternionFromEulerXYZ(float3(1.1F, -0.7F, 0.8F));
    math::floatQuaternion slerpResult{};
    float interpolation = 0.0F;
    results.push_back(RunBenchmark(
        "Quaternion Slerp",
        [&] {
            interpolation += 0.0001F;
            if (interpolation > 1.0F) {
                interpolation -= 1.0F;
            }
            slerpResult = math::Slerp(slerpStart, slerpEnd, interpolation);
        },
        [&] { return slerpResult.x + slerpResult.w; }));

#if defined(_WIN32)
    const DirectX::XMVECTOR dxSlerpStart = DirectX::XMQuaternionRotationRollPitchYaw(0.1F, 0.2F, 0.3F);
    const DirectX::XMVECTOR dxSlerpEnd = DirectX::XMQuaternionRotationRollPitchYaw(1.1F, -0.7F, 0.8F);
    DirectX::XMVECTOR dxSlerpResult{};
    float dxInterpolation = 0.0F;
    results.push_back(RunBenchmark(
        "DirectX XMQuaternionSlerp",
        [&] {
            dxInterpolation += 0.0001F;
            if (dxInterpolation > 1.0F) {
                dxInterpolation -= 1.0F;
            }
            dxSlerpResult =
                DirectX::XMQuaternionSlerp(dxSlerpStart, dxSlerpEnd, dxInterpolation);
        },
        [&] {
            return DirectX::XMVectorGetX(dxSlerpResult) +
                   DirectX::XMVectorGetW(dxSlerpResult);
        }));
#endif

    const std::array<float3, 6> controlPoints{
        float3(0.0F, 0.0F, 0.0F),
        float3(1.0F, 2.0F, 0.0F),
        float3(2.0F, -1.0F, 1.0F),
        float3(3.0F, 1.5F, 2.0F),
        float3(4.0F, 0.5F, 1.0F),
        float3(5.0F, 0.0F, 0.0F)};
    float curveAmount = 0.0F;
    float3 curveResult{};
    results.push_back(RunBenchmark(
        "CubicBezier float3",
        [&] {
            curveAmount += 0.0001F;
            if (curveAmount > 1.0F) {
                curveAmount -= 1.0F;
            }
            curveResult = CubicBezier(
                controlPoints[0], controlPoints[1], controlPoints[2], controlPoints[3], curveAmount);
        },
        [&] { return curveResult.x + curveResult.y; }));

    results.push_back(RunBenchmark(
        "Bezier 6 points (span)",
        [&] {
            curveAmount += 0.0001F;
            if (curveAmount > 1.0F) {
                curveAmount -= 1.0F;
            }
            curveResult = Bezier(
                std::span<const float3>(controlPoints.data(), controlPoints.size()),
                curveAmount);
        },
        [&] { return curveResult.x + curveResult.y; }));

    results.push_back(RunBenchmark(
        "Bezier 6 points (array)",
        [&] {
            curveAmount += 0.0001F;
            if (curveAmount > 1.0F) {
                curveAmount -= 1.0F;
            }
            curveResult = Bezier(controlPoints, curveAmount);
        },
        [&] { return curveResult.x + curveResult.y; }));

    math::Pcg32 random(0x12345678ULL, 7ULL);
    std::uint32_t randomResult = 0;
    results.push_back(RunBenchmark(
        "Pcg32 NextU32",
        [&] { randomResult = random.NextU32(); },
        [&] { return static_cast<double>(randomResult); }));

    math::PBRMaterialSample<float> material{};
    material.baseColor = float3(0.8F, 0.3F, 0.1F);
    material.metallic = 0.35F;
    const float3 surfaceNormal = Normalize(float3(0.2F, 0.9F, 0.3F));
    const float3 viewDirection = Normalize(float3(0.1F, 0.4F, 1.0F));
    const float3 lightDirection = Normalize(float3(-0.3F, 0.8F, 0.5F));
    float3 pbrResult{};
    results.push_back(RunBenchmark(
        "EvaluateCookTorrance",
        [&] {
            material.roughness += 0.0001F;
            if (material.roughness > 0.95F) {
                material.roughness = 0.05F;
            }
            pbrResult = math::EvaluateCookTorrance(
                material,
                surfaceNormal,
                viewDirection,
                lightDirection);
        },
        [&] { return pbrResult.x + pbrResult.y + pbrResult.z; }));

    std::cout << "Math microbenchmarks (Release, median of 7 samples)\n\n";
    std::cout << std::left << std::setw(30) << "Operation"
              << std::right << std::setw(12) << "median ns"
              << std::setw(12) << "min ns"
              << std::setw(14) << "M ops/s"
              << std::setw(14) << "iterations" << '\n';
    std::cout << std::string(82, '-') << '\n';
    for (const BenchmarkResult& result : results) {
        PrintResult(result);
    }

    std::cout << "\nSink: " << benchmarkSink << '\n';
    return 0;
}
