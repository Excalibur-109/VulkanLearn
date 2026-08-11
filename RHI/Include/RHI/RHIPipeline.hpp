#pragma once

#include "RHI/RHIShaders.hpp"

#include <array>
#include <optional>

namespace RHI
{

/** 定义光栅化期间的多边形剔除方式。*/
enum class RHICullMode : std::uint8_t
{
    /** 不剔除三角形。*/
    None,
    /** 剔除正面三角形。*/
    Front,
    /** 剔除背面三角形。*/
    Back,
};

/** 定义光栅化期间的多边形填充方式。*/
enum class RHIFillMode : std::uint8_t
{
    /** 填充三角形内部。*/
    Solid,
    /** 仅光栅化三角形边缘。*/
    Wireframe,
};

/** 定义被视为正面的绕序。*/
enum class RHIFrontFace : std::uint8_t
{
    /** 逆时针绕序为正面。*/
    CounterClockwise,
    /** 顺时针绕序为正面。*/
    Clockwise,
};

/** 定义颜色混合中的源和目标系数。*/
enum class RHIBlendFactor : std::uint8_t
{
    /** 系数为零。*/
    Zero,
    /** 系数为一。*/
    One,
    /** 系数为源颜色。*/
    SourceColor,
    /** 系数为一减源颜色。*/
    OneMinusSourceColor,
    /** 系数为目标颜色。*/
    DestinationColor,
    /** 系数为一减目标颜色。*/
    OneMinusDestinationColor,
    /** 系数为源 Alpha。*/
    SourceAlpha,
    /** 系数为一减源 Alpha。*/
    OneMinusSourceAlpha,
    /** 系数为目。Alpha。*/
    DestinationAlpha,
    /** 系数为一减目。Alpha。*/
    OneMinusDestinationAlpha,
    /** 系数为可编程混合常量。*/
    BlendConstant,
    /** 系数为一减可编程混合常量。*/
    OneMinusBlendConstant,
    /** 系数为第二源颜色。*/
    Source1Color,
    /** 系数为一减第二源颜色。*/
    OneMinusSource1Color,
    /** 系数为第二源 Alpha。*/
    Source1Alpha,
    /** 系数为一减第二源 Alpha。*/
    OneMinusSource1Alpha,
};

/** 定义颜色混合使用的算术操作。*/
enum class RHIBlendOp : std::uint8_t
{
    /** 相加源项和目标项。*/
    Add,
    /** 用源项减目标项。*/
    Subtract,
    /** 用目标项减源项。*/
    ReverseSubtract,
    /** 取源项和目标项的最小值。*/
    Min,
    /** 取源项和目标项的最大值。*/
    Max,
};

/** 以位掩码定义颜色附件可写通道。*/
enum class RHIColorWriteMask : std::uint8_t
{
    /** 不写入颜色通道。*/
    None = 0,
    /** 写入红色通道。*/
    Red = 1u << 0u,
    /** 写入绿色通道。*/
    Green = 1u << 1u,
    /** 写入蓝色通道。*/
    Blue = 1u << 2u,
    /** 写入 Alpha 通道。*/
    Alpha = 1u << 3u,
    /** 写入所有颜色通道。*/
    All = (1u << 0u) | (1u << 1u) | (1u << 2u) | (1u << 3u),
};

/** 合并颜色写入掩码位。*/
[[nodiscard]] constexpr RHIColorWriteMask operator|(const RHIColorWriteMask left, const RHIColorWriteMask right) noexcept
{
    return static_cast<RHIColorWriteMask>(static_cast<std::uint8_t>(left) | static_cast<std::uint8_t>(right));
}

/** 定义模板缓冲区操作。*/
enum class RHIStencilOp : std::uint8_t
{
    /** 保留现有模板值。*/
    Keep,
    /** 替换为模板参考值。*/
    Replace,
    /** 递增并钳制。*/
    IncrementClamp,
    /** 递减并钳制。*/
    DecrementClamp,
    /** 按位取反该值。*/
    Invert,
    /** 递增并回绕。*/
    IncrementWrap,
    /** 递减并回绕。*/
    DecrementWrap,
    /** 替换为零。*/
    Zero,
};

/** 定义无需重建图形管线即可改变的状态。*/
enum class RHIDynamicState : std::uint8_t
{
    /** 光栅视口。*/
    Viewport,
    /** 光栅裁剪矩形。*/
    Scissor,
    /** RGBA 混合常量。*/
    BlendConstant,
    /** 模板参考值。*/
    StencilReference,
    /** 模板比较掩码。*/
    StencilCompareMask,
    /** 模板写入掩码。*/
    StencilWriteMask,
    /** 深度偏移值。*/
    DepthBias,
    /** 深度边界。*/
    DepthBounds,
    /** 线宽。*/
    LineWidth,
};

/** 定义一组光栅化状态。*/
struct RHIRasterizerState final
{
    /** 三角形填充方式。*/
    RHIFillMode FillMode = RHIFillMode::Solid;
    /** 三角形剔除方式。*/
    RHICullMode CullMode = RHICullMode::Back;
    /** 正面绕序。*/
    RHIFrontFace FrontFace = RHIFrontFace::CounterClockwise;
    /** 启用用于阴影贴图类渲染的深度偏移。*/
    bool DepthBiasEnable = false;
    /** 常量深度偏移项。*/
    float DepthBiasConstant = 0.0f;
    /** 最大深度偏移钳制值。*/
    float DepthBiasClamp = 0.0f;
    /** 斜率缩放深度偏移项。*/
    float DepthBiasSlope = 0.0f;
    /** 当前后端支持时启用深度钳制。*/
    bool DepthClampEnable = false;
    /** 当前后端支持时启用保守光栅化。*/
    bool ConservativeRasterEnable = false;
};

/** 定义正面或背面模板测试与更新。*/
struct RHIStencilFaceState final
{
    /** 模板更新前使用的比较操作。*/
    RHICompareOp CompareOp = RHICompareOp::Always;
    /** 模板测试失败时的操作。*/
    RHIStencilOp FailOp = RHIStencilOp::Keep;
    /** 模板通过但深度失败时的操作。*/
    RHIStencilOp DepthFailOp = RHIStencilOp::Keep;
    /** 模板和深度均通过时的操作。*/
    RHIStencilOp PassOp = RHIStencilOp::Keep;
};

/** 定义深度和模板测试状态。*/
struct RHIDepthStencilState final
{
    /** 启用深度测试。*/
    bool DepthTestEnable = true;
    /** 启用深度写入。*/
    bool DepthWriteEnable = true;
    /** 深度比较操作。*/
    RHICompareOp DepthCompareOp = RHICompareOp::LessEqual;
    /** 启用深度边界测试。*/
    bool DepthBoundsEnable = false;
    /** 启用模板测试。*/
    bool StencilTestEnable = false;
    /** 正面模板状态。*/
    RHIStencilFaceState FrontFace{};
    /** 背面模板状态。*/
    RHIStencilFaceState BackFace{};
};

/** 定义一个颜色目标的混合和通道写入状态。*/
struct RHIColorBlendAttachmentState final
{
    /** 在写入目标前启用混合。*/
    bool BlendEnable = false;
    /** RGB 混合源系数。*/
    RHIBlendFactor SourceColorFactor = RHIBlendFactor::One;
    /** RGB 混合目标系数。*/
    RHIBlendFactor DestinationColorFactor = RHIBlendFactor::Zero;
    /** RGB 混合操作。*/
    RHIBlendOp ColorOp = RHIBlendOp::Add;
    /** Alpha 混合源系数。*/
    RHIBlendFactor SourceAlphaFactor = RHIBlendFactor::One;
    /** Alpha 混合目标系数。*/
    RHIBlendFactor DestinationAlphaFactor = RHIBlendFactor::Zero;
    /** Alpha 混合操作。*/
    RHIBlendOp AlphaOp = RHIBlendOp::Add;
    /** 启用写入的通道。*/
    RHIColorWriteMask WriteMask = RHIColorWriteMask::All;
};

/** 定义图形管线的输出格式和混合状态。*/
struct RHIColorBlendState final
{
    /** 按颜色附件槽位索引的状态。*/
    std::vector<RHIColorBlendAttachmentState> Attachments;
    /** 使用动态混合常量时。true。*/
    bool BlendConstantDynamic = true;
};

/** 定义一个可编程管线阶段的着色器模块和入口点。*/
struct RHIShaderStageDesc final
{
    /** 包含入口点的着色器模块。*/
    RHIShaderModuleHandle Module{};
    /** 此入口点执行的阶段。*/
    RHIShaderStage Stage = RHIShaderStage::Vertex;
    /** 着色器模块内的入口点名称。*/
    std::string EntryPoint = "main";
};

/** 描述图形管线接受的附件格式。*/
struct RHIRenderTargetLayout final
{
    /** 按槽位索引的颜色附件格式。*/
    std::vector<RHIFormat> ColorFormats;
    /** 可选的深度/模板附件格式。*/
    RHIFormat DepthStencilFormat = RHIFormat::Unknown;
    /** 所有附件所需的光栅采样数量。*/
    RHISampleCount SampleCount = RHISampleCount::Count1;
};

/** 描述图形管线创建参数。*/
struct RHIGraphicsPipelineDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 管线使用的资源接口。*/
    RHIPipelineLayoutDesc Layout{};
    /** 可编程阶段；通常至少提供顶点和像素阶段。*/
    std::vector<RHIShaderStageDesc> ShaderStages;
    /** 按绑定编号索引的顶点输入槽。*/
    std::vector<RHIVertexBufferLayout> VertexBuffers;
    /** 图元组装拓扑。*/
    RHIPrimitiveTopology Topology = RHIPrimitiveTopology::TriangleList;
    /** 光栅化状态。*/
    RHIRasterizerState Rasterizer{};
    /** 深度/模板状态。*/
    RHIDepthStencilState DepthStencil{};
    /** 颜色混合状态。*/
    RHIColorBlendState Blend{};
    /** 渲染时要求的附件接口。*/
    RHIRenderTargetLayout RenderTargets{};
    /** 命令录制时动态提供的状态。*/
    std::vector<RHIDynamicState> DynamicStates;
};

/** 描述计算管线创建参数。*/
struct RHIComputePipelineDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 管线使用的资源接口。*/
    RHIPipelineLayoutDesc Layout{};
    /** 计算入口点。*/
    RHIShaderStageDesc ComputeShader{};
};

/** 定义加速结构种类。*/
enum class RHIAccelerationStructureType : std::uint8_t
{
    /** 由几何体构建的底层结构。*/
    BottomLevel,
    /** 由实例构建的顶层结构。*/
    TopLevel,
};

/** 定义底层加速结构的几何类别。*/
enum class RHIRayTracingGeometryType : std::uint8_t
{
    /** 由顶点和可选索引缓冲区提供的三角形几何。*/
    Triangles,
    /** 轴对齐包围盒。*/
    ProceduralAabbs,
};

/** 描述构建加速结构的三角形几何。*/
struct RHIRayTracingTriangleGeometry final
{
    /** 顶点位置缓冲区。*/
    RHIBufferHandle VertexBuffer{};
    /** 到首顶点的字节偏移。*/
    std::uint64_t VertexOffset = 0;
    /** 顶点步长，单位为字节。*/
    std::uint32_t VertexStride = 0;
    /** 顶点数量。*/
    std::uint32_t VertexCount = 0;
    /** 可选索引缓冲区。*/
    RHIBufferHandle IndexBuffer{};
    /** 到首索引的字节偏移。*/
    std::uint64_t IndexOffset = 0;
    /** 存在索引缓冲区时的索引编码。*/
    RHIIndexType IndexType = RHIIndexType::UInt32;
    /** 索引数量；非索引几何为零。*/
    std::uint32_t IndexCount = 0;
    /** 三角形对任意命中着色器不透明时为 true。*/
    bool Opaque = true;
};

/** 描述构建加速结构的 AABB 几何。*/
struct RHIRayTracingAabbGeometry final
{
    /** 保存轴对齐包围盒的缓冲区。*/
    RHIBufferHandle Buffer{};
    /** 到首包围盒的字节偏移。*/
    std::uint64_t Offset = 0;
    /** 包围盒之间的字节步长。*/
    std::uint32_t Stride = 0;
    /** 包围盒数量。*/
    std::uint32_t Count = 0;
    /** 几何体对任意命中着色器不透明时为 true。*/
    bool Opaque = true;
};

/** 存储一个底层加速结构几何载荷。*/
using RHIRayTracingGeometry = std::variant<RHIRayTracingTriangleGeometry, RHIRayTracingAabbGeometry>;

/** 描述可复用的加速结构分配。*/
struct RHIAccelerationStructureDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 结构层级。*/
    RHIAccelerationStructureType Type = RHIAccelerationStructureType::BottomLevel;
    /** 构建时预期的最大几何体或实例数量。*/
    std::uint32_t MaxPrimitiveCount = 0;
    /** 初始构建后将更新该结构时。true。*/
    bool AllowUpdate = false;
    /** 构建后该结构可压缩时。true。*/
    bool AllowCompaction = false;
};

/** 描述顶层加速结构中的一个实例。*/
struct RHIRayTracingInstance final
{
    /** 行主。3x4 对象到世界变换。*/
    std::array<float, 12> Transform{};
    /** 此记录实例化的底层结构。*/
    RHIAccelerationStructureHandle BottomLevel{};
    /** 应用定义。24 位实例标识符。*/
    std::uint32_t InstanceId = 0;
    /** 光线掩码使用。8 位可见性掩码。*/
    std::uint8_t Mask = 0xffu;
    /** 着色器表贡献索引。*/
    std::uint32_t HitGroupOffset = 0;
    /** 实例不透明时为 true。*/
    bool Opaque = true;
};

/** 描述加速结构构建命令。*/
struct RHIAccelerationStructureBuildDesc final
{
    /** 目标结构。*/
    RHIAccelerationStructureHandle Destination{};
    /** 原地更新的源结构；完整构建时为无效句柄。*/
    RHIAccelerationStructureHandle Source{};
    /** 底层几何体；顶层构建时为空。*/
    std::vector<RHIRayTracingGeometry> Geometries;
    /** 顶层实例；底层构建时为空。*/
    std::vector<RHIRayTracingInstance> Instances;
    /** 后端构建期间使用的临时缓冲区。*/
    RHIBufferHandle ScratchBuffer{};
    /** 可供构建使用的临时缓冲区首字节。*/
    std::uint64_t ScratchOffset = 0;
    /** 本操作更新已有结构时。true。*/
    bool Update = false;
};

/** 定义光线追踪管线中的组角色。*/
enum class RHIRayTracingShaderGroupType : std::uint8_t
{
    /** 通用光线生成、未命中或可调用着色器。*/
    General,
    /** 三角形命中组。*/
    TrianglesHitGroup,
    /** 含相交着色器的过程化命中组。*/
    ProceduralHitGroup,
};

/** 描述光线追踪着色器表中的一条记录。*/
struct RHIRayTracingShaderGroup final
{
    /** 组分类。*/
    RHIRayTracingShaderGroupType Type = RHIRayTracingShaderGroupType::General;
    /** 通用、最近命中或相交着色器阶段索引。*/
    std::uint32_t PrimaryShader = std::numeric_limits<std::uint32_t>::max();
    /** 可选的任意命中着色器阶段索引。*/
    std::uint32_t AnyHitShader = std::numeric_limits<std::uint32_t>::max();
    /** 可选的相交着色器阶段索引。*/
    std::uint32_t IntersectionShader = std::numeric_limits<std::uint32_t>::max();
};

/** 描述光线追踪管线创建参数。*/
struct RHIRayTracingPipelineDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 管线使用的资源接口。*/
    RHIPipelineLayoutDesc Layout{};
    /** 由组引用的光线追踪着色器阶段。*/
    std::vector<RHIShaderStageDesc> ShaderStages;
    /** 着色器表分组。*/
    std::vector<RHIRayTracingShaderGroup> Groups;
    /** 此管线支持的最大递归深度。*/
    std::uint32_t MaxRecursionDepth = 1;
};

/** 描述光线调度使用的设备缓冲区和记录。*/
struct RHIShaderTableDesc final
{
    /** 保存紧凑着色器记录的缓冲区。*/
    RHIBufferHandle Buffer{};
    /** 光线生成记录的字节偏移。*/
    std::uint64_t RayGenerationOffset = 0;
    /** 未命中记录表的字节偏移。*/
    std::uint64_t MissOffset = 0;
    /** 一条未命中记录的字节步长。*/
    std::uint64_t MissStride = 0;
    /** 未命中记录数量。*/
    std::uint32_t MissCount = 0;
    /** 命中组记录表的字节偏移。*/
    std::uint64_t HitGroupOffset = 0;
    /** 一条命中组记录的字节步长。*/
    std::uint64_t HitGroupStride = 0;
    /** 命中组记录数量。*/
    std::uint32_t HitGroupCount = 0;
    /** 可调用记录表的字节偏移。*/
    std::uint64_t CallableOffset = 0;
    /** 一条可调用记录的字节步长。*/
    std::uint64_t CallableStride = 0;
    /** 可调用记录数量。*/
    std::uint32_t CallableCount = 0;
};

} // namespace RHI
