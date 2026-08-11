#pragma once

#include "RHI/RHIResources.hpp"

#include <span>
#include <variant>
#include <vector>

namespace RHI
{

/** 定义同步依赖使用的管线阶段点。*/
enum class RHIPipelineStage : std::uint32_t
{
    /** 未指定阶段。*/
    None = 0,
    /** 已提交工作的顶部。*/
    Top = 1u << 0u,
    /** 间接参数读取。*/
    DrawIndirect = 1u << 1u,
    /** 顶点输入提取。*/
    VertexInput = 1u << 2u,
    /** 顶点着色器执行。*/
    VertexShader = 1u << 3u,
    /** 曲面细分着色器执行。*/
    TessellationShader = 1u << 4u,
    /** 几何着色器执行。*/
    GeometryShader = 1u << 5u,
    /** 光栅化及早期深度/模板测试。*/
    EarlyDepthStencil = 1u << 6u,
    /** 像素着色器执行。*/
    PixelShader = 1u << 7u,
    /** 后期深度/模板测试。*/
    LateDepthStencil = 1u << 8u,
    /** 颜色附件输出。*/
    ColorOutput = 1u << 9u,
    /** 计算着色器执行。*/
    ComputeShader = 1u << 10u,
    /** 传输操作。*/
    Copy = 1u << 11u,
    /** 光线追踪着色器执行。*/
    RayTracingShader = 1u << 12u,
    /** 加速结构构建。*/
    AccelerationStructureBuild = 1u << 13u,
    /** 已提交工作的底部。*/
    Bottom = 1u << 14u,
    /** 所有图形管线阶段。*/
    AllGraphics = (1u << 2u) | (1u << 3u) | (1u << 4u) | (1u << 5u) | (1u << 6u) | (1u << 7u) | (1u << 8u) | (1u << 9u),
    /** 此抽象层支持的所有阶段。*/
    All = 0x7fffffffu,
};

/** 合并管线阶段位。*/
[[nodiscard]] constexpr RHIPipelineStage operator|(const RHIPipelineStage left, const RHIPipelineStage right) noexcept
{
    return static_cast<RHIPipelineStage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

/** 定义同步信号量的行为。*/
enum class RHISemaphoreType : std::uint8_t
{
    /** 信号量仅触发和消耗一次。*/
    Binary,
    /** 信号量跟踪单调递增的整数值。*/
    Timeline,
};

/** 定义查询池中存储的查询类型。*/
enum class RHIQueryType : std::uint8_t
{
    /** 测量已用 GPU 时间戳计数。*/
    Timestamp,
    /** 统计通过深度/模板测试的采样数。*/
    Occlusion,
    /** 收集实现定义的管线统计数据。*/
    PipelineStatistics,
};

/** 描述同步围栏创建参数。*/
struct RHIFenceDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 围栏初始为已触发状态时。true。*/
    bool InitiallySignaled = false;
};

/** 描述同步信号量创建参数。*/
struct RHISemaphoreDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 信号量触发模型。*/
    RHISemaphoreType Type = RHISemaphoreType::Binary;
    /** 时间线信号量的初始值。*/
    std::uint64_t InitialValue = 0;
};

/** 描述查询池创建参数。*/
struct RHIQueryPoolDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 此查询池记录的查询种类。*/
    RHIQueryType Type = RHIQueryType::Timestamp;
    /** 查询槽位数量。*/
    std::uint32_t Count = 0;
};

/** 指定执行已提交命令列表前的信号量等待。*/
struct RHISemaphoreWait final
{
    /** 要等待的信号量。*/
    RHISemaphoreHandle Semaphore{};
    /** 要等待的时间线值；二元信号量忽略该值。*/
    std::uint64_t Value = 0;
    /** 消费依赖数据的最早阶段。*/
    RHIPipelineStage WaitStages = RHIPipelineStage::All;
};

/** 指定执行已提交命令列表后的信号量触发。*/
struct RHISemaphoreSignal final
{
    /** 要触发的信号量。*/
    RHISemaphoreHandle Semaphore{};
    /** 要触发的时间线值；二元信号量忽略该值。*/
    std::uint64_t Value = 0;
};

/** 描述全部或部分缓冲区的状态转换。*/
struct RHIBufferBarrier final
{
    /** 访问状态将变化的缓冲区。*/
    RHIBufferHandle Buffer{};
    /** 屏障前状态。*/
    RHIResourceState Before = RHIResourceState::Undefined;
    /** 屏障后状态。*/
    RHIResourceState After = RHIResourceState::Undefined;
    /** 受转换影响的首字节。*/
    std::uint64_t Offset = 0;
    /** 受影响的字节数；MaxUint64 表示剩余范围。*/
    std::uint64_t Size = std::numeric_limits<std::uint64_t>::max();
};

/** 描述全部或部分纹理的状态转换。*/
struct RHITextureBarrier final
{
    /** 访问状态将变化的纹理。*/
    RHITextureHandle Texture{};
    /** 屏障前状态。*/
    RHIResourceState Before = RHIResourceState::Undefined;
    /** 屏障后状态。*/
    RHIResourceState After = RHIResourceState::Undefined;
    /** 受转换影响的 mip 和数组层范围。*/
    RHISubresourceRange Range{};
};

/** 描述无序访问写入的排序屏障。*/
struct RHIUnorderedAccessBarrier final
{
    /** 可选的待排序缓冲区；无效句柄表示全局生效。*/
    RHIBufferHandle Buffer{};
    /** 可选的待排序纹理；无效句柄表示全局生效。*/
    RHITextureHandle Texture{};
};

/** 描述两个资源之间别名内存的复用。*/
struct RHIAliasingBarrier final
{
    /** 此前占用该内存的缓冲区；内容未知时为无效句柄。*/
    RHIBufferHandle BeforeBuffer{};
    /** 此前占用该内存的纹理；内容未知时为无效句柄。*/
    RHITextureHandle BeforeTexture{};
    /** 新占用该内存的缓冲区；纹理情况为无效句柄。*/
    RHIBufferHandle AfterBuffer{};
    /** 新占用该内存的纹理；缓冲区情况为无效句柄。*/
    RHITextureHandle AfterTexture{};
};

/** 存储命令列表屏障批次中的一个屏障操作。*/
using RHIBarrier = std::variant<RHIBufferBarrier, RHITextureBarrier, RHIUnorderedAccessBarrier, RHIAliasingBarrier>;

} // namespace RHI
