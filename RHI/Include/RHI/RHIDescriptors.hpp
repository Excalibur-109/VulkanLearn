#pragma once

#include "RHI/RHISynchronization.hpp"

#include <span>
#include <variant>
#include <vector>

namespace RHI
{

/** 定义着色器可见绑定的种类。*/
enum class RHIBindingType : std::uint8_t
{
    /** 只读常量/统一缓冲区。*/
    ConstantBuffer,
    /** 只读结构化或原始缓冲区。*/
    ReadOnlyBuffer,
    /** 读写结构化或原始缓冲区。*/
    ReadWriteBuffer,
    /** 只读采样纹理。*/
    SampledTexture,
    /** 读写存储纹理。*/
    StorageTexture,
    /** 过滤和坐标采样器状态。*/
    Sampler,
    /** 只读加速结构。*/
    AccelerationStructure,
};

/** 以位掩码定义布局标志。*/
enum class RHIBindLayoutFlags : std::uint8_t
{
    /** 无特殊布局行为。*/
    None = 0,
    /** 布局允许无绑。无界描述符数组。*/
    AllowBindless = 1u << 0u,
    /** 后端支持时，录制命令开始后仍可更新布局。*/
    UpdateAfterBind = 1u << 1u,
};

/** 合并绑定布局标志位。*/
[[nodiscard]] constexpr RHIBindLayoutFlags operator|(const RHIBindLayoutFlags left, const RHIBindLayoutFlags right) noexcept
{
    return static_cast<RHIBindLayoutFlags>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

/** 定义绑定组布局中的一个槽位。*/
struct RHIBindLayoutEntry final
{
    /** 对着色器可见的绑定编号。*/
    std::uint32_t Binding = 0;
    /** 此槽位接受的描述符种类。*/
    RHIBindingType Type = RHIBindingType::ConstantBuffer;
    /** 可访问此槽位的着色器阶段。*/
    RHIShaderStage Visibility = RHIShaderStage::All;
    /** 固定描述符数量；零表示无界数组。*/
    std::uint32_t Count = 1;
    /** 描述符数组可部分填充时为 true。*/
    bool PartiallyBound = false;
};

/** 描述由管线和绑定组共享的绑定组布局。*/
struct RHIBindLayoutDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 该布局声明的绑定槽位。*/
    std::vector<RHIBindLayoutEntry> Entries;
    /** 可选的高级布局行为。*/
    RHIBindLayoutFlags Flags = RHIBindLayoutFlags::None;
};

/** 定义管线布局中的内联常量范围。*/
struct RHIPushConstantRange final
{
    /** 管线全局常量空间中的首字节。*/
    std::uint32_t Offset = 0;
    /** 范围内的字节数。*/
    std::uint32_t SizeInBytes = 0;
    /** 允许读取该范围的着色器阶段。*/
    RHIShaderStage Visibility = RHIShaderStage::All;
};

/** 描述管线完整的资源接口。*/
struct RHIPipelineLayoutDesc final
{
    /** 按集合编号索引的绑定组布局。*/
    std::vector<RHIBindLayoutHandle> BindLayouts;
    /** 管线共享的内联常量范围。*/
    std::vector<RHIPushConstantRange> PushConstantRanges;
};

/** 指定写入绑定组的缓冲区范围。*/
struct RHIBufferBinding final
{
    /** 已绑定的缓冲区。*/
    RHIBufferHandle Buffer{};
    /** 首个可访问字节。*/
    std::uint64_t Offset = 0;
    /** 可访问字节数；MaxUint64 表示所有剩余字节。*/
    std::uint64_t Size = std::numeric_limits<std::uint64_t>::max();
};

/** 指定写入绑定组的纹理视图。*/
struct RHITextureBinding final
{
    /** 已绑定的纹理视图。*/
    RHITextureViewHandle View{};
};

/** 指定写入绑定组的采样器。*/
struct RHISamplerBinding final
{
    /** 已绑定的采样器状态。*/
    RHISamplerHandle Sampler{};
};

/** 指定写入绑定组的加速结构。*/
struct RHIAccelerationStructureBinding final
{
    /** 已绑定的加速结构。*/
    RHIAccelerationStructureHandle AccelerationStructure{};
};

/** 存储一次绑定组更新的资源载荷。*/
using RHIBindingResource = std::variant<RHIBufferBinding, RHITextureBinding, RHISamplerBinding, RHIAccelerationStructureBinding>;

/** 描述提供给绑定组的一个绑定值。*/
struct RHIBindSetWrite final
{
    /** 要更新的布局绑定编号。*/
    std::uint32_t Binding = 0;
    /** 要更新的首个数组元素。*/
    std::uint32_t ArrayElement = 0;
    /** 写入该绑定的资源载荷。*/
    RHIBindingResource Resource{};
};

/** 描述绑定组实例创建参数。*/
struct RHIBindSetDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 用于验证所提供绑定的布局。*/
    RHIBindLayoutHandle Layout{};
    /** 初始绑定值。*/
    std::vector<RHIBindSetWrite> InitialWrites;
};

} // namespace RHI
