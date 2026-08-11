#pragma once

#include "RHI/RHIHandles.hpp"

#include <array>
#include <span>
#include <variant>

namespace RHI
{

/** 定义纹理资源的维度。*/
enum class RHITextureDimension : std::uint8_t
{
    /** 一维纹理。*/
    Texture1D,
    /** 二维纹理。*/
    Texture2D,
    /** 三维纹理。*/
    Texture3D,
};

/** 以位掩码定义缓冲区允许的操作。*/
enum class RHIBufferUsage : std::uint32_t
{
    /** 不允许特殊操作。*/
    None = 0,
    /** 缓冲区可绑定为顶点输入。*/
    Vertex = 1u << 0u,
    /** 缓冲区可绑定为索引输入。*/
    Index = 1u << 1u,
    /** 缓冲区可绑定为常。统一缓冲区。*/
    Constant = 1u << 2u,
    /** 缓冲区可由着色器读取。*/
    ShaderRead = 1u << 3u,
    /** 缓冲区可由着色器读写。*/
    ShaderWrite = 1u << 4u,
    /** 缓冲区可用于传输操作。*/
    Copy = 1u << 5u,
    /** 缓冲区可存储间接绘制或调度参数。*/
    Indirect = 1u << 6u,
    /** 缓冲区可存储加速结构输入或输出数据。*/
    AccelerationStructure = 1u << 7u,
    /** 缓冲区可存储查询结果。*/
    QueryResolve = 1u << 8u,
};

/** 合并缓冲区用途位。*/
[[nodiscard]] constexpr RHIBufferUsage operator|(const RHIBufferUsage left, const RHIBufferUsage right) noexcept
{
    return static_cast<RHIBufferUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

/** 以位掩码定义纹理允许的操作。*/
enum class RHITextureUsage : std::uint32_t
{
    /** 不允许特殊操作。*/
    None = 0,
    /** 纹理可由着色器采样。*/
    ShaderRead = 1u << 0u,
    /** 纹理可由着色器读写。*/
    ShaderWrite = 1u << 1u,
    /** 纹理可作为颜色附件。*/
    ColorAttachment = 1u << 2u,
    /** 纹理可作为深。模板附件。*/
    DepthStencilAttachment = 1u << 3u,
    /** 纹理可用于传输操作。*/
    Copy = 1u << 4u,
    /** 纹理可呈现到显示表面。*/
    Present = 1u << 5u,
    /** 纹理可存储可变速率着色数据。*/
    ShadingRate = 1u << 6u,
    /** 纹理可存储加速结构数据。*/
    AccelerationStructure = 1u << 7u,
};

/** 合并纹理用途位。*/
[[nodiscard]] constexpr RHITextureUsage operator|(const RHITextureUsage left, const RHITextureUsage right) noexcept
{
    return static_cast<RHITextureUsage>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

/** 定义渲染区域开始时附件的初始化方式。*/
enum class RHILoadOp : std::uint8_t
{
    /** 保留此前附件内容。*/
    Load,
    /** 用清除值替换附件内容。*/
    Clear,
    /** 丢弃此前附件内容。*/
    Discard,
};

/** 定义渲染区域结束后附件内容的处理方式。*/
enum class RHIStoreOp : std::uint8_t
{
    /** 附件内容在该区域结束后仍可使用。*/
    Store,
    /** 附件内容可在该区域结束后丢弃。*/
    Discard,
};

/** 定义访问纹理时使用的视图维度。*/
enum class RHITextureViewDimension : std::uint8_t
{
    /** 查看一维纹素。*/
    Texture1D,
    /** 查看一维纹理数组。*/
    Texture1DArray,
    /** 查看二维纹素。*/
    Texture2D,
    /** 查看二维纹理数组。*/
    Texture2DArray,
    /** 查看立方体纹理。*/
    TextureCube,
    /** 查看立方体纹理数组。*/
    TextureCubeArray,
    /** 查看三维纹素。*/
    Texture3D,
};

/** 定义使用边框寻址的采样器边框颜色。*/
enum class RHIBorderColor : std::uint8_t
{
    /** 透明黑色边框。*/
    TransparentBlack,
    /** 不透明黑色边框。*/
    OpaqueBlack,
    /** 不透明白色边框。*/
    OpaqueWhite,
};

/** 描述缓冲区分配。*/
struct RHIBufferDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 分配大小，单位为字节。*/
    std::uint64_t SizeInBytes = 0;
    /** 缓冲区支持的操作。*/
    RHIBufferUsage Usage = RHIBufferUsage::None;
    /** 预期的分配访问模式。*/
    RHIMemoryUsage MemoryUsage = RHIMemoryUsage::GpuOnly;
    /** 初始抽象访问状态。*/
    RHIResourceState InitialState = RHIResourceState::Undefined;
    /** 临时分配可别名复用该资源时为 true。*/
    bool AllowAliasing = false;
};

/** 描述纹理分配。*/
struct RHITextureDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 纹理维度。*/
    RHITextureDimension Dimension = RHITextureDimension::Texture2D;
    /** mip 级别零的纹素尺寸。*/
    RHIExtent3D Extent{};
    /** 数组层数量。*/
    std::uint32_t ArrayLayers = 1;
    /** mip 级别数量。*/
    std::uint32_t MipLevels = 1;
    /** 光栅采样数量。*/
    RHISampleCount SampleCount = RHISampleCount::Count1;
    /** 纹素格式。*/
    RHIFormat Format = RHIFormat::Unknown;
    /** 纹理支持的操作。*/
    RHITextureUsage Usage = RHITextureUsage::None;
    /** 预期的分配访问模式。*/
    RHIMemoryUsage MemoryUsage = RHIMemoryUsage::GpuOnly;
    /** 初始抽象访问状态。*/
    RHIResourceState InitialState = RHIResourceState::Undefined;
    /** 临时分配可别名复用该资源时为 true。*/
    bool AllowAliasing = false;
};

/** 描述覆盖纹理子范围的纹理视图。*/
struct RHITextureViewDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 该视图所引用的纹理资源。*/
    RHITextureHandle Texture{};
    /** 视图维度。*/
    RHITextureViewDimension Dimension = RHITextureViewDimension::Texture2D;
    /** 覆盖格式；Unknown 表示使用纹理自身格式。*/
    RHIFormat Format = RHIFormat::Unknown;
    /** 视图包含。mip 和数组层范围。*/
    RHISubresourceRange Range{};
};

/** 描述采样器过滤和纹理坐标行为。*/
struct RHISamplerDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 缩小过滤方式。*/
    RHIFilter MinFilter = RHIFilter::Linear;
    /** 放大过滤方式。*/
    RHIFilter MagFilter = RHIFilter::Linear;
    /** mip 级别过滤方式。*/
    RHIFilter MipFilter = RHIFilter::Linear;
    /** U 坐标寻址模式。*/
    RHIAddressMode AddressU = RHIAddressMode::Wrap;
    /** V 坐标寻址模式。*/
    RHIAddressMode AddressV = RHIAddressMode::Wrap;
    /** W 坐标寻址模式。*/
    RHIAddressMode AddressW = RHIAddressMode::Wrap;
    /** mip 选择前应用的 LOD 偏移。*/
    float MipLodBias = 0.0f;
    /** 最大各向异性；一表示关闭各向异性过滤。*/
    float MaxAnisotropy = 1.0f;
    /** 比较采样使用的比较操作。*/
    RHICompareOp CompareOp = RHICompareOp::Never;
    /** 最小可访问 mip 级别。*/
    float MinLod = 0.0f;
    /** 最大可访问 mip 级别。*/
    float MaxLod = std::numeric_limits<float>::max();
    /** 采用边框寻址时，纹理外部采样得到的颜色。*/
    RHIBorderColor BorderColor = RHIBorderColor::TransparentBlack;
};

/** 存储颜色清除值。*/
struct RHIClearColorValue final
{
    /** 线性红色分量。*/
    float R = 0.0f;
    /** 线性绿色分量。*/
    float G = 0.0f;
    /** 线性蓝色分量。*/
    float B = 0.0f;
    /** 线。Alpha 分量。*/
    float A = 1.0f;
};

/** 存储深度/模板清除值。*/
struct RHIClearDepthStencilValue final
{
    /** 深度清除值。*/
    float Depth = 1.0f;
    /** 模板清除值。*/
    std::uint32_t Stencil = 0;
};

/** 存储颜色清除值或深度/模板清除值。*/
using RHIClearValue = std::variant<RHIClearColorValue, RHIClearDepthStencilValue>;

/** 标识一个纹理子资源。*/
struct RHISubresource final
{
    /** 纹理分量。*/
    RHIFormatAspect Aspect = RHIFormatAspect::Color;
    /** mip 级别。*/
    std::uint32_t MipLevel = 0;
    /** 数组层。*/
    std::uint32_t ArrayLayer = 0;
};

/** 描述缓冲区到缓冲区复制操作。*/
struct RHIBufferCopyRegion final
{
    /** 源偏移，单位为字节。*/
    std::uint64_t SourceOffset = 0;
    /** 目标偏移，单位为字节。*/
    std::uint64_t DestinationOffset = 0;
    /** 要复制的字节数。*/
    std::uint64_t SizeInBytes = 0;
};

/** 描述缓冲区到纹理或纹理到缓冲区的复制操作。*/
struct RHITextureBufferCopyRegion final
{
    /** 参与复制的纹理子资源。*/
    RHISubresource TextureSubresource{};
    /** 纹理内偏移，单位为纹素。*/
    RHIOffset3D TextureOffset{};
    /** 复制范围，单位为纹素。*/
    RHIExtent3D TextureExtent{};
    /** 缓冲区中的首字节位置。*/
    std::uint64_t BufferOffset = 0;
    /** 每个缓冲区行的纹素数；零表示紧密排列数据。*/
    std::uint32_t BufferRowLength = 0;
    /** 每个缓冲区图像的纹素行数；零表示紧密排列数据。*/
    std::uint32_t BufferImageHeight = 0;
};

/** 描述一次纹理到纹理复制操作。*/
struct RHITextureCopyRegion final
{
    /** 源子资源。*/
    RHISubresource SourceSubresource{};
    /** 源偏移，单位为纹素。*/
    RHIOffset3D SourceOffset{};
    /** 目标子资源。*/
    RHISubresource DestinationSubresource{};
    /** 目标偏移，单位为纹素。*/
    RHIOffset3D DestinationOffset{};
    /** 复制范围，单位为纹素。*/
    RHIExtent3D Extent{};
};

/** 描述由应用提供、用于呈现的不透明原生窗口。*/
struct RHIPresentationSurfaceDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 由应用拥有的不透明原生窗口令牌，其解释方式由后端决定。*/
    std::uintptr_t NativeWindowToken = 0;
};

/** 定义交换链呈现图像时采用的策略。*/
enum class RHIPresentMode : std::uint8_t
{
    /** 呈现等待显示同步。*/
    VSync,
    /** 呈现以低延迟为目标，并在允许时可能撕裂。*/
    Immediate,
    /** 可用时采用邮箱式最新帧呈现策略。*/
    Mailbox,
};

/** 描述呈现交换链的创建参数。*/
struct RHISwapchainDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 接收呈现图像的表面。*/
    RHISurfaceHandle Surface{};
    /** 请求的图像尺寸，单位为像素。*/
    RHIExtent2D Extent{};
    /** 请求的呈现图像格式。*/
    RHIFormat Format = RHIFormat::BGRA8_UNorm_sRGB;
    /** 请求的飞行中图像数量。*/
    std::uint32_t ImageCount = 3;
    /** 呈现行为。*/
    RHIPresentMode PresentMode = RHIPresentMode::VSync;
};

/** 描述从交换链获取的图像。*/
struct RHIAcquiredImage final
{
    /** 已获取图像的纹理。*/
    RHITextureHandle Texture{};
    /** 从零开始的交换链图像索引。*/
    std::uint32_t ImageIndex = 0;
    /** 可以开始渲染时会被触发的信号量。*/
    RHISemaphoreHandle AvailableSemaphore{};
};

} // namespace RHI
