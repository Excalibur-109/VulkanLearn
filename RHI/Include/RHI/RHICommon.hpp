#pragma once

#include "RHI/RHIExport.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace RHI
{

/** 表示 RHI 操作结果，不暴露底层后端错误码。*/
enum class RHIResult : std::uint8_t
{
    /** 操作成功完成。*/
    Success,
    /** 传入参数无效。*/
    InvalidArgument,
    /** 当前设备不支持请求的操作。*/
    Unsupported,
    /** 设备缺少内存或其他必要资源。*/
    OutOfMemory,
    /** 设备或呈现表面已失效，必须重新创建。*/
    DeviceLost,
    /** 操作需等待此前工作完成后才能继续。*/
    NotReady,
    /** 操作在完成前被取消。*/
    Cancelled,
    /** 后端发生未分类的失败。*/
    Failure,
};

/** 携带操作结果及可选诊断文本。*/
struct RHIStatus final
{
    /** 结果类别。*/
    RHIResult Code = RHIResult::Success;
    /** 面向调用方的可读诊断信息。*/
    std::string Message;

    /** 当关联操作成功完成时返回 true。*/
    [[nodiscard]] bool Succeeded() const noexcept { return Code == RHIResult::Success; }

    /** 创建不带诊断文本的成功状态。*/
    [[nodiscard]] static RHIStatus Ok() { return {}; }

    /** 创建带说明文本的失败状态。*/
    [[nodiscard]] static RHIStatus Error(const RHIResult code, std::string message)
    {
        return {code, std::move(message)};
    }
};

/** 判断结果是否表示成功完成。*/
[[nodiscard]] constexpr bool Succeeded(const RHIResult result) noexcept
{
    return result == RHIResult::Success;
}

/** 判断结果是否表示错误或未完成状态。*/
[[nodiscard]] constexpr bool Failed(const RHIResult result) noexcept
{
    return !Succeeded(result);
}

/** 描述命令执行所在的硬件队列类型。*/
enum class RHIQueueType : std::uint8_t
{
    /** 可执行图形、计算和复制工作的队列。*/
    Graphics,
    /** 可用时专用于计算工作的队列。*/
    Compute,
    /** 可用时专用于传输工作的队列。*/
    Copy,
};

/** 描述资源分配的预。CPU/GPU 访问模式。*/
enum class RHIMemoryUsage : std::uint8_t
{
    /** 。GPU 访问优化的设备本地内存。*/
    GpuOnly,
    /** CPU 可写、主要用于上传到 GPU 的内存。*/
    CpuToGpu,
    /** CPU 可读、主要由 GPU 写入的内存。*/
    GpuToCpu,
    /** CPU 与 GPU 访问同等重要的内存。*/
    CpuAndGpu,
};

/** 指定资源的抽象访。布局状态。*/
enum class RHIResourceState : std::uint32_t
{
    /** 未声明访问；内容可被丢弃。*/
    Undefined = 0,
    /** 通用读写状态。*/
    Common = 1u << 0u,
    /** 传输复制源。*/
    CopySource = 1u << 1u,
    /** 传输复制目标。*/
    CopyDestination = 1u << 2u,
    /** 顶点输入缓冲区。*/
    VertexBuffer = 1u << 3u,
    /** 索引输入缓冲区。*/
    IndexBuffer = 1u << 4u,
    /** 常量/统一缓冲区。*/
    ConstantBuffer = 1u << 5u,
    /** 着色器只读资源。*/
    ShaderRead = 1u << 6u,
    /** 着色器读写资源。*/
    UnorderedAccess = 1u << 7u,
    /** 颜色附件目标。*/
    RenderTarget = 1u << 8u,
    /** 可写深度/模板附件。*/
    DepthWrite = 1u << 9u,
    /** 只读深度/模板附件。*/
    DepthRead = 1u << 10u,
    /** 呈现源。*/
    Present = 1u << 11u,
    /** 多重采样解析源。*/
    ResolveSource = 1u << 12u,
    /** 多重采样解析目标。*/
    ResolveDestination = 1u << 13u,
    /** 加速结构数据的读取访问。*/
    AccelerationStructureRead = 1u << 14u,
    /** 加速结构数据的写入访问。*/
    AccelerationStructureWrite = 1u << 15u,
    /** 可变速率着色数据的读取访问。*/
    ShadingRateSource = 1u << 16u,
};

/** 合并两个资源状态位掩码。*/
[[nodiscard]] constexpr RHIResourceState operator|(const RHIResourceState left, const RHIResourceState right) noexcept
{
    return static_cast<RHIResourceState>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

/** 测试资源状态掩码是否包含任一请求状态。*/
[[nodiscard]] constexpr bool HasAnyState(const RHIResourceState value, const RHIResourceState mask) noexcept
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(mask)) != 0u;
}

/** 定义图像资源的纹素编码。*/
enum class RHIFormat : std::uint16_t
{
    /** 未指定格式。*/
    Unknown,
    /** 单个 8 位无符号归一化通道。*/
    R8_UNorm,
    /** 两个 8 位无符号归一化通道。*/
    RG8_UNorm,
    /** 四个 8 位无符号归一化线性通道。*/
    RGBA8_UNorm,
    /** 使用 sRGB 传递函数的四个 8 位无符号归一化通道。*/
    RGBA8_UNorm_sRGB,
    /** 。BGRA 存储顺序排列的四。8 位无符号归一化线性通道。*/
    BGRA8_UNorm,
    /** 。BGRA 存储顺序排列的四。8 位无符号归一。sRGB 通道。*/
    BGRA8_UNorm_sRGB,
    /** 单个 16 位浮点通道。*/
    R16_Float,
    /** 两个 16 位浮点通道。*/
    RG16_Float,
    /** 四个 16 位浮点通道。*/
    RGBA16_Float,
    /** 单个 32 位浮点通道。*/
    R32_Float,
    /** 两个 32 位浮点通道。*/
    RG32_Float,
    /** 三个 32 位浮点通道。*/
    RGB32_Float,
    /** 四个 32 位浮点通道。*/
    RGBA32_Float,
    /** 单个 32 位无符号整数通道。*/
    R32_UInt,
    /** 单个 32 位有符号整数通道。*/
    R32_SInt,
    /** 组合。24 位深度和 8 位模板格式。*/
    D24_UNorm_S8_UInt,
    /** 32 位浮点深度格式。*/
    D32_Float,
    /** 组合。32 位浮点深度和 8 位模板格式。*/
    D32_Float_S8_UInt,
    /** 块压缩的四通道线性格式。*/
    BC1_RGBA_UNorm,
    /** 块压缩的四通道 sRGB 格式。*/
    BC1_RGBA_UNorm_sRGB,
    /** 块压缩的高质量四通道线性格式。*/
    BC3_RGBA_UNorm,
    /** 块压缩的高质量四通道 sRGB 格式。*/
    BC3_RGBA_UNorm_sRGB,
    /** 块压缩的法线贴图友好型线性格式。*/
    BC5_RG_UNorm,
    /** 块压缩的 HDR 浮点格式。*/
    BC6H_RGB_UFloat,
    /** 块压缩的高质量四通道线性格式。*/
    BC7_RGBA_UNorm,
    /** 块压缩的高质量四通道 sRGB 格式。*/
    BC7_RGBA_UNorm_sRGB,
};

/** 选择有格式纹理的平面或分量。*/
enum class RHIFormatAspect : std::uint8_t
{
    /** 颜色平面。*/
    Color = 1u << 0u,
    /** 深度平面。*/
    Depth = 1u << 1u,
    /** 模板平面。*/
    Stencil = 1u << 2u,
};

/** 合并纹理格式分量位。*/
[[nodiscard]] constexpr RHIFormatAspect operator|(const RHIFormatAspect left, const RHIFormatAspect right) noexcept
{
    return static_cast<RHIFormatAspect>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

/** 选择光栅化采样数量。*/
enum class RHISampleCount : std::uint8_t
{
    /** 每像素一个采样。*/
    Count1 = 1,
    /** 每像素两个采样。*/
    Count2 = 2,
    /** 每像素四个采样。*/
    Count4 = 4,
    /** 每像素八个采样。*/
    Count8 = 8,
    /** 每像素十六个采样。*/
    Count16 = 16,
};

/** 定义光栅绘制的图元组装方式。*/
enum class RHIPrimitiveTopology : std::uint8_t
{
    /** 相互独立的点。*/
    PointList,
    /** 相互独立的线段。*/
    LineList,
    /** 连续连接的线段。*/
    LineStrip,
    /** 相互独立的三角形。*/
    TriangleList,
    /** 连续连接的三角形。*/
    TriangleStrip,
    /** 用于曲面细分的面片图元。*/
    PatchList,
};

/** 定义索引缓冲区元素位宽。*/
enum class RHIIndexType : std::uint8_t
{
    /** 每个索引占用 16 位。*/
    UInt16,
    /** 每个索引占用 32 位。*/
    UInt32,
};

/** 以位掩码定义着色器阶段可见性。*/
enum class RHIShaderStage : std::uint16_t
{
    /** 不包含着色器阶段。*/
    None = 0,
    /** 顶点处理阶段。*/
    Vertex = 1u << 0u,
    /** 曲面细分控制阶段。*/
    Hull = 1u << 1u,
    /** 曲面细分求值阶段。*/
    Domain = 1u << 2u,
    /** 几何处理阶段。*/
    Geometry = 1u << 3u,
    /** 片段/像素处理阶段。*/
    Pixel = 1u << 4u,
    /** 计算处理阶段。*/
    Compute = 1u << 5u,
    /** 光线生成阶段。*/
    RayGeneration = 1u << 6u,
    /** 光线未命中阶段。*/
    RayMiss = 1u << 7u,
    /** 光线命中组阶段。*/
    RayHit = 1u << 8u,
    /** 可调用光线追踪阶段。*/
    RayCallable = 1u << 9u,
    /** 用于网格着色的任务/工作放大阶段。*/
    Task = 1u << 10u,
    /** 用于网格着色的图元和顶点生成阶段。*/
    Mesh = 1u << 11u,
    /** 所有图形管线阶段。*/
    AllGraphics = (1u << 0u) | (1u << 1u) | (1u << 2u) | (1u << 3u) | (1u << 4u) | (1u << 10u) | (1u << 11u),
    /** 所有可编程阶段。*/
    All = ((1u << 0u) | (1u << 1u) | (1u << 2u) | (1u << 3u) | (1u << 4u) | (1u << 5u) | (1u << 6u) | (1u << 7u) | (1u << 8u) | (1u << 9u) | (1u << 10u) | (1u << 11u)),
};

/** 合并着色器阶段可见性位。*/
[[nodiscard]] constexpr RHIShaderStage operator|(const RHIShaderStage left, const RHIShaderStage right) noexcept
{
    return static_cast<RHIShaderStage>(static_cast<std::uint16_t>(left) | static_cast<std::uint16_t>(right));
}

/** 定义缩小、放大和 mip 级别过滤方式。*/
enum class RHIFilter : std::uint8_t
{
    /** 最近邻过滤。*/
    Nearest,
    /** 线性过滤。*/
    Linear,
    /** 后端支持时使用各向异性过滤。*/
    Anisotropic,
};

/** 定义归一化范围外纹理坐标的处理方式。*/
enum class RHIAddressMode : std::uint8_t
{
    /** 周期性重复纹理。*/
    Wrap,
    /** 每个重复周期镜像纹理。*/
    Mirror,
    /** 钳制到边缘纹素。*/
    Clamp,
    /** 采样显式边框颜色。*/
    Border,
    /** 先镜像一次，再钳制到边缘纹素。*/
    MirrorOnce,
};

/** 定义深度、模板和比较采样使用的比较操作。*/
enum class RHICompareOp : std::uint8_t
{
    /** 比较始终失败。*/
    Never,
    /** 源值小于目标值。*/
    Less,
    /** 源值等于目标值。*/
    Equal,
    /** 源值小于或等于目标值。*/
    LessEqual,
    /** 源值大于目标值。*/
    Greater,
    /** 源值不等于目标值。*/
    NotEqual,
    /** 源值大于或等于目标值。*/
    GreaterEqual,
    /** 比较始终成功。*/
    Always,
};

/** 在不耦合具体图形接口的前提下标识一个后端实现。*/
struct RHIBackendId final
{
    /** 由后端提供方指定的稳定、区分大小写的标识符。*/
    std::string Value;

    /** 标识符包含后端名称时返回 true。*/
    [[nodiscard]] bool IsValid() const noexcept { return !Value.empty(); }

    /** 两个标识符指向同一后端提供方时返回 true。*/
    [[nodiscard]] bool operator==(const RHIBackendId& other) const noexcept { return Value == other.Value; }
};

/** 表示主版本、次版本和修订版本。*/
struct RHIVersion final
{
    /** 主兼容版本。*/
    std::uint32_t Major = 1;
    /** 次功能版本。*/
    std::uint32_t Minor = 0;
    /** 修订版本。*/
    std::uint32_t Patch = 0;
};

/** 描述二维无符号尺寸。*/
struct RHIExtent2D final
{
    /** 水平尺寸，单位为纹素或像素。*/
    std::uint32_t Width = 0;
    /** 垂直尺寸，单位为纹素或像素。*/
    std::uint32_t Height = 0;

    /** 两个维度均非零时返回 true。*/
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Width != 0 && Height != 0; }
};

/** 描述三维无符号尺寸。*/
struct RHIExtent3D final
{
    /** 水平尺寸，单位为纹素。*/
    std::uint32_t Width = 0;
    /** 垂直尺寸，单位为纹素。*/
    std::uint32_t Height = 0;
    /** 深度尺寸，单位为纹素。*/
    std::uint32_t Depth = 1;

    /** 所有维度均非零时返。true。*/
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Width != 0 && Height != 0 && Depth != 0; }
};

/** 描述有符号三维纹理偏移。*/
struct RHIOffset3D final
{
    /** 水平纹素偏移。*/
    std::int32_t X = 0;
    /** 垂直纹素偏移。*/
    std::int32_t Y = 0;
    /** 深度纹素偏移。*/
    std::int32_t Z = 0;
};

/** 描述浮点光栅化视口。*/
struct RHIViewport final
{
    /** 左边界，单位为帧缓冲像素。*/
    float X = 0.0f;
    /** 上边界，单位为帧缓冲像素。*/
    float Y = 0.0f;
    /** 视口宽度，单位为帧缓冲像素。*/
    float Width = 0.0f;
    /** 视口高度，单位为帧缓冲像素。*/
    float Height = 0.0f;
    /** 最小深度值。*/
    float MinDepth = 0.0f;
    /** 最大深度值。*/
    float MaxDepth = 1.0f;
};

/** 描述整数光栅化裁剪矩形。*/
struct RHIScissor final
{
    /** 左边界，单位为帧缓冲像素。*/
    std::int32_t X = 0;
    /** 上边界，单位为帧缓冲像素。*/
    std::int32_t Y = 0;
    /** 矩形宽度，单位为帧缓冲像素。*/
    std::uint32_t Width = 0;
    /** 矩形高度，单位为帧缓冲像素。*/
    std::uint32_t Height = 0;
};

/** 存储线。RGBA 颜色。*/
struct RHIColor final
{
    /** 红色分量。*/
    float R = 0.0f;
    /** 绿色分量。*/
    float G = 0.0f;
    /** 蓝色分量。*/
    float B = 0.0f;
    /** Alpha 分量。*/
    float A = 1.0f;
};

/** 描述连续的纹。mip 级别和数组层范围。*/
struct RHISubresourceRange final
{
    /** 范围覆盖的纹理分量。*/
    RHIFormatAspect Aspect = RHIFormatAspect::Color;
    /** 首个 mip 级别。*/
    std::uint32_t BaseMipLevel = 0;
    /** mip 级别数量；MaxUint32 表示其后的所有级别。*/
    std::uint32_t MipLevelCount = std::numeric_limits<std::uint32_t>::max();
    /** 首个数组层。*/
    std::uint32_t BaseArrayLayer = 0;
    /** 数组层数量；MaxUint32 表示其后的所有层。*/
    std::uint32_t ArrayLayerCount = std::numeric_limits<std::uint32_t>::max();
};

} // namespace RHI
