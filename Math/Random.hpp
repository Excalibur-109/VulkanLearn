#pragma once

/**
 * @file Random.hpp
 * @brief 可复现伪随机序列、常见概率分布和图形学几何采样。
 *
 * 这些算法不用于密码学。引擎中应把 Random 实例按系统、线程或实体分流，而不是让所有任务
 * 争用一个全局生成器。相同 seed 与 stream 会得到相同序列，便于回放、测试和确定性生成。
 */

#include "Math/Quaternion.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>

namespace math {

/// SplitMix64 适合从一个种子快速扩散出多个高质量种子，也可独立用于非加密随机序列。
class SplitMix64 {
public:
    explicit constexpr SplitMix64(std::uint64_t seed = 0) noexcept
        : state_(seed) {
    }

    constexpr std::uint64_t NextU64() noexcept {
        std::uint64_t value = (state_ += 0x9E3779B97F4A7C15ULL);
        value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
        return value ^ (value >> 31U);
    }

private:
    std::uint64_t state_ = 0;
};

/// 极小状态的 XorShift64*，适合粒子等大量独立流，不用于安全、存档校验或网络密钥。
class XorShift64Star {
public:
    explicit constexpr XorShift64Star(std::uint64_t seed = 1) noexcept
        : state_(seed == 0 ? 0x9E3779B97F4A7C15ULL : seed) {
    }

    constexpr std::uint64_t NextU64() noexcept {
        state_ ^= state_ >> 12U;
        state_ ^= state_ << 25U;
        state_ ^= state_ >> 27U;
        return state_ * 0x2545F4914F6CDD1DULL;
    }

private:
    std::uint64_t state_ = 1;
};

/**
 * @brief PCG-XSH-RR 32 位生成器。
 *
 * state 决定序列位置，stream 决定彼此独立的随机流。游戏世界生成可固定 seed，任务系统
 * 可以给每个 worker 不同 stream，从而保证结果可复现且不会共享一个带锁全局生成器。
 */
class Pcg32 {
public:
    explicit Pcg32(
        std::uint64_t seed = 0x853C49E6748FEA9BULL,
        std::uint64_t stream = 0xDA3E39CB94B95BDBULL) noexcept {
        Seed(seed, stream);
    }

    void Seed(std::uint64_t seed, std::uint64_t stream = 1) noexcept {
        state_ = 0;
        increment_ = (stream << 1U) | 1U;
        static_cast<void>(NextU32());
        state_ += seed;
        static_cast<void>(NextU32());
    }

    std::uint32_t NextU32() noexcept {
        const std::uint64_t previous = state_;
        state_ = previous * 6364136223846793005ULL + increment_;
        const std::uint32_t xorShifted =
            static_cast<std::uint32_t>(((previous >> 18U) ^ previous) >> 27U);
        const std::uint32_t rotation = static_cast<std::uint32_t>(previous >> 59U);
        return std::rotr(xorShifted, static_cast<int>(rotation));
    }

    std::uint64_t NextU64() noexcept {
        return (static_cast<std::uint64_t>(NextU32()) << 32U) |
               static_cast<std::uint64_t>(NextU32());
    }

private:
    std::uint64_t state_ = 0;
    std::uint64_t increment_ = 1;
};

constexpr std::uint32_t Hash32(std::uint32_t value) noexcept {
    // avalanche hash：让输入任意一位变化扩散到多数输出位，适合从实体 ID 派生稳定随机值。
    value ^= value >> 16U;
    value *= 0x7FEB352DU;
    value ^= value >> 15U;
    value *= 0x846CA68BU;
    value ^= value >> 16U;
    return value;
}

constexpr std::uint64_t Hash64(std::uint64_t value) noexcept {
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return value;
}

constexpr float HashFloat01(std::uint32_t seed) noexcept {
    // 取 24 个高质量 bit，正好匹配 float 的有效尾数精度，结果范围为 [0, 1)。
    return static_cast<float>(Hash32(seed) >> 8U) * (1.0F / 16777216.0F);
}

class Random {
public:
    explicit Random(
        std::uint64_t seed = 0x853C49E6748FEA9BULL,
        std::uint64_t stream = 0xDA3E39CB94B95BDBULL) noexcept
        : generator_(seed, stream) {
    }

    void Seed(std::uint64_t seed, std::uint64_t stream = 1) noexcept {
        generator_.Seed(seed, stream);
        hasSpareNormal_ = false;
    }

    std::uint32_t UInt() noexcept {
        return generator_.NextU32();
    }

    std::uint64_t UInt64() noexcept {
        return generator_.NextU64();
    }

    /// 无 modulo bias 的 [0, exclusiveMaximum) 均匀整数。
    std::uint32_t UInt(std::uint32_t exclusiveMaximum) noexcept {
        if (exclusiveMaximum == 0) {
            return 0;
        }
        const std::uint32_t threshold =
            (std::uint32_t{0} - exclusiveMaximum) % exclusiveMaximum;
        for (;;) {
            const std::uint32_t value = UInt();
            if (value >= threshold) {
                return value % exclusiveMaximum;
            }
        }
    }

    std::uint64_t UInt64(std::uint64_t exclusiveMaximum) noexcept {
        if (exclusiveMaximum == 0) {
            return 0;
        }
        const std::uint64_t threshold =
            (std::uint64_t{0} - exclusiveMaximum) % exclusiveMaximum;
        for (;;) {
            const std::uint64_t value = UInt64();
            if (value >= threshold) {
                return value % exclusiveMaximum;
            }
        }
    }

    std::uint32_t UInt(
        std::uint32_t inclusiveMinimum,
        std::uint32_t exclusiveMaximum) noexcept {
        return exclusiveMaximum <= inclusiveMinimum
                   ? inclusiveMinimum
                   : inclusiveMinimum + UInt(exclusiveMaximum - inclusiveMinimum);
    }

    std::int32_t Int(
        std::int32_t inclusiveMinimum,
        std::int32_t exclusiveMaximum) noexcept {
        if (exclusiveMaximum <= inclusiveMinimum) {
            return inclusiveMinimum;
        }
        const std::uint64_t range =
            static_cast<std::uint64_t>(static_cast<std::int64_t>(exclusiveMaximum) -
                                       static_cast<std::int64_t>(inclusiveMinimum));
        return static_cast<std::int32_t>(
            static_cast<std::int64_t>(inclusiveMinimum) +
            static_cast<std::int64_t>(UInt64(range)));
    }

    bool Bool() noexcept {
        return (UInt() & 1U) != 0;
    }

    float Float01() noexcept {
        // float 只有 24 位有效精度，使用高 24 bit 可直接构造均匀的 [0,1) 网格。
        return static_cast<float>(UInt() >> 8U) * (1.0F / 16777216.0F);
    }

    double Double01() noexcept {
        return static_cast<double>(UInt64() >> 11U) *
               (1.0 / 9007199254740992.0);
    }

    float Float(float minimum, float maximum) noexcept {
        return Lerp(minimum, maximum, Float01());
    }

    double Double(double minimum, double maximum) noexcept {
        return Lerp(minimum, maximum, Double01());
    }

    bool Chance(float probability) noexcept {
        return Float01() < Saturate(probability);
    }

    /// Box-Muller 正态分布。一次计算生成两个样本，第二个缓存在对象中供下次调用。
    double Normal(double mean = 0.0, double standardDeviation = 1.0) noexcept {
        if (hasSpareNormal_) {
            hasSpareNormal_ = false;
            return mean + spareNormal_ * standardDeviation;
        }

        const double radius = std::sqrt(-2.0 * std::log(1.0 - Double01()));
        const double angle = TwoPi<double> * Double01();
        spareNormal_ = radius * std::sin(angle);
        hasSpareNormal_ = true;
        return mean + radius * std::cos(angle) * standardDeviation;
    }

    double Exponential(double rate) noexcept {
        // 逆变换采样：若 U 均匀分布于 [0,1)，则 -ln(1-U)/rate 服从指数分布。
        return rate <= 0.0
                   ? 0.0
                   : -std::log(1.0 - Double01()) / rate;
    }

    template <FloatingScalar T, std::size_t N>
    Vector<T, N> VectorRange(
        const Vector<T, N>& minimum,
        const Vector<T, N>& maximum) noexcept {
        Vector<T, N> output{};
        for (std::size_t index = 0; index < N; ++index) {
            if constexpr (std::same_as<T, float>) {
                output[index] = Float(minimum[index], maximum[index]);
            } else {
                output[index] = Double(minimum[index], maximum[index]);
            }
        }
        return output;
    }

    float2 OnUnitCircle() noexcept {
        const float angle = TwoPi<float> * Float01();
        return {std::cos(angle), std::sin(angle)};
    }

    float2 InsideUnitCircle() noexcept {
        // 面积随半径平方增长，因此半径必须取 sqrt(U)，直接用 U 会让样本过度集中在中心。
        return OnUnitCircle() * std::sqrt(Float01());
    }

    float3 OnUnitSphere() noexcept {
        const float z = Float(-1.0F, 1.0F);
        const float angle = TwoPi<float> * Float01();
        const float radial = std::sqrt(Max(0.0F, 1.0F - z * z));
        return {radial * std::cos(angle), radial * std::sin(angle), z};
    }

    float3 InsideUnitSphere() noexcept {
        // 球体体积随半径立方增长，所以内部均匀采样的半径是 cbrt(U)。
        return OnUnitSphere() * std::cbrt(Float01());
    }

    float3 OnHemisphere(const float3& normal) noexcept {
        float3 direction = OnUnitSphere();
        if (Dot(direction, normal) < 0.0F) {
            direction = -direction;
        }
        return direction;
    }

    float3 CosineWeightedHemisphere(const float3& normal) noexcept {
        // PDF=cos(theta)/pi，方向分布与 Lambert BRDF 的余弦项匹配，可降低蒙特卡洛方差。
        const float2 disk = InsideUnitCircle();
        const float z = std::sqrt(Max(0.0F, 1.0F - Dot(disk, disk)));
        const float3 n = NormalizeSafe(normal, float3(0.0F, 0.0F, 1.0F));
        const float3 helper = Abs(n.z) < 0.999F ? float3(0.0F, 0.0F, 1.0F)
                                                : float3(1.0F, 0.0F, 0.0F);
        const float3 tangent = Normalize(Cross(helper, n));
        const float3 bitangent = Cross(n, tangent);
        return Normalize(tangent * disk.x + bitangent * disk.y + n * z);
    }

    floatQuaternion UniformQuaternion() noexcept {
        // Shoemake 方法在四维单位球面上均匀采样，因此旋转不会偏向某些轴或角度。
        const float u1 = Float01();
        const float u2 = Float01();
        const float u3 = Float01();
        const float sqrtOneMinusU1 = std::sqrt(1.0F - u1);
        const float sqrtU1 = std::sqrt(u1);
        return {
            sqrtOneMinusU1 * std::sin(TwoPi<float> * u2),
            sqrtOneMinusU1 * std::cos(TwoPi<float> * u2),
            sqrtU1 * std::sin(TwoPi<float> * u3),
            sqrtU1 * std::cos(TwoPi<float> * u3)};
    }

    /// 三角形内均匀重心坐标，返回 (weightA, weightB, weightC)。
    float3 TriangleBarycentric() noexcept {
        const float sqrtU = std::sqrt(Float01());
        const float v = Float01();
        return {1.0F - sqrtU, sqrtU * (1.0F - v), sqrtU * v};
    }

    template <typename T>
    void Shuffle(std::span<T> values) noexcept {
        // Fisher-Yates：第 remaining 轮从未固定区间等概率挑选一个元素放到末尾。
        for (std::size_t remaining = values.size(); remaining > 1; --remaining) {
            const std::size_t selected =
                static_cast<std::size_t>(UInt64(static_cast<std::uint64_t>(remaining)));
            std::swap(values[remaining - 1], values[selected]);
        }
    }

    template <typename T>
    T* Choose(std::span<T> values) noexcept {
        return values.empty()
                   ? nullptr
                   : &values[static_cast<std::size_t>(UInt64(values.size()))];
    }

    template <typename T>
    const T* Choose(std::span<const T> values) noexcept {
        return values.empty()
                   ? nullptr
                   : &values[static_cast<std::size_t>(UInt64(values.size()))];
    }

    /// 权重可以不归一化；负权重按 0 处理。全为 0 时返回 weights.size()。
    std::size_t WeightedIndex(std::span<const float> weights) noexcept {
        float total = 0.0F;
        for (const float weight : weights) {
            total += Max(weight, 0.0F);
        }
        if (total <= 0.0F) {
            return weights.size();
        }

        float target = Float(0.0F, total);
        for (std::size_t index = 0; index < weights.size(); ++index) {
            const float weight = Max(weights[index], 0.0F);
            if (weight <= 0.0F) {
                continue;
            }
            if (target < weight) {
                return index;
            }
            target -= weight;
        }
        return weights.size() - 1;
    }

private:
    // Random 是分布层，Pcg32 是熵源；分开后可替换底层生成器而不重写采样算法。
    Pcg32 generator_;
    double spareNormal_ = 0.0;
    bool hasSpareNormal_ = false;
};

} // namespace math
