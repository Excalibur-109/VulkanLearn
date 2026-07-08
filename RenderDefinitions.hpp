#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

/**
 * @file RenderDefinitions.hpp
 * @brief 跨图形 API 的渲染描述层。
 *
 * 这个文件只定义“渲染引擎准备阶段”需要的数据结构，不包含任何 Vulkan、D3D、Metal
 * 或 OpenGL 的原生类型。后端实现应该读取这些通用结构，再转换成具体 API 对象，例如：
 *
 * - BufferDesc -> VkBuffer / ID3D12Resource / MTLBuffer
 * - TextureDesc -> VkImage / ID3D12Resource(Texture) / MTLTexture
 * - GraphicsPipelineDesc -> VkPipeline / D3D12 PSO / MTLRenderPipelineState
 * - RenderGraphDesc -> 后端命令录制顺序、资源状态转换和同步 barrier
 *
 * 设计目标：
 * - 上层渲染逻辑只依赖这些结构，不关心底层 API。
 * - 每个结构都尽量表达“意图”，而不是表达某个 API 的创建参数。
 * - 后端可以根据 RenderCapabilities 降级或选择不同实现路径。
 */
namespace Engine::Render {

/// 无效数组下标，常用于 optional index 或查找失败的返回值。
inline constexpr std::uint32_t INVALID_INDEX = std::numeric_limits<std::uint32_t>::max();

/// 0 被保留为无效句柄，真实后端资源句柄从非 0 值开始分配。
inline constexpr std::uint64_t INVALID_HANDLE_VALUE = 0;

/// 表示 buffer binding 或 barrier 覆盖资源剩余全部范围。
inline constexpr std::uint64_t WHOLE_SIZE = std::numeric_limits<std::uint64_t>::max();

/**
 * @brief 类型安全的轻量资源句柄。
 *
 * Handle 只保存引擎自己的逻辑 id，不直接保存 VkBuffer、ID3D12Resource* 等后端对象。
 * 后端资源管理器可以用这个 id 去索引真正的 API 资源。不同 Tag 让 BufferHandle 和
 * TextureHandle 不能互相误传。
 */
template <typename Tag>
struct Handle {
    /// 资源管理器分配的逻辑 id；0 表示无效。
    std::uint64_t value = INVALID_HANDLE_VALUE;

    constexpr Handle() noexcept = default;

    /// 显式从资源 id 构造句柄，避免整数被意外隐式转换成资源句柄。
    explicit constexpr Handle(std::uint64_t handleValue) noexcept
        : value(handleValue) {
    }

    /// 判断句柄是否指向一个已分配的逻辑资源。
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return value != INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return isValid();
    }

    friend constexpr bool operator==(Handle lhs, Handle rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(Handle lhs, Handle rhs) noexcept {
        return !(lhs == rhs);
    }
};

// 这些空 tag 只用于让 Handle<T> 变成不同的 C++ 类型，不参与运行期逻辑。
struct BufferTag {};
struct TextureTag {};
struct TextureViewTag {};
struct SamplerTag {};
struct ShaderTag {};
struct PipelineLayoutTag {};
struct PipelineTag {};
struct RenderPassTag {};
struct FramebufferTag {};
struct SwapchainTag {};
struct BindGroupLayoutTag {};
struct BindGroupTag {};
struct QueryPoolTag {};
struct PipelineCacheTag {};
struct SemaphoreTag {};
struct FenceTag {};
struct MeshTag {};
struct MaterialTag {};

/// GPU buffer 资源，例如顶点、索引、uniform、storage、上传 staging buffer。
using BufferHandle = Handle<BufferTag>;

/// GPU texture/image 资源，例如贴图、渲染目标、深度缓冲、swapchain image。
using TextureHandle = Handle<TextureTag>;

/// texture 的视图，描述 mip/layer/维度/格式重解释。
using TextureViewHandle = Handle<TextureViewTag>;

/// 采样器状态对象，描述过滤、寻址、各向异性和比较采样。
using SamplerHandle = Handle<SamplerTag>;

/// 着色器对象或着色器模块。
using ShaderHandle = Handle<ShaderTag>;

/// 资源绑定布局集合，对应 Vulkan pipeline layout / D3D root signature / Metal argument layout。
using PipelineLayoutHandle = Handle<PipelineLayoutTag>;

/// 图形或计算管线对象。
using PipelineHandle = Handle<PipelineTag>;

/// 渲染通道对象；在现代后端中也可以只是动态渲染/附件配置的缓存 key。
using RenderPassHandle = Handle<RenderPassTag>;

/// framebuffer 或一组绑定到 render pass 的附件视图。
using FramebufferHandle = Handle<FramebufferTag>;

/// swapchain 对象句柄，表示窗口后备缓冲队列。
using SwapchainHandle = Handle<SwapchainTag>;

/// 一组资源槽位的布局，对应 Vulkan descriptor set layout / D3D descriptor table。
using BindGroupLayoutHandle = Handle<BindGroupLayoutTag>;

/// 一组实际绑定资源，对应 Vulkan descriptor set / D3D descriptor heap 区间 / Metal argument buffer。
using BindGroupHandle = Handle<BindGroupTag>;

/// GPU 查询池句柄，例如 timestamp、occlusion、pipeline statistics。
using QueryPoolHandle = Handle<QueryPoolTag>;

/// 管线缓存句柄，用于复用后端 pipeline 编译结果。
using PipelineCacheHandle = Handle<PipelineCacheTag>;

/// GPU 队列间同步信号句柄，可映射到 binary semaphore、timeline semaphore 或 shared event。
using SemaphoreHandle = Handle<SemaphoreTag>;

/// CPU 等待 GPU 完成的同步对象句柄。
using FenceHandle = Handle<FenceTag>;

/// 引擎层 mesh 资源句柄。
using MeshHandle = Handle<MeshTag>;

/// 引擎层 material 资源句柄。
using MaterialHandle = Handle<MaterialTag>;

namespace detail {
template <typename Enum>
[[nodiscard]] constexpr auto toUnderlying(Enum value) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(value);
}

template <typename Enum>
[[nodiscard]] constexpr Enum bitOr(Enum lhs, Enum rhs) noexcept {
    return static_cast<Enum>(toUnderlying(lhs) | toUnderlying(rhs));
}

template <typename Enum>
[[nodiscard]] constexpr Enum bitAnd(Enum lhs, Enum rhs) noexcept {
    return static_cast<Enum>(toUnderlying(lhs) & toUnderlying(rhs));
}
} // namespace detail

template <typename Enum>
[[nodiscard]] constexpr bool hasAny(Enum value, Enum flags) noexcept {
    return (detail::toUnderlying(value) & detail::toUnderlying(flags)) != 0;
}

template <typename Enum>
[[nodiscard]] constexpr bool hasAll(Enum value, Enum flags) noexcept {
    return (detail::toUnderlying(value) & detail::toUnderlying(flags)) == detail::toUnderlying(flags);
}

/// 当前后端使用的图形 API，用于能力查询、日志和后端分支。
enum class GraphicsApi : std::uint8_t {
    Unknown,
    Vulkan,
    Direct3D11,
    Direct3D12,
    Metal,
    OpenGL,
    WebGPU
};

/// GPU 队列类型。不是所有 API 都公开独立队列，后端可以把多个类型映射到同一个实际队列。
enum class QueueType : std::uint8_t {
    Graphics,
    Compute,
    Transfer,
    Present
};

/// GPU 选择偏好。移动端/笔记本上可用于选择省电或高性能适配器。
enum class PowerPreference : std::uint8_t {
    Default,
    LowPower,
    HighPerformance
};

/// 后端验证层开关级别。
enum class ValidationMode : std::uint8_t {
    Disabled,
    Enabled,
    GpuAssisted
};

/// 引擎希望启用的渲染特性位。后端初始化时可根据设备能力做裁剪或报错。
enum class RenderFeature : std::uint64_t {
    None = 0,
    Compute = 1ull << 0,
    GeometryShader = 1ull << 1,
    Tessellation = 1ull << 2,
    MeshShader = 1ull << 3,
    RayTracing = 1ull << 4,
    Bindless = 1ull << 5,
    SamplerAnisotropy = 1ull << 6,
    SamplerCompare = 1ull << 7,
    TimestampQuery = 1ull << 8,
    OcclusionQuery = 1ull << 9,
    PipelineStatisticsQuery = 1ull << 10,
    IndirectDraw = 1ull << 11,
    DrawIndirectCount = 1ull << 12,
    DynamicRendering = 1ull << 13,
    ConservativeRasterization = 1ull << 14,
    TextureCompressionBC = 1ull << 15,
    TextureCompressionETC2 = 1ull << 16,
    TextureCompressionASTC = 1ull << 17,
    Multiview = 1ull << 18,
    DebugMarkers = 1ull << 19
};

[[nodiscard]] constexpr RenderFeature operator|(RenderFeature lhs, RenderFeature rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr RenderFeature operator&(RenderFeature lhs, RenderFeature rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr RenderFeature& operator|=(RenderFeature& lhs, RenderFeature rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 渲染后端初始化参数。窗口原生句柄由平台层保存，这里只描述渲染器自己的需求。
struct RenderBackendDesc {
    std::string applicationName; ///< 应用名称，用于后端实例创建和调试器显示。
    std::string engineName = "VulkanLearn"; ///< 引擎名称，用于后端实例创建和调试器显示。
    GraphicsApi preferredApi = GraphicsApi::Vulkan; ///< 优先使用的图形 API。
    PowerPreference powerPreference = PowerPreference::HighPerformance; ///< GPU 选择偏好。
    ValidationMode validation = ValidationMode::Enabled; ///< 是否启用验证层/调试层。
    RenderFeature requiredFeatures = RenderFeature::None; ///< 必须支持的功能，不支持时初始化应失败。
    RenderFeature optionalFeatures = RenderFeature::DebugMarkers | RenderFeature::TimestampQuery; ///< 可选功能，后端尽量开启。
    std::uint32_t framesInFlight = 2; ///< CPU/GPU 并行帧数。
    bool enableGpuCrashDumps = false; ///< 是否启用 GPU 崩溃转储，具体支持由后端决定。
    bool enablePipelineCache = true; ///< 是否启用管线缓存。
};

/// 队列族/队列能力描述，供后端选择 graphics/compute/transfer/present 队列。
struct QueueDesc {
    QueueType type = QueueType::Graphics; ///< 队列类型。
    std::uint32_t count = 1; ///< 希望创建的队列数量。
    float priority = 1.0F; ///< 队列优先级，范围通常是 0 到 1。
};

/// 物理适配器摘要。初始化 UI 或日志可以直接展示这些信息。
struct AdapterDesc {
    std::string name; ///< GPU/适配器名称。
    GraphicsApi api = GraphicsApi::Unknown; ///< 该适配器所属后端 API。
    std::uint64_t dedicatedVideoMemory = 0; ///< 独立显存字节数。
    std::uint64_t sharedSystemMemory = 0; ///< 可共享系统内存字节数。
    bool isIntegrated = false; ///< 是否集成显卡。
    bool isSoftware = false; ///< 是否软件适配器。
};

/**
 * @brief 引擎统一像素/顶点数据格式。
 *
 * Format 需要覆盖“资源创建”和“管线附件格式”两类使用场景。后端转换时要注意：
 * - sRGB 格式只应该用于颜色贴图或 swapchain，不适合作为普通线性数据。
 * - Depth/Stencil 格式不能当普通 color attachment。
 * - 压缩格式 BC* 不是所有平台都支持，需要根据 RenderCapabilities 或后端能力降级。
 */
enum class Format : std::uint16_t {
    Undefined,

    R8UNorm,
    R8SNorm,
    R8UInt,
    R8SInt,
    RG8UNorm,
    RG8SNorm,
    RG8UInt,
    RG8SInt,
    RGBA8UNorm,
    RGBA8SNorm,
    RGBA8UInt,
    RGBA8SInt,
    RGBA8SRGB,
    BGRA8UNorm,
    BGRA8SRGB,

    R16UNorm,
    R16SNorm,
    R16UInt,
    R16SInt,
    R16Float,
    RG16UNorm,
    RG16SNorm,
    RG16UInt,
    RG16SInt,
    RG16Float,
    RGBA16UNorm,
    RGBA16SNorm,
    RGBA16UInt,
    RGBA16SInt,
    RGBA16Float,

    R32UInt,
    R32SInt,
    R32Float,
    RG32UInt,
    RG32SInt,
    RG32Float,
    RGB32UInt,
    RGB32SInt,
    RGB32Float,
    RGBA32UInt,
    RGBA32SInt,
    RGBA32Float,

    RGB10A2UNorm,
    R11G11B10Float,

    D16UNorm,
    D24UNorm,
    S8UInt,
    D24UNormS8UInt,
    D32Float,
    D32FloatS8UInt,

    BC1RGBAUNorm,
    BC1RGBASRGB,
    BC3RGBAUNorm,
    BC3RGBASRGB,
    BC5RGUNorm,
    BC5RGSNorm,
    BC7RGBAUNorm,
    BC7RGBASRGB,

    ETC2RGB8UNorm,
    ETC2RGB8SRGB,
    ETC2RGBA8UNorm,
    ETC2RGBA8SRGB,
    ASTC4x4UNorm,
    ASTC4x4SRGB,
    ASTC8x8UNorm,
    ASTC8x8SRGB
};

/// 判断格式是否包含深度分量，用于选择 depth attachment 或 depth texture 采样路径。
[[nodiscard]] constexpr bool isDepthFormat(Format format) noexcept {
    return format == Format::D16UNorm ||
           format == Format::D24UNorm ||
           format == Format::D24UNormS8UInt ||
           format == Format::D32Float ||
           format == Format::D32FloatS8UInt;
}

/// 判断格式是否包含 stencil 分量，用于模板测试和 depth-stencil attachment 创建。
[[nodiscard]] constexpr bool hasStencilFormat(Format format) noexcept {
    return format == Format::S8UInt ||
           format == Format::D24UNormS8UInt ||
           format == Format::D32FloatS8UInt;
}

/// 多重采样数量，对应 Vulkan sample count / D3D sample count / Metal sampleCount。
enum class SampleCount : std::uint8_t {
    Count1 = 1,
    Count2 = 2,
    Count4 = 4,
    Count8 = 8,
    Count16 = 16,
    Count32 = 32,
    Count64 = 64
};

/**
 * @brief swapchain 呈现模式。
 *
 * Fifo 是最通用的垂直同步模式；Immediate/Mailbox 可能不被所有平台支持。
 */
enum class PresentMode : std::uint8_t {
    Immediate,
    Mailbox,
    Fifo,
    FifoRelaxed
};

/**
 * @brief 资源在 GPU 管线中的逻辑状态。
 *
 * 显式 API（Vulkan/D3D12）需要把这些状态翻译成 image layout/resource state/barrier。
 * 隐式 API（OpenGL/D3D11）可以把它用于调试验证或减少不必要的绑定错误。
 */
enum class ResourceState : std::uint16_t {
    Undefined,
    Common,
    CopySource,
    CopyDestination,
    VertexBuffer,
    IndexBuffer,
    ConstantBuffer,
    ShaderRead,
    ShaderWrite,
    RenderTarget,
    DepthRead,
    DepthWrite,
    ResolveSource,
    ResolveDestination,
    Present,
    IndirectArgument,
    AccelerationStructureRead,
    AccelerationStructureWrite,
    ShadingRateTexture
};

/// GPU 管线阶段位。需要精确同步时可配合 AccessFlags 构造 barrier。
enum class PipelineStage : std::uint64_t {
    None = 0,
    TopOfPipe = 1ull << 0,
    DrawIndirect = 1ull << 1,
    VertexInput = 1ull << 2,
    VertexShader = 1ull << 3,
    TessControlShader = 1ull << 4,
    TessEvaluationShader = 1ull << 5,
    GeometryShader = 1ull << 6,
    FragmentShader = 1ull << 7,
    EarlyFragmentTests = 1ull << 8,
    LateFragmentTests = 1ull << 9,
    ColorAttachmentOutput = 1ull << 10,
    ComputeShader = 1ull << 11,
    Transfer = 1ull << 12,
    BottomOfPipe = 1ull << 13,
    Host = 1ull << 14,
    RayTracingShader = 1ull << 15,
    AccelerationStructureBuild = 1ull << 16,
    TaskShader = 1ull << 17,
    MeshShader = 1ull << 18,
    AllGraphics = 1ull << 19,
    AllCommands = 1ull << 20
};

[[nodiscard]] constexpr PipelineStage operator|(PipelineStage lhs, PipelineStage rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr PipelineStage operator&(PipelineStage lhs, PipelineStage rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr PipelineStage& operator|=(PipelineStage& lhs, PipelineStage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// GPU 资源访问类型位。比 ResourceState 更接近后端 barrier 所需的 access mask。
enum class AccessFlags : std::uint64_t {
    None = 0,
    IndirectCommandRead = 1ull << 0,
    IndexRead = 1ull << 1,
    VertexAttributeRead = 1ull << 2,
    UniformRead = 1ull << 3,
    InputAttachmentRead = 1ull << 4,
    ShaderRead = 1ull << 5,
    ShaderWrite = 1ull << 6,
    ColorAttachmentRead = 1ull << 7,
    ColorAttachmentWrite = 1ull << 8,
    DepthStencilRead = 1ull << 9,
    DepthStencilWrite = 1ull << 10,
    TransferRead = 1ull << 11,
    TransferWrite = 1ull << 12,
    HostRead = 1ull << 13,
    HostWrite = 1ull << 14,
    MemoryRead = 1ull << 15,
    MemoryWrite = 1ull << 16,
    AccelerationStructureRead = 1ull << 17,
    AccelerationStructureWrite = 1ull << 18
};

[[nodiscard]] constexpr AccessFlags operator|(AccessFlags lhs, AccessFlags rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr AccessFlags operator&(AccessFlags lhs, AccessFlags rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr AccessFlags& operator|=(AccessFlags& lhs, AccessFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 二维尺寸，常用于窗口、swapchain、viewport/scissor 和 2D 纹理。
struct Extent2D {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
};

/// 三维尺寸，2D 纹理时 depth 通常为 1，数组层数单独由 arrayLayers 表示。
struct Extent3D {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t depth = 1;
};

/// 二维整数偏移，主要用于 scissor、copy region 和贴图区域更新。
struct Offset2D {
    std::int32_t x = 0;
    std::int32_t y = 0;
};

/// 三维整数偏移，主要用于 buffer-to-texture 上传和 3D texture copy。
struct Offset3D {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t z = 0;
};

/// 二维矩形区域。offset 是左上角，extent 是宽高。
struct Rect2D {
    Offset2D offset{};
    Extent2D extent{};
};

/// 浮点 viewport。深度范围一般是 [0, 1]，具体后端负责坐标系差异转换。
struct Viewport {
    float x = 0.0F;
    float y = 0.0F;
    float width = 1.0F;
    float height = 1.0F;
    float minDepth = 0.0F;
    float maxDepth = 1.0F;
};

/// color attachment 的清屏颜色，默认透明黑。
struct ClearColor {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

/// depth-stencil attachment 的清屏值，默认 depth=1 表示远平面。
struct ClearDepthStencil {
    float depth = 1.0F;
    std::uint32_t stencil = 0;
};

/// 同时容纳颜色和深度模板清屏值，RenderGraph attachment 可按实际类型读取对应字段。
struct ClearValue {
    ClearColor color{};
    ClearDepthStencil depthStencil{};
};

/// texture 子资源 aspect 位，用于区分 color/depth/stencil/plane。
enum class TextureAspect : std::uint32_t {
    None = 0,
    Color = 1u << 0,
    Depth = 1u << 1,
    Stencil = 1u << 2,
    Plane0 = 1u << 3,
    Plane1 = 1u << 4,
    Plane2 = 1u << 5,
    All = 0xFFFFFFFFu
};

[[nodiscard]] constexpr TextureAspect operator|(TextureAspect lhs, TextureAspect rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr TextureAspect operator&(TextureAspect lhs, TextureAspect rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr TextureAspect& operator|=(TextureAspect& lhs, TextureAspect rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// buffer 的用途位。创建 buffer 时必须声明后续会如何使用，显式 API 会用它设置 usage flags。
enum class BufferUsage : std::uint32_t {
    None = 0,
    TransferSource = 1u << 0,
    TransferDestination = 1u << 1,
    Vertex = 1u << 2,
    Index = 1u << 3,
    Uniform = 1u << 4,
    Storage = 1u << 5,
    Indirect = 1u << 6,
    ShaderDeviceAddress = 1u << 7
};

[[nodiscard]] constexpr BufferUsage operator|(BufferUsage lhs, BufferUsage rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr BufferUsage operator&(BufferUsage lhs, BufferUsage rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr BufferUsage& operator|=(BufferUsage& lhs, BufferUsage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// buffer 创建附加标志，用于表达生命周期和后端内存选择提示。
enum class BufferCreateFlags : std::uint32_t {
    None = 0,
    DedicatedMemory = 1u << 0,
    SparseBinding = 1u << 1,
    RingBuffer = 1u << 2,
    Transient = 1u << 3
};

[[nodiscard]] constexpr BufferCreateFlags operator|(BufferCreateFlags lhs, BufferCreateFlags rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr BufferCreateFlags operator&(BufferCreateFlags lhs, BufferCreateFlags rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr BufferCreateFlags& operator|=(BufferCreateFlags& lhs, BufferCreateFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// texture/image 的用途位。一个 texture 可以同时是采样贴图、渲染目标或拷贝目标。
enum class TextureUsage : std::uint32_t {
    None = 0,
    TransferSource = 1u << 0,
    TransferDestination = 1u << 1,
    Sampled = 1u << 2,
    Storage = 1u << 3,
    ColorAttachment = 1u << 4,
    DepthStencilAttachment = 1u << 5,
    Present = 1u << 6,
    Transient = 1u << 7
};

[[nodiscard]] constexpr TextureUsage operator|(TextureUsage lhs, TextureUsage rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr TextureUsage operator&(TextureUsage lhs, TextureUsage rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr TextureUsage& operator|=(TextureUsage& lhs, TextureUsage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// texture 创建附加标志，用于表达 cube、格式重解释、稀疏资源等需求。
enum class TextureCreateFlags : std::uint32_t {
    None = 0,
    CubeCompatible = 1u << 0,
    MutableFormat = 1u << 1,
    DedicatedMemory = 1u << 2,
    SparseBinding = 1u << 3,
    GenerateMips = 1u << 4,
    RenderGraphTransient = 1u << 5
};

[[nodiscard]] constexpr TextureCreateFlags operator|(TextureCreateFlags lhs, TextureCreateFlags rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr TextureCreateFlags operator&(TextureCreateFlags lhs, TextureCreateFlags rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr TextureCreateFlags& operator|=(TextureCreateFlags& lhs, TextureCreateFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 资源内存访问方向。后端可据此选择显存、本地可映射内存或读回内存。
enum class MemoryUsage : std::uint8_t {
    GpuOnly,
    CpuToGpu,
    GpuToCpu,
    CpuOnly
};

/// 资源生命周期提示，帮助资源分配器决定是否池化、复用或立即释放。
enum class ResourceLifetime : std::uint8_t {
    Persistent,
    PerFrame,
    Transient
};

/// 纹理资源本身的维度，不包含 view 维度重解释。
enum class TextureDimension : std::uint8_t {
    Texture1D,
    Texture2D,
    Texture3D
};

/// 纹理视图维度。一个 2D array texture 可以被创建为 View2DArray 或单层 View2D。
enum class TextureViewDimension : std::uint8_t {
    View1D,
    View1DArray,
    View2D,
    View2DArray,
    View3D,
    Cube,
    CubeArray
};

/// 纹理采样过滤方式。
enum class FilterMode : std::uint8_t {
    Nearest,
    Linear
};

/// mip 层级之间的过滤方式。
enum class MipmapMode : std::uint8_t {
    Nearest,
    Linear
};

/// UVW 坐标越界时的寻址方式。
enum class AddressMode : std::uint8_t {
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder
};

/// ClampToBorder 模式下采样器返回的边框颜色。
enum class BorderColor : std::uint8_t {
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite
};

/// 通用比较函数，深度测试、模板测试和 shadow sampler 都会使用。
enum class CompareOp : std::uint8_t {
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always
};

/// GPU buffer 创建描述，不包含初始数据；初始数据通过 UploadBatchDesc 提交。
struct BufferDesc {
    std::string debugName; ///< 调试名称，用于后端对象命名和 GPU 调试器显示。
    std::uint64_t size = 0; ///< buffer 字节数。创建真实 GPU buffer 时必须大于 0。
    BufferUsage usage = BufferUsage::None; ///< 声明 buffer 的用途位，后端据此设置 usage flags。
    BufferCreateFlags flags = BufferCreateFlags::None; ///< 创建附加标志，例如 transient、dedicated memory。
    MemoryUsage memoryUsage = MemoryUsage::GpuOnly; ///< 内存访问模式，决定资源放在显存、上传堆或读回堆。
    ResourceLifetime lifetime = ResourceLifetime::Persistent; ///< 生命周期提示，影响分配器复用策略。
    bool persistentlyMapped = false; ///< 是否希望 CPU 长期映射该 buffer，通常用于动态 uniform/staging 数据。
};

/// GPU texture/image 创建描述，不包含 view 和 sampler。
struct TextureDesc {
    std::string debugName; ///< 调试名称，用于后端对象命名。
    TextureDimension dimension = TextureDimension::Texture2D; ///< 资源维度。cube texture 本质上仍是 2D array。
    Extent3D extent{}; ///< texture 的像素尺寸。2D texture 的 depth 应为 1。
    std::uint32_t arrayLayers = 1; ///< 数组层数；cube 为 6，cube array 为 6 的倍数。
    std::uint32_t mipLevels = 1; ///< mip 层数；如果需要自动生成 mip，创建时仍要预留层数。
    Format format = Format::RGBA8UNorm; ///< 资源存储格式。
    SampleCount samples = SampleCount::Count1; ///< MSAA 采样数；普通采样贴图一般为 Count1。
    TextureUsage usage = TextureUsage::Sampled; ///< texture 后续用途，影响后端 image usage/resource flags。
    TextureCreateFlags flags = TextureCreateFlags::None; ///< 创建附加标志，例如 cube compatible、mutable format。
    ResourceLifetime lifetime = ResourceLifetime::Persistent; ///< 生命周期提示，RenderGraph 临时图一般设为 Transient。
    ResourceState initialState = ResourceState::Undefined; ///< 创建后的逻辑初始状态，后端可据此插入首个 transition。
};

/// texture view 创建描述，用于选择 texture 的一部分或重解释可兼容格式。
struct TextureViewDesc {
    std::string debugName; ///< 调试名称。
    TextureHandle texture{}; ///< 被查看的底层 texture。
    TextureViewDimension dimension = TextureViewDimension::View2D; ///< view 暴露给 shader/attachment 的维度。
    Format format = Format::Undefined; ///< Undefined 表示沿用底层 texture 格式。
    TextureAspect aspect = TextureAspect::Color; ///< view 覆盖的 aspect，depth/stencil view 需要显式设置。
    std::uint32_t baseMipLevel = 0; ///< view 起始 mip。
    std::uint32_t mipLevelCount = 1; ///< view 覆盖 mip 数。
    std::uint32_t baseArrayLayer = 0; ///< view 起始数组层。
    std::uint32_t arrayLayerCount = 1; ///< view 覆盖数组层数。
};

/// sampler 创建描述。sampler 只描述采样规则，不持有 texture。
struct SamplerDesc {
    std::string debugName; ///< 调试名称。
    FilterMode minFilter = FilterMode::Linear; ///< 缩小时的过滤方式。
    FilterMode magFilter = FilterMode::Linear; ///< 放大时的过滤方式。
    MipmapMode mipmapMode = MipmapMode::Linear; ///< mip 层之间的过滤方式。
    AddressMode addressU = AddressMode::Repeat; ///< U 方向寻址。
    AddressMode addressV = AddressMode::Repeat; ///< V 方向寻址。
    AddressMode addressW = AddressMode::Repeat; ///< W 方向寻址，3D texture 时使用。
    float mipLodBias = 0.0F; ///< mip LOD 偏移。
    float minLod = 0.0F; ///< 可采样的最小 mip LOD。
    float maxLod = std::numeric_limits<float>::max(); ///< 可采样的最大 mip LOD。
    bool enableAnisotropy = false; ///< 是否启用各向异性过滤。
    float maxAnisotropy = 1.0F; ///< 各向异性等级，启用时后端需要 clamp 到设备上限。
    bool enableCompare = false; ///< 是否启用比较采样，常用于 shadow map。
    CompareOp compareOp = CompareOp::LessOrEqual; ///< 比较采样函数。
    BorderColor borderColor = BorderColor::OpaqueBlack; ///< ClampToBorder 时使用的边框颜色。
};

/// shader 阶段位掩码，用于描述 shader 自身阶段和资源可见性。
enum class ShaderStage : std::uint32_t {
    None = 0,
    Vertex = 1u << 0,
    TessControl = 1u << 1,
    TessEvaluation = 1u << 2,
    Geometry = 1u << 3,
    Fragment = 1u << 4,
    Compute = 1u << 5,
    Task = 1u << 6,
    Mesh = 1u << 7,
    AllGraphics = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7),
    All = 0xFFFFFFFFu
};

[[nodiscard]] constexpr ShaderStage operator|(ShaderStage lhs, ShaderStage rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr ShaderStage operator&(ShaderStage lhs, ShaderStage rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr ShaderStage& operator|=(ShaderStage& lhs, ShaderStage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// shader 源码或字节码的语言/中间格式。
enum class ShaderLanguage : std::uint8_t {
    Unknown,
    GLSL,
    HLSL,
    Slang,
    MSL,
    SPIRV,
    DXIL
};

/// shader 宏定义，用于同一份源码生成不同变体。
struct ShaderDefine {
    std::string name; ///< 宏名称。
    std::string value = "1"; ///< 宏值，默认定义为 1。
};

/// shader 编译参数。运行期加载 bytecode 时可以忽略。
struct ShaderCompileOptions {
    std::string targetProfile; ///< 目标 profile，例如 vs_6_6、ps_6_6、spirv_1_6。
    std::vector<std::string> includeDirectories; ///< include 搜索路径。
    std::vector<ShaderDefine> defines; ///< 编译宏列表。
    bool enableDebugInfo = true; ///< 是否生成调试信息。
    bool optimize = false; ///< 是否开启优化，开发期通常关闭以加快编译和保留调试信息。
    bool treatWarningsAsErrors = false; ///< 是否把 warning 当作 error。
};

/**
 * @brief 单个 shader stage 的描述。
 *
 * filePath/source/bytecode 三者按后端策略选择其一或组合使用：
 * - 开发期可以给 source 或 filePath，由 shader compiler 编译。
 * - 运行期可以直接给 bytecode，例如 SPIR-V 或 DXIL。
 */
struct ShaderDesc {
    std::string debugName; ///< 调试名称。
    ShaderStage stage = ShaderStage::Vertex; ///< 当前 shader 属于哪个管线阶段。
    ShaderLanguage language = ShaderLanguage::Unknown; ///< 源码或 bytecode 的语言/格式。
    std::string entryPoint = "main"; ///< shader 入口函数名。
    std::string filePath; ///< shader 文件路径；为空表示不从文件加载。
    std::string source; ///< 内存中的 shader 源码；适合工具生成或热重载。
    std::vector<std::byte> bytecode; ///< 已编译字节码；例如 SPIR-V、DXIL、Metal library 数据。
    ShaderCompileOptions compileOptions{}; ///< 需要编译 source/filePath 时使用的编译参数。
};

/// shader specialization constant，用于在创建管线时固化常量并触发后端优化。
struct ShaderSpecializationConstant {
    std::uint32_t constantId = 0; ///< shader 中声明的 specialization constant id。
    std::vector<std::byte> data; ///< 常量原始字节数据，后端按 shader 反射信息解释类型。
};

/// 资源绑定槽类型，描述 shader 看到的资源类别。
enum class BindingType : std::uint8_t {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
    StorageTexture,
    Sampler,
    CombinedTextureSampler,
    PushConstant,
    AccelerationStructure
};

/// sampled texture 在 shader 中的采样数据类型。
enum class TextureSampleType : std::uint8_t {
    Float,
    UnfilterableFloat,
    SignedInteger,
    UnsignedInteger,
    Depth
};

/// 单个绑定槽的布局信息，相当于 descriptor binding/root parameter/argument entry。
struct BindGroupLayoutEntry {
    std::uint32_t binding = 0; ///< 绑定槽编号，对应 shader 中的 binding/register/argument index。
    BindingType type = BindingType::UniformBuffer; ///< 该槽位资源类型。
    ShaderStage visibility = ShaderStage::AllGraphics; ///< 哪些 shader stage 可以访问该槽位。
    std::uint32_t arrayCount = 1; ///< 资源数组长度；bindless 或数组纹理绑定可大于 1。
    bool writable = false; ///< storage buffer/texture 是否允许 shader 写入。
    TextureViewDimension textureViewDimension = TextureViewDimension::View2D; ///< texture 绑定期望的 view 维度。
    TextureSampleType textureSampleType = TextureSampleType::Float; ///< sampled texture 的采样类型。
    Format storageTextureFormat = Format::Undefined; ///< storage texture 格式，非 storage texture 时忽略。
};

/// 一组绑定槽布局。Vulkan 中通常对应一个 descriptor set layout。
struct BindGroupLayoutDesc {
    std::string debugName; ///< 调试名称。
    std::uint32_t set = 0; ///< 绑定组编号，对应 Vulkan set / D3D register space / Metal buffer index 分组。
    std::vector<BindGroupLayoutEntry> entries; ///< 该组内所有 binding 声明。
};

/// buffer 绑定到 shader 时的范围。
struct BufferBinding {
    BufferHandle buffer{}; ///< 被绑定的 buffer。
    std::uint64_t offset = 0; ///< 起始字节偏移。
    std::uint64_t size = WHOLE_SIZE; ///< 绑定字节范围，WHOLE_SIZE 表示从 offset 到末尾。
};

/// texture 绑定到 shader 时的 view。texture 字段可用于后端验证 view 来源。
struct TextureBinding {
    TextureViewHandle view{}; ///< 实际暴露给 shader 的 texture view。
    TextureHandle texture{}; ///< 底层 texture，可选但有助于调试和状态跟踪。
};

/// 一个实际资源绑定写入项。
struct ResourceBinding {
    std::uint32_t binding = 0; ///< 目标绑定槽编号，必须存在于 BindGroupLayoutEntry 中。
    std::uint32_t arrayElement = 0; ///< 写入资源数组的第几个元素。
    BindingType type = BindingType::UniformBuffer; ///< 本次写入的资源类型。
    BufferBinding buffer{}; ///< buffer 类型绑定时使用。
    TextureBinding texture{}; ///< texture 类型绑定时使用。
    SamplerHandle sampler{}; ///< sampler 或 combined texture sampler 绑定时使用。
};

/// 一组实际绑定资源。渲染命令只绑定 BindGroupHandle，不直接散落绑定单个资源。
struct BindGroupDesc {
    std::string debugName; ///< 调试名称。
    BindGroupLayoutHandle layout{}; ///< 该绑定组使用的布局。
    std::vector<ResourceBinding> bindings; ///< 初始资源绑定列表。
};

/// push constant/root constant 的可见范围。
struct PushConstantRange {
    ShaderStage stages = ShaderStage::AllGraphics; ///< 哪些 shader stage 可以访问该常量范围。
    std::uint32_t offset = 0; ///< 字节偏移。
    std::uint32_t size = 0; ///< 字节大小。
};

/// 管线资源布局。所有 graphics/compute pipeline 都应该引用一个布局。
struct PipelineLayoutDesc {
    std::string debugName; ///< 调试名称。
    std::vector<BindGroupLayoutHandle> bindGroupLayouts; ///< 管线可绑定的 bind group layout 列表，顺序代表 set/space。
    std::vector<PushConstantRange> pushConstants; ///< 小块高频常量范围。
};

/// shader 反射得到的资源绑定信息，可用于自动生成 BindGroupLayoutDesc。
struct ShaderResourceReflection {
    std::string name; ///< shader 中的资源名称。
    std::uint32_t set = 0; ///< 资源所在 set/space。
    std::uint32_t binding = 0; ///< 资源 binding/register。
    BindingType type = BindingType::UniformBuffer; ///< 资源类型。
    ShaderStage stages = ShaderStage::None; ///< 使用该资源的 shader stage。
    std::uint32_t arrayCount = 1; ///< 数组长度。
    std::uint32_t size = 0; ///< buffer 或 push constant 字节大小，未知时为 0。
};

/// shader 输入/输出参数反射信息。
struct ShaderParameterReflection {
    std::string name; ///< 参数名称。
    std::string semanticName; ///< HLSL 语义名，GLSL/Slang 可为空。
    std::uint32_t semanticIndex = 0; ///< 语义索引。
    std::uint32_t location = 0; ///< shader location。
    Format format = Format::Undefined; ///< 参数格式，无法推导时为 Undefined。
};

/// 一个 shader 或完整 shader program 的反射结果。
struct ShaderReflectionDesc {
    std::vector<ShaderResourceReflection> resources; ///< 资源绑定反射列表。
    std::vector<ShaderParameterReflection> inputs; ///< shader 输入参数。
    std::vector<ShaderParameterReflection> outputs; ///< shader 输出参数。
    std::vector<PushConstantRange> pushConstants; ///< push constant 范围。
};

/// 顶点属性格式，描述单个 attribute 在 vertex buffer 中的存储类型。
enum class VertexFormat : std::uint8_t {
    Float32,
    Float32x2,
    Float32x3,
    Float32x4,
    UInt32,
    UInt32x2,
    UInt32x3,
    UInt32x4,
    SInt32,
    SInt32x2,
    SInt32x3,
    SInt32x4,
    UNorm8x4,
    SNorm8x4,
    UInt16x2,
    UInt16x4,
    SInt16x2,
    SInt16x4,
    UNorm16x2,
    UNorm16x4,
    SNorm16x2,
    SNorm16x4
};

/// 顶点 buffer 的步进频率，区分逐顶点数据和实例化数据。
enum class VertexInputRate : std::uint8_t {
    PerVertex,
    PerInstance
};

/// 单个顶点属性描述，例如 position/normal/uv/color。
struct VertexAttributeDesc {
    std::string semanticName; ///< 语义名，D3D/HLSL 常用；Vulkan/GL 可用于反射和调试。
    std::uint32_t semanticIndex = 0; ///< 同名语义的索引，例如 TEXCOORD0/TEXCOORD1。
    std::uint32_t location = 0; ///< shader 输入 location。
    std::uint32_t binding = 0; ///< 来自哪个 vertex buffer binding。
    VertexFormat format = VertexFormat::Float32x3; ///< 属性格式。
    std::uint64_t offset = 0; ///< 在单个顶点结构内的字节偏移。
};

/// 一个 vertex buffer binding 的布局，可包含多个属性。
struct VertexBufferLayoutDesc {
    std::uint32_t binding = 0; ///< vertex buffer binding 编号。
    std::uint64_t stride = 0; ///< 相邻顶点/实例数据之间的字节跨度。
    VertexInputRate inputRate = VertexInputRate::PerVertex; ///< 按顶点还是按实例前进。
    std::uint32_t stepRate = 1; ///< 实例化步进倍率；大多数后端常用 1。
    std::vector<VertexAttributeDesc> attributes; ///< 该 binding 提供的属性列表。
};

/// 输入图元拓扑，描述顶点流如何被解释成点、线、三角形或 patch。
enum class PrimitiveTopology : std::uint8_t {
    PointList,
    LineList,
    LineStrip,
    TriangleList,
    TriangleStrip,
    PatchList
};

/// 多边形栅格化模式。Line/Point 不是所有平台和设备都完全支持。
enum class PolygonMode : std::uint8_t {
    Fill,
    Line,
    Point
};

/// 面剔除模式。
enum class CullMode : std::uint8_t {
    None,
    Front,
    Back,
    FrontAndBack
};

/// 正面三角形绕序。注意不同 API 的 framebuffer 坐标系可能影响最终正反面判断。
enum class FrontFace : std::uint8_t {
    CounterClockwise,
    Clockwise
};

/// 模板测试操作。
enum class StencilOp : std::uint8_t {
    Keep,
    Zero,
    Replace,
    IncrementClamp,
    DecrementClamp,
    Invert,
    IncrementWrap,
    DecrementWrap
};

/// 混合因子，用于颜色和 alpha blend。
enum class BlendFactor : std::uint8_t {
    Zero,
    One,
    SourceColor,
    OneMinusSourceColor,
    DestinationColor,
    OneMinusDestinationColor,
    SourceAlpha,
    OneMinusSourceAlpha,
    DestinationAlpha,
    OneMinusDestinationAlpha,
    ConstantColor,
    OneMinusConstantColor,
    ConstantAlpha,
    OneMinusConstantAlpha
};

/// 混合运算。
enum class BlendOp : std::uint8_t {
    Add,
    Subtract,
    ReverseSubtract,
    Min,
    Max
};

/// 逻辑颜色运算。现代渲染中较少使用，部分后端可能不支持。
enum class LogicOp : std::uint8_t {
    Clear,
    And,
    AndReverse,
    Copy,
    AndInverted,
    NoOp,
    Xor,
    Or,
    Nor,
    Equivalent,
    Invert,
    OrReverse,
    CopyInverted,
    OrInverted,
    Nand,
    Set
};

/// color attachment 写通道掩码。
enum class ColorWriteMask : std::uint8_t {
    None = 0,
    R = 1u << 0,
    G = 1u << 1,
    B = 1u << 2,
    A = 1u << 3,
    All = 0x0F
};

[[nodiscard]] constexpr ColorWriteMask operator|(ColorWriteMask lhs, ColorWriteMask rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr ColorWriteMask operator&(ColorWriteMask lhs, ColorWriteMask rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr ColorWriteMask& operator|=(ColorWriteMask& lhs, ColorWriteMask rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 创建管线时不固定、录制命令时动态设置的状态。
enum class DynamicState : std::uint8_t {
    Viewport,
    Scissor,
    LineWidth,
    DepthBias,
    BlendConstants,
    StencilReference
};

/// 输入装配阶段状态，描述顶点如何组成图元。
struct InputAssemblyState {
    PrimitiveTopology topology = PrimitiveTopology::TriangleList; ///< 图元拓扑，最常见是 TriangleList。
    bool primitiveRestart = false; ///< strip 拓扑中是否允许特殊索引重启图元。
    std::uint32_t patchControlPoints = 0; ///< PatchList 使用的控制点数量，非曲面细分时为 0。
};

/// 光栅化阶段状态。
struct RasterState {
    PolygonMode polygonMode = PolygonMode::Fill; ///< 填充、线框或点模式。
    CullMode cullMode = CullMode::Back; ///< 剔除哪些面。
    FrontFace frontFace = FrontFace::CounterClockwise; ///< 判定正面的顶点绕序。
    bool depthClampEnable = false; ///< 是否把超出 near/far 的深度 clamp，而不是裁剪。
    bool depthBiasEnable = false; ///< 是否启用深度偏移，常用于 shadow map。
    float depthBiasConstantFactor = 0.0F; ///< 常量深度偏移。
    float depthBiasClamp = 0.0F; ///< 深度偏移 clamp。
    float depthBiasSlopeFactor = 0.0F; ///< 与斜率相关的深度偏移。
    float lineWidth = 1.0F; ///< 线宽；多数现代后端只保证 1.0。
};

/// 单面的模板测试状态。
struct StencilFaceState {
    StencilOp failOp = StencilOp::Keep; ///< 模板测试失败时的操作。
    StencilOp passOp = StencilOp::Keep; ///< 模板和深度测试都通过时的操作。
    StencilOp depthFailOp = StencilOp::Keep; ///< 模板通过但深度失败时的操作。
    CompareOp compareOp = CompareOp::Always; ///< 模板比较函数。
    std::uint32_t compareMask = 0xFFFFFFFFu; ///< 比较时使用的读掩码。
    std::uint32_t writeMask = 0xFFFFFFFFu; ///< 写入 stencil buffer 时的写掩码。
    std::uint32_t reference = 0; ///< 模板参考值；也可以通过 DynamicState 动态设置。
};

/// 深度和模板测试状态。
struct DepthStencilState {
    bool depthTestEnable = true; ///< 是否读取 depth buffer 做深度测试。
    bool depthWriteEnable = true; ///< 深度测试通过后是否写入 depth buffer。
    CompareOp depthCompareOp = CompareOp::Less; ///< 深度比较函数。
    bool depthBoundsTestEnable = false; ///< 是否启用深度范围测试，部分后端不支持。
    float minDepthBounds = 0.0F; ///< 深度范围测试下限。
    float maxDepthBounds = 1.0F; ///< 深度范围测试上限。
    bool stencilTestEnable = false; ///< 是否启用 stencil 测试。
    StencilFaceState front{}; ///< 正面模板状态。
    StencilFaceState back{}; ///< 背面模板状态。
};

/// 多重采样状态。
struct MultisampleState {
    SampleCount samples = SampleCount::Count1; ///< MSAA 采样数，必须与 render target 采样数兼容。
    bool sampleShadingEnable = false; ///< 是否启用 per-sample shading。
    float minSampleShading = 1.0F; ///< per-sample shading 的最小采样比例。
    std::uint64_t sampleMask = std::numeric_limits<std::uint64_t>::max(); ///< 采样位掩码。
    bool alphaToCoverageEnable = false; ///< 是否把 alpha 转成 coverage，常用于植被边缘。
    bool alphaToOneEnable = false; ///< 是否把 alpha 强制为 1，支持度有限。
};

/// 单个 color attachment 的混合状态。
struct ColorBlendAttachmentState {
    bool blendEnable = false; ///< 是否启用 blending。
    BlendFactor sourceColor = BlendFactor::One; ///< 源颜色因子。
    BlendFactor destinationColor = BlendFactor::Zero; ///< 目标颜色因子。
    BlendOp colorOp = BlendOp::Add; ///< 颜色混合运算。
    BlendFactor sourceAlpha = BlendFactor::One; ///< 源 alpha 因子。
    BlendFactor destinationAlpha = BlendFactor::Zero; ///< 目标 alpha 因子。
    BlendOp alphaOp = BlendOp::Add; ///< alpha 混合运算。
    ColorWriteMask writeMask = ColorWriteMask::All; ///< 允许写入的颜色通道。
};

/// 整个管线的混合状态，attachments 数量应与 colorFormats 数量一致。
struct BlendState {
    bool logicOpEnable = false; ///< 是否启用逻辑运算，启用时通常不能同时使用普通 blending。
    LogicOp logicOp = LogicOp::Copy; ///< 逻辑运算类型。
    std::array<float, 4> blendConstants{0.0F, 0.0F, 0.0F, 0.0F}; ///< 常量混合颜色。
    std::vector<ColorBlendAttachmentState> attachments; ///< 每个 color attachment 的混合设置。
};

/// 图形管线完整描述。后端可以用它生成 PSO/pipeline，并缓存相同描述。
struct GraphicsPipelineDesc {
    std::string debugName; ///< 调试名称。
    PipelineCacheHandle cache{}; ///< 可选管线缓存；无效句柄表示使用后端默认缓存策略。
    PipelineLayoutHandle layout{}; ///< 资源绑定布局。
    std::vector<ShaderDesc> shaders; ///< 图形阶段 shader 列表，通常至少包含 vertex 和 fragment。
    std::vector<ShaderSpecializationConstant> specializationConstants; ///< 管线创建时固化的 shader 常量。
    std::vector<VertexBufferLayoutDesc> vertexBuffers; ///< 顶点输入布局。
    InputAssemblyState inputAssembly{}; ///< 输入装配状态。
    RasterState raster{}; ///< 光栅化状态。
    DepthStencilState depthStencil{}; ///< 深度模板状态。
    MultisampleState multisample{}; ///< MSAA 状态。
    BlendState blend{}; ///< 颜色混合状态。
    std::vector<DynamicState> dynamicStates{DynamicState::Viewport, DynamicState::Scissor}; ///< 命令录制时动态设置的状态。
    std::vector<Format> colorFormats; ///< color attachment 格式列表；动态渲染后端可直接使用。
    Format depthStencilFormat = Format::Undefined; ///< depth-stencil attachment 格式，没有深度则为 Undefined。
    RenderPassHandle compatibleRenderPass{}; ///< 传统 render pass 后端的兼容 render pass。
    std::uint32_t subpass = 0; ///< 使用 compatibleRenderPass 时的 subpass index。
};

/// 计算管线描述。
struct ComputePipelineDesc {
    std::string debugName; ///< 调试名称。
    PipelineCacheHandle cache{}; ///< 可选管线缓存；无效句柄表示使用后端默认缓存策略。
    PipelineLayoutHandle layout{}; ///< 资源绑定布局。
    ShaderDesc shader{}; ///< compute shader。
    std::vector<ShaderSpecializationConstant> specializationConstants; ///< 管线创建时固化的 shader 常量。
};

/// 管线缓存描述。后端可以把 initialData 解释为上次保存的 pipeline cache blob。
struct PipelineCacheDesc {
    std::string debugName; ///< 调试名称。
    std::vector<std::byte> initialData; ///< 初始缓存数据，空表示新建。
    bool allowSerialization = true; ///< 是否允许在退出时导出缓存数据。
};

/// GPU 查询类型。
enum class QueryType : std::uint8_t {
    Timestamp,
    Occlusion,
    PipelineStatistics
};

/// pipeline statistics 查询的统计项位。
enum class PipelineStatisticFlags : std::uint32_t {
    None = 0,
    InputAssemblyVertices = 1u << 0,
    InputAssemblyPrimitives = 1u << 1,
    VertexShaderInvocations = 1u << 2,
    GeometryShaderInvocations = 1u << 3,
    GeometryShaderPrimitives = 1u << 4,
    ClippingInvocations = 1u << 5,
    ClippingPrimitives = 1u << 6,
    FragmentShaderInvocations = 1u << 7,
    TessControlShaderPatches = 1u << 8,
    TessEvaluationShaderInvocations = 1u << 9,
    ComputeShaderInvocations = 1u << 10
};

[[nodiscard]] constexpr PipelineStatisticFlags operator|(PipelineStatisticFlags lhs, PipelineStatisticFlags rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr PipelineStatisticFlags operator&(PipelineStatisticFlags lhs, PipelineStatisticFlags rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr PipelineStatisticFlags& operator|=(PipelineStatisticFlags& lhs, PipelineStatisticFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 查询池描述。timestamp 用于 GPU 时间，occlusion 用于遮挡查询。
struct QueryPoolDesc {
    std::string debugName; ///< 调试名称。
    QueryType type = QueryType::Timestamp; ///< 查询类型。
    std::uint32_t queryCount = 1; ///< 查询槽数量。
    PipelineStatisticFlags statistics = PipelineStatisticFlags::None; ///< PipelineStatistics 查询时需要的统计项。
};

/// 渲染开始时 attachment 内容如何处理。
enum class LoadOp : std::uint8_t {
    Load,
    Clear,
    DontCare
};

/// 渲染结束时 attachment 内容如何处理。
enum class StoreOp : std::uint8_t {
    Store,
    DontCare
};

/// MSAA resolve 行为。Average 是最常见的颜色 resolve；深度 resolve 后端支持差异较大。
enum class ResolveMode : std::uint8_t {
    None,
    Average,
    Min,
    Max,
    SampleZero
};

/// 单个 color render target 的绑定和 load/store 行为。
struct RenderTargetDesc {
    TextureViewHandle view{}; ///< color attachment view。
    TextureViewHandle resolveView{}; ///< MSAA resolve 目标；无效句柄表示不 resolve。
    ResolveMode resolveMode = ResolveMode::None; ///< resolve 行为。
    LoadOp loadOp = LoadOp::Clear; ///< pass 开始时如何处理已有内容。
    StoreOp storeOp = StoreOp::Store; ///< pass 结束时是否保留结果。
    ClearColor clearColor{}; ///< loadOp 为 Clear 时使用的清屏颜色。
    ResourceState stateBefore = ResourceState::Undefined; ///< pass 开始前期望状态。
    ResourceState stateAfter = ResourceState::RenderTarget; ///< pass 结束后目标状态。
};

/// depth-stencil attachment 的绑定和 load/store 行为。
struct DepthStencilTargetDesc {
    TextureViewHandle view{}; ///< depth-stencil attachment view。
    TextureViewHandle depthResolveView{}; ///< depth resolve 目标；无效句柄表示不 resolve。
    TextureViewHandle stencilResolveView{}; ///< stencil resolve 目标；无效句柄表示不 resolve。
    ResolveMode depthResolveMode = ResolveMode::None; ///< depth resolve 行为。
    ResolveMode stencilResolveMode = ResolveMode::None; ///< stencil resolve 行为。
    LoadOp depthLoadOp = LoadOp::Clear; ///< depth pass 开始行为。
    StoreOp depthStoreOp = StoreOp::Store; ///< depth pass 结束行为。
    LoadOp stencilLoadOp = LoadOp::DontCare; ///< stencil pass 开始行为。
    StoreOp stencilStoreOp = StoreOp::DontCare; ///< stencil pass 结束行为。
    ClearDepthStencil clearValue{}; ///< 清除 depth/stencil 时使用的值。
    ResourceState stateBefore = ResourceState::Undefined; ///< pass 开始前期望状态。
    ResourceState stateAfter = ResourceState::DepthWrite; ///< pass 结束后目标状态。
};

/// 一次 render pass 的附件集合和渲染区域。
struct RenderPassDesc {
    std::string debugName; ///< 调试名称。
    Rect2D renderArea{}; ///< 渲染影响的区域，一般等于 framebuffer 尺寸。
    std::vector<RenderTargetDesc> colorTargets; ///< color attachments。
    std::optional<DepthStencilTargetDesc> depthStencilTarget; ///< 可选 depth-stencil attachment。
};

/// framebuffer 描述，一般用于传统 render pass API；动态渲染后端可把它当附件集合缓存。
struct FramebufferDesc {
    std::string debugName; ///< 调试名称。
    RenderPassHandle renderPass{}; ///< 兼容的 render pass。
    std::vector<TextureViewHandle> attachments; ///< 实际绑定的附件 view。
    Extent2D extent{}; ///< framebuffer 宽高。
    std::uint32_t layers = 1; ///< framebuffer 层数。
};

/// swapchain 色彩空间。后端需要根据平台支持选择最接近的实际色彩空间。
enum class ColorSpace : std::uint8_t {
    SRGBNonlinear,
    DisplayP3Nonlinear,
    ExtendedSRGBLinear,
    HDR10ST2084,
    HDR10HLG
};

/// swapchain 输出表面变换。移动端或可旋转窗口系统可能会用到。
enum class SurfaceTransform : std::uint8_t {
    Identity,
    Rotate90,
    Rotate180,
    Rotate270,
    HorizontalMirror,
    HorizontalMirrorRotate90,
    HorizontalMirrorRotate180,
    HorizontalMirrorRotate270,
    Inherit
};

/// swapchain alpha 合成模式，用于透明窗口或系统 compositor。
enum class CompositeAlphaMode : std::uint8_t {
    Opaque,
    PreMultiplied,
    PostMultiplied,
    Inherit
};

/// 交换链描述。窗口系统相关 handle 不放在这里，由平台层交给具体后端。
struct SwapchainDesc {
    std::string debugName; ///< 调试名称。
    Extent2D extent{}; ///< drawable 尺寸。
    Format preferredFormat = Format::BGRA8SRGB; ///< 期望后备缓冲格式，后端可根据平台支持选择最接近格式。
    ColorSpace colorSpace = ColorSpace::SRGBNonlinear; ///< 期望色彩空间。
    PresentMode presentMode = PresentMode::Fifo; ///< 呈现模式。
    std::uint32_t imageCount = 2; ///< swapchain image 数量，常见为 2 或 3。
    SurfaceTransform preTransform = SurfaceTransform::Identity; ///< 呈现前表面变换。
    CompositeAlphaMode compositeAlpha = CompositeAlphaMode::Opaque; ///< 与系统 compositor 的 alpha 合成方式。
    bool allowTearing = false; ///< 是否允许无垂直同步撕裂显示，平台不支持时应忽略。
    bool fullscreen = false; ///< 是否请求独占或无边框全屏，具体由平台层实现。
    bool hdr = false; ///< 是否请求 HDR 输出，后端需要结合 format/colorSpace 判断。
};

/// texture 子资源范围，用于 barrier、copy、view 等操作。
struct TextureSubresourceRange {
    TextureAspect aspect = TextureAspect::All; ///< 覆盖的 aspect，All 表示由格式自动推导。
    std::uint32_t baseMipLevel = 0; ///< 起始 mip。
    std::uint32_t mipLevelCount = 1; ///< mip 数量。
    std::uint32_t baseArrayLayer = 0; ///< 起始数组层。
    std::uint32_t arrayLayerCount = 1; ///< 数组层数量。
};

/// 全局内存 barrier，用于没有特定资源但需要约束前后访问顺序的情况。
struct GlobalBarrier {
    PipelineStage sourceStages = PipelineStage::AllCommands; ///< barrier 前需要等待的管线阶段。
    PipelineStage destinationStages = PipelineStage::AllCommands; ///< barrier 后允许继续的管线阶段。
    AccessFlags sourceAccess = AccessFlags::MemoryWrite; ///< barrier 前的访问类型。
    AccessFlags destinationAccess = AccessFlags::MemoryRead; ///< barrier 后的访问类型。
};

/// texture 状态转换请求。显式 API 会翻译成 image barrier。
struct TextureBarrier {
    TextureHandle texture{}; ///< 目标 texture。
    TextureSubresourceRange range{}; ///< 需要转换的子资源范围。
    ResourceState before = ResourceState::Undefined; ///< 转换前状态。
    ResourceState after = ResourceState::Common; ///< 转换后状态。
    PipelineStage sourceStages = PipelineStage::AllCommands; ///< 转换前需要等待的管线阶段。
    PipelineStage destinationStages = PipelineStage::AllCommands; ///< 转换后可继续的管线阶段。
    AccessFlags sourceAccess = AccessFlags::None; ///< 转换前资源访问类型；None 表示由 ResourceState 推导。
    AccessFlags destinationAccess = AccessFlags::None; ///< 转换后资源访问类型；None 表示由 ResourceState 推导。
    QueueType sourceQueue = QueueType::Graphics; ///< 源队列所有权。
    QueueType destinationQueue = QueueType::Graphics; ///< 目标队列所有权，跨队列时后端需要 ownership transfer。
    bool discardContents = false; ///< true 表示旧内容不需要保留，后端可使用更便宜的布局转换。
};

/// buffer 状态转换请求。显式 API 会翻译成 buffer/resource barrier。
struct BufferBarrier {
    BufferHandle buffer{}; ///< 目标 buffer。
    std::uint64_t offset = 0; ///< 起始字节偏移。
    std::uint64_t size = WHOLE_SIZE; ///< 覆盖字节范围。
    ResourceState before = ResourceState::Undefined; ///< 转换前状态。
    ResourceState after = ResourceState::Common; ///< 转换后状态。
    PipelineStage sourceStages = PipelineStage::AllCommands; ///< 转换前需要等待的管线阶段。
    PipelineStage destinationStages = PipelineStage::AllCommands; ///< 转换后可继续的管线阶段。
    AccessFlags sourceAccess = AccessFlags::None; ///< 转换前资源访问类型；None 表示由 ResourceState 推导。
    AccessFlags destinationAccess = AccessFlags::None; ///< 转换后资源访问类型；None 表示由 ResourceState 推导。
    QueueType sourceQueue = QueueType::Graphics; ///< 源队列所有权。
    QueueType destinationQueue = QueueType::Graphics; ///< 目标队列所有权。
};

/// 一批资源 barrier，通常在 pass 开始前或 copy/dispatch/draw 之间提交。
struct ResourceBarriers {
    std::vector<GlobalBarrier> globals; ///< 全局内存 barriers。
    std::vector<TextureBarrier> textures; ///< texture barriers。
    std::vector<BufferBarrier> buffers; ///< buffer barriers。
};

/// CPU 到 GPU buffer 上传请求。后端负责 staging buffer 和 copy command。
struct BufferUploadDesc {
    BufferHandle destination{}; ///< 目标 buffer。
    std::uint64_t destinationOffset = 0; ///< 写入目标 buffer 的字节偏移。
    std::vector<std::byte> data; ///< 待上传的原始字节。
};

/// CPU 到 GPU texture 上传请求。行对齐要求由后端处理。
struct TextureUploadDesc {
    TextureHandle destination{}; ///< 目标 texture。
    std::uint32_t mipLevel = 0; ///< 目标 mip。
    std::uint32_t arrayLayer = 0; ///< 目标数组层。
    Offset3D offset{}; ///< 写入区域偏移。
    Extent3D extent{}; ///< 写入区域尺寸。
    std::uint64_t bytesPerRow = 0; ///< 源数据每行字节数；0 表示后端按格式和宽度推导。
    std::uint64_t rowsPerImage = 0; ///< 3D/array 数据每张 image 行数；0 表示后端推导。
    std::vector<std::byte> data; ///< 待上传的原始像素字节。
};

/// 一帧或一次加载阶段的上传请求集合。
struct UploadBatchDesc {
    std::vector<BufferUploadDesc> buffers; ///< buffer 上传列表。
    std::vector<TextureUploadDesc> textures; ///< texture 上传列表。
};

/// buffer 到 buffer 的拷贝区域。
struct BufferCopyDesc {
    BufferHandle source{}; ///< 源 buffer。
    BufferHandle destination{}; ///< 目标 buffer。
    std::uint64_t sourceOffset = 0; ///< 源字节偏移。
    std::uint64_t destinationOffset = 0; ///< 目标字节偏移。
    std::uint64_t size = 0; ///< 拷贝字节数。
};

/// texture 拷贝位置，包含 texture 和具体子资源。
struct TextureCopyLocation {
    TextureHandle texture{}; ///< 目标或源 texture。
    TextureAspect aspect = TextureAspect::Color; ///< 拷贝的 aspect。
    std::uint32_t mipLevel = 0; ///< mip 层。
    std::uint32_t arrayLayer = 0; ///< 数组层。
    Offset3D offset{}; ///< 子资源内偏移。
};

/// texture 到 texture 的拷贝区域。
struct TextureCopyDesc {
    TextureCopyLocation source{}; ///< 源 texture 位置。
    TextureCopyLocation destination{}; ///< 目标 texture 位置。
    Extent3D extent{}; ///< 拷贝尺寸。
};

/// buffer 和 texture 之间的拷贝区域。
struct BufferTextureCopyDesc {
    BufferHandle buffer{}; ///< 参与拷贝的 buffer。
    TextureCopyLocation texture{}; ///< 参与拷贝的 texture 位置。
    std::uint64_t bufferOffset = 0; ///< buffer 起始字节偏移。
    std::uint64_t bytesPerRow = 0; ///< buffer 中每行字节数；0 表示后端按格式推导。
    std::uint64_t rowsPerImage = 0; ///< buffer 中每张 image 行数；0 表示后端推导。
    Extent3D extent{}; ///< 拷贝尺寸。
};

/// texture blit 区域，可用于缩放、格式兼容转换或 mipmap 生成。
struct TextureBlitDesc {
    TextureCopyLocation source{}; ///< 源 texture 位置。
    TextureCopyLocation destination{}; ///< 目标 texture 位置。
    Extent3D sourceExtent{}; ///< 源区域尺寸。
    Extent3D destinationExtent{}; ///< 目标区域尺寸。
    FilterMode filter = FilterMode::Linear; ///< 缩放过滤方式。
};

/// 自动生成 mipmap 的请求。
struct MipmapGenerationDesc {
    TextureHandle texture{}; ///< 目标 texture。
    TextureAspect aspect = TextureAspect::Color; ///< 生成哪个 aspect 的 mip。
    std::uint32_t baseArrayLayer = 0; ///< 起始数组层。
    std::uint32_t arrayLayerCount = 1; ///< 数组层数量。
    FilterMode filter = FilterMode::Linear; ///< 下采样过滤方式。
};

/// index buffer 中索引元素的整数类型。
enum class IndexType : std::uint8_t {
    UInt16,
    UInt32
};

/// draw call 绑定的单个 vertex stream。
struct VertexStream {
    BufferHandle buffer{}; ///< 顶点 buffer。
    std::uint32_t binding = 0; ///< 对应 VertexBufferLayoutDesc::binding。
    std::uint64_t offset = 0; ///< buffer 起始字节偏移。
    std::uint64_t stride = 0; ///< 运行期 stride；0 表示使用 pipeline layout 中的 stride。
};

/// draw indexed 使用的 index stream。
struct IndexStream {
    BufferHandle buffer{}; ///< 索引 buffer。
    IndexType indexType = IndexType::UInt32; ///< 索引元素类型。
    std::uint64_t offset = 0; ///< 起始字节偏移。
    std::uint32_t indexCount = 0; ///< 可读取索引数量，便于验证 draw 范围。
};

/// 轴对齐包围盒，用于视锥裁剪、光源裁剪和加速结构构建。
struct BoundingBox {
    glm::vec3 min{0.0F}; ///< 最小点。
    glm::vec3 max{0.0F}; ///< 最大点。
};

/// 包围球，用于更便宜的粗裁剪或 LOD 选择。
struct BoundingSphere {
    glm::vec3 center{0.0F}; ///< 球心。
    float radius = 0.0F; ///< 半径。
};

/// mesh 内的一个可独立绘制部分。
struct SubmeshDesc {
    std::string name; ///< 子网格名称。
    std::uint32_t firstVertex = 0; ///< 非索引绘制的起始顶点。
    std::uint32_t vertexCount = 0; ///< 顶点数量。
    std::uint32_t firstIndex = 0; ///< 索引绘制的起始索引。
    std::uint32_t indexCount = 0; ///< 索引数量。
    std::uint32_t firstInstance = 0; ///< 默认起始实例。
    std::uint32_t instanceCount = 1; ///< 默认实例数量。
    std::int32_t materialIndex = -1; ///< 默认材质索引，-1 表示未指定。
    glm::vec3 boundsMin{0.0F}; ///< 本地空间 AABB 最小点。
    glm::vec3 boundsMax{0.0F}; ///< 本地空间 AABB 最大点。
    BoundingSphere boundsSphere{}; ///< 本地空间包围球。
};

/// mesh 资源描述，只引用 GPU buffer，不保存 CPU 顶点数组。
struct MeshDesc {
    std::string debugName; ///< 调试名称。
    std::vector<VertexStream> vertexStreams; ///< mesh 默认顶点流。
    std::optional<IndexStream> indexStream; ///< 可选索引流。
    std::vector<SubmeshDesc> submeshes; ///< 子网格列表。
};

/// 材质中按名字记录的贴图槽，便于编辑器和材质系统做参数管理。
struct TextureSlot {
    std::string name; ///< 槽位名，例如 baseColor、normal、metallicRoughness。
    TextureViewHandle texture{}; ///< 绑定的贴图 view。
    SamplerHandle sampler{}; ///< 贴图采样器。
};

/// 材质参数类型，用于编辑器和自动打包 uniform/push constant 数据。
enum class MaterialParameterType : std::uint8_t {
    Float,
    Float2,
    Float3,
    Float4,
    Int,
    UInt,
    Bool
};

/// 命名材质参数。数值统一存放在 value 中，具体读取方式由 type 决定。
struct MaterialParameter {
    std::string name; ///< 参数名，例如 roughness、metallic、baseColorFactor。
    MaterialParameterType type = MaterialParameterType::Float4; ///< 参数类型。
    glm::vec4 value{0.0F}; ///< 参数值，标量使用 x，bool 使用 x 是否非 0。
};

/// 材质描述，表示“用哪个 pipeline + 哪些资源参数绘制”。
struct MaterialDesc {
    std::string debugName; ///< 调试名称。
    PipelineHandle pipeline{}; ///< 材质默认管线。
    std::vector<BindGroupHandle> bindGroups; ///< 材质级资源绑定组。
    std::vector<TextureSlot> textureSlots; ///< 材质贴图槽。
    std::vector<MaterialParameter> parameters; ///< 命名材质参数。
    std::vector<std::byte> pushConstantData; ///< 材质 push constant/root constant 数据。
};

/// 非索引绘制命令。通常由可见性裁剪和排序阶段生成。
struct DrawCommand {
    PipelineHandle pipeline{}; ///< 使用的图形管线。
    std::vector<BindGroupHandle> bindGroups; ///< 绘制前需要绑定的资源组。
    std::vector<VertexStream> vertexStreams; ///< 本次绘制的顶点流，可覆盖 mesh 默认流。
    std::uint32_t vertexCount = 0; ///< 绘制顶点数。
    std::uint32_t instanceCount = 1; ///< 实例数量。
    std::uint32_t firstVertex = 0; ///< 起始顶点。
    std::uint32_t firstInstance = 0; ///< 起始实例。
};

/// 索引绘制命令。
struct DrawIndexedCommand {
    PipelineHandle pipeline{}; ///< 使用的图形管线。
    std::vector<BindGroupHandle> bindGroups; ///< 绘制前需要绑定的资源组。
    std::vector<VertexStream> vertexStreams; ///< 本次绘制的顶点流。
    IndexStream indexStream{}; ///< 索引流。
    std::uint32_t indexCount = 0; ///< 绘制索引数。
    std::uint32_t instanceCount = 1; ///< 实例数量。
    std::uint32_t firstIndex = 0; ///< 起始索引。
    std::int32_t vertexOffsetElements = 0; ///< 索引值加上的顶点偏移，单位是顶点不是字节。
    std::uint32_t firstInstance = 0; ///< 起始实例。
};

/// compute dispatch 命令。
struct DispatchCommand {
    PipelineHandle pipeline{}; ///< 使用的计算管线。
    std::vector<BindGroupHandle> bindGroups; ///< dispatch 前需要绑定的资源组。
    std::uint32_t groupCountX = 1; ///< X 方向 workgroup 数量。
    std::uint32_t groupCountY = 1; ///< Y 方向 workgroup 数量。
    std::uint32_t groupCountZ = 1; ///< Z 方向 workgroup 数量。
};

/// 间接非索引绘制命令，参数从 GPU buffer 读取。
struct DrawIndirectCommand {
    PipelineHandle pipeline{}; ///< 使用的图形管线。
    std::vector<BindGroupHandle> bindGroups; ///< 绘制前需要绑定的资源组。
    std::vector<VertexStream> vertexStreams; ///< 本次绘制的顶点流。
    BufferHandle argumentBuffer{}; ///< 间接参数 buffer。
    std::uint64_t argumentOffset = 0; ///< 第一个间接参数的字节偏移。
    std::uint32_t drawCount = 1; ///< 执行的间接绘制数量。
    std::uint32_t stride = 0; ///< 每个间接参数结构的字节跨度；0 表示使用后端默认结构大小。
    BufferHandle countBuffer{}; ///< 可选 draw count buffer；无效句柄表示使用 drawCount。
    std::uint64_t countBufferOffset = 0; ///< countBuffer 中 draw count 的字节偏移。
};

/// 间接索引绘制命令。
struct DrawIndexedIndirectCommand {
    PipelineHandle pipeline{}; ///< 使用的图形管线。
    std::vector<BindGroupHandle> bindGroups; ///< 绘制前需要绑定的资源组。
    std::vector<VertexStream> vertexStreams; ///< 本次绘制的顶点流。
    IndexStream indexStream{}; ///< 索引流。
    BufferHandle argumentBuffer{}; ///< 间接参数 buffer。
    std::uint64_t argumentOffset = 0; ///< 第一个间接参数的字节偏移。
    std::uint32_t drawCount = 1; ///< 执行的间接绘制数量。
    std::uint32_t stride = 0; ///< 每个间接参数结构的字节跨度；0 表示使用后端默认结构大小。
    BufferHandle countBuffer{}; ///< 可选 draw count buffer；无效句柄表示使用 drawCount。
    std::uint64_t countBufferOffset = 0; ///< countBuffer 中 draw count 的字节偏移。
};

/// 间接 compute dispatch 命令，groupCount 从 GPU buffer 读取。
struct DispatchIndirectCommand {
    PipelineHandle pipeline{}; ///< 使用的计算管线。
    std::vector<BindGroupHandle> bindGroups; ///< dispatch 前需要绑定的资源组。
    BufferHandle argumentBuffer{}; ///< 间接参数 buffer。
    std::uint64_t argumentOffset = 0; ///< 参数字节偏移。
};

/// GPU 调试标记，用于 RenderDoc、PIX、Xcode GPU Frame Debugger 等工具分组显示。
struct DebugMarkerDesc {
    std::string name; ///< 标记名称。
    std::array<float, 4> color{0.2F, 0.6F, 1.0F, 1.0F}; ///< 标记颜色。
};

/// 写入 timestamp 查询的命令。
struct TimestampQueryCommand {
    QueryPoolHandle queryPool{}; ///< timestamp 查询池。
    std::uint32_t queryIndex = 0; ///< 写入的查询槽。
    PipelineStage stage = PipelineStage::BottomOfPipe; ///< 记录时间戳的管线阶段。
};

/// 重置查询范围的命令。
struct ResetQueryCommand {
    QueryPoolHandle queryPool{}; ///< 查询池。
    std::uint32_t firstQuery = 0; ///< 起始查询槽。
    std::uint32_t queryCount = 1; ///< 查询数量。
};

/// 拷贝查询结果到 buffer 的命令，便于 CPU 或后续 GPU pass 读取。
struct ResolveQueryCommand {
    QueryPoolHandle queryPool{}; ///< 查询池。
    std::uint32_t firstQuery = 0; ///< 起始查询槽。
    std::uint32_t queryCount = 1; ///< 查询数量。
    BufferHandle destination{}; ///< 结果写入的 buffer。
    std::uint64_t destinationOffset = 0; ///< 目标字节偏移。
    std::uint64_t stride = sizeof(std::uint64_t); ///< 每个查询结果的字节跨度。
    bool waitForResults = true; ///< 是否等待查询结果可用。
};

/// 一个 render graph pass 对应的实际工作负载。
struct RenderPassWorkload {
    std::string passName; ///< 对应 RenderGraphPassDesc::name，用名字关联 pass 描述和命令。
    Viewport viewport{}; ///< 本 pass 默认 viewport。
    Rect2D scissor{}; ///< 本 pass 默认 scissor。
    ResourceBarriers barriers; ///< pass 内需要显式插入的额外 barrier。
    std::vector<DebugMarkerDesc> debugMarkers; ///< pass 内调试标记；后端可按顺序插入 begin/end marker。
    std::vector<BufferCopyDesc> bufferCopies; ///< buffer 到 buffer 拷贝命令。
    std::vector<TextureCopyDesc> textureCopies; ///< texture 到 texture 拷贝命令。
    std::vector<BufferTextureCopyDesc> bufferToTextureCopies; ///< buffer 到 texture 拷贝命令。
    std::vector<BufferTextureCopyDesc> textureToBufferCopies; ///< texture 到 buffer 拷贝命令。
    std::vector<TextureBlitDesc> textureBlits; ///< texture blit 命令。
    std::vector<MipmapGenerationDesc> mipmapGenerations; ///< mipmap 生成命令。
    std::vector<ResetQueryCommand> queryResets; ///< 查询重置命令。
    std::vector<TimestampQueryCommand> timestampWrites; ///< timestamp 写入命令。
    std::vector<ResolveQueryCommand> queryResolves; ///< 查询结果拷贝命令。
    std::vector<DrawCommand> draws; ///< 非索引绘制列表。
    std::vector<DrawIndexedCommand> indexedDraws; ///< 索引绘制列表。
    std::vector<DrawIndirectCommand> indirectDraws; ///< 间接非索引绘制列表。
    std::vector<DrawIndexedIndirectCommand> indexedIndirectDraws; ///< 间接索引绘制列表。
    std::vector<DispatchCommand> dispatches; ///< 计算 dispatch 列表。
    std::vector<DispatchIndirectCommand> indirectDispatches; ///< 间接计算 dispatch 列表。
};

/// semaphore 类型。timeline semaphore 能表达递增计数，binary semaphore 只表达一次信号。
enum class SemaphoreType : std::uint8_t {
    Binary,
    Timeline
};

/// semaphore 创建描述。
struct SemaphoreDesc {
    std::string debugName; ///< 调试名称。
    SemaphoreType type = SemaphoreType::Binary; ///< 同步对象类型。
    std::uint64_t initialValue = 0; ///< timeline semaphore 初始值，binary semaphore 忽略。
};

/// fence 创建描述。fence 通常用于 CPU 等待某帧 GPU 工作完成。
struct FenceDesc {
    std::string debugName; ///< 调试名称。
    bool signaled = false; ///< 创建时是否为已触发状态。
};

/// 队列提交前等待的 semaphore。
struct QueueWaitDesc {
    SemaphoreHandle semaphore{}; ///< 需要等待的 semaphore。
    std::uint64_t value = 0; ///< timeline semaphore 等待值；binary semaphore 忽略。
    PipelineStage stages = PipelineStage::AllCommands; ///< 等待影响的管线阶段。
};

/// 队列提交完成后 signal 的 semaphore。
struct QueueSignalDesc {
    SemaphoreHandle semaphore{}; ///< 需要 signal 的 semaphore。
    std::uint64_t value = 0; ///< timeline semaphore signal 值；binary semaphore 忽略。
};

/// 一次队列提交描述。后端可把 passNames 映射到实际 command buffer 列表。
struct QueueSubmitDesc {
    std::string debugName; ///< 调试名称。
    QueueType queue = QueueType::Graphics; ///< 提交到哪类队列。
    std::vector<std::string> passNames; ///< 本次提交包含的 RenderGraph pass 名称。
    std::vector<QueueWaitDesc> waits; ///< 提交前等待的同步对象。
    std::vector<QueueSignalDesc> signals; ///< 提交完成后 signal 的同步对象。
    FenceHandle fence{}; ///< 可选 fence，用于 CPU 等待提交完成。
};

/// 呈现请求。窗口系统原生 surface 仍由平台层和后端保存。
struct PresentDesc {
    SwapchainHandle swapchain{}; ///< 目标 swapchain。
    std::uint32_t imageIndex = 0; ///< 要呈现的 swapchain image 下标。
    std::vector<SemaphoreHandle> waitSemaphores; ///< present 前需要等待的 semaphore。
    PresentMode presentMode = PresentMode::Fifo; ///< 本次呈现期望模式，后端可按 swapchain 实际模式处理。
    bool allowTearing = false; ///< 本次呈现是否允许 tearing。
};

/// 物体变换数据，保留上一帧矩阵可用于 motion vector/TAA。
struct TransformData {
    glm::mat4 localToWorld{1.0F}; ///< 当前帧本地到世界矩阵。
    glm::mat4 previousLocalToWorld{1.0F}; ///< 上一帧本地到世界矩阵。
};

/// 相机数据。投影矩阵的坐标系差异应由创建矩阵或后端适配层统一处理。
struct CameraData {
    glm::mat4 view{1.0F}; ///< 世界到视图矩阵。
    glm::mat4 projection{1.0F}; ///< 投影矩阵。
    glm::mat4 viewProjection{1.0F}; ///< projection * view，供 shader 直接使用。
    glm::mat4 previousViewProjection{1.0F}; ///< 上一帧 viewProjection，用于 motion vector/TAA。
    glm::vec3 position{0.0F}; ///< 世界空间相机位置。
    float nearPlane = 0.1F; ///< 近裁剪面距离。
    float farPlane = 1000.0F; ///< 远裁剪面距离。
    float verticalFovRadians = 1.0471975512F; ///< 垂直视场角，单位弧度，默认约 60 度。
    glm::vec2 jitter{0.0F}; ///< 当前帧投影抖动，TAA/TSR 常用。
    glm::vec2 previousJitter{0.0F}; ///< 上一帧投影抖动。
};

/// 光源类型。
enum class LightType : std::uint8_t {
    Directional,
    Point,
    Spot
};

/// CPU 侧光源描述，后续可打包进 uniform/storage buffer。
struct LightData {
    LightType type = LightType::Directional; ///< 光源类型。
    glm::vec3 color{1.0F}; ///< 线性空间颜色。
    float intensity = 1.0F; ///< 光强，具体物理单位由渲染器约定。
    glm::vec3 direction{0.0F, -1.0F, 0.0F}; ///< 方向光/聚光灯方向。
    float range = 10.0F; ///< 点光/聚光影响半径。
    glm::vec3 position{0.0F}; ///< 点光/聚光世界位置。
    float innerConeRadians = 0.0F; ///< 聚光内锥角，单位弧度。
    float outerConeRadians = 0.7853981634F; ///< 聚光外锥角，默认约 45 度。
};

/// 渲染队列，用于粗粒度排序和选择 pass。
enum class RenderQueue : std::uint8_t {
    Background,
    Opaque,
    AlphaTest,
    Transparent,
    Overlay
};

/// 场景中的一个可渲染物体实例。
struct RenderObjectDesc {
    std::string debugName; ///< 调试名称。
    MeshHandle mesh{}; ///< 使用的 mesh。
    MaterialHandle material{}; ///< 使用的材质。
    TransformData transform{}; ///< 物体变换。
    std::uint32_t submeshIndex = 0; ///< 绘制 mesh 中的哪个 submesh。
    RenderQueue queue = RenderQueue::Opaque; ///< 渲染队列。
    std::uint64_t sortingKey = 0; ///< 精细排序 key，可编码 pipeline/material/depth 等信息。
    std::uint32_t layerMask = 0xFFFFFFFFu; ///< 渲染层掩码，供相机/pass 过滤。
    BoundingBox worldBounds{}; ///< 世界空间包围盒，裁剪阶段可直接使用。
    BoundingSphere worldBoundsSphere{}; ///< 世界空间包围球，粗裁剪和 LOD 可直接使用。
    bool visible = true; ///< 是否参与当前帧可见性处理。
    bool castsShadow = true; ///< 是否投射阴影。
    bool receivesShadow = true; ///< 是否接收阴影。
};

/// 场景环境参数，供 sky、IBL、曝光和后处理使用。
struct SceneEnvironmentDesc {
    glm::vec3 ambientColor{0.03F}; ///< 简单环境光颜色。
    float exposure = 1.0F; ///< 曝光倍率。
    TextureViewHandle skyTexture{}; ///< 可选天空贴图。
    TextureViewHandle irradianceTexture{}; ///< 可选漫反射 IBL。
    TextureViewHandle prefilteredReflectionTexture{}; ///< 可选预滤波反射 IBL。
    TextureViewHandle brdfLut{}; ///< 可选 BRDF LUT。
};

/// 当前帧参与渲染的高层场景数据。
struct RenderSceneDesc {
    CameraData camera{}; ///< 主相机。
    SceneEnvironmentDesc environment{}; ///< 场景环境和后处理基础参数。
    std::vector<LightData> lights; ///< 光源列表。
    std::vector<RenderObjectDesc> objects; ///< 可渲染物体列表。
};

/// RenderGraph 中声明的资源类型。
enum class RenderGraphResourceType : std::uint8_t {
    Buffer,
    Texture,
    SwapchainImage
};

/// RenderGraph 资源标志，用于别名、导入导出和临时资源优化。
enum class RenderGraphResourceFlags : std::uint32_t {
    None = 0,
    Imported = 1u << 0,
    Exported = 1u << 1,
    Transient = 1u << 2,
    AllowAliasing = 1u << 3,
    NeverCull = 1u << 4
};

[[nodiscard]] constexpr RenderGraphResourceFlags operator|(RenderGraphResourceFlags lhs, RenderGraphResourceFlags rhs) noexcept {
    return detail::bitOr(lhs, rhs);
}

[[nodiscard]] constexpr RenderGraphResourceFlags operator&(RenderGraphResourceFlags lhs, RenderGraphResourceFlags rhs) noexcept {
    return detail::bitAnd(lhs, rhs);
}

constexpr RenderGraphResourceFlags& operator|=(RenderGraphResourceFlags& lhs, RenderGraphResourceFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// pass 对某个 graph 资源的读写引用。
struct RenderGraphResourceRef {
    std::string name; ///< graph 资源名，必须能在 RenderGraphDesc 的资源列表中找到。
    RenderGraphResourceType type = RenderGraphResourceType::Texture; ///< 被引用资源类型。
    ResourceState state = ResourceState::ShaderRead; ///< pass 访问该资源时需要的状态。
    PipelineStage stages = PipelineStage::AllCommands; ///< 访问发生的管线阶段。
    AccessFlags access = AccessFlags::None; ///< 访问类型；None 表示由 state 推导。
};

/// RenderGraph 内部或外部导入的 buffer 声明。
struct RenderGraphBufferDesc {
    std::string name; ///< graph 内唯一名称。
    BufferDesc desc{}; ///< graph 创建内部 buffer 时使用的描述。
    RenderGraphResourceFlags flags = RenderGraphResourceFlags::None; ///< graph 资源标志。
    bool imported = false; ///< true 表示资源由外部创建，graph 不负责生命周期。
    BufferHandle externalHandle{}; ///< imported 为 true 时使用的外部资源句柄。
};

/// RenderGraph 内部或外部导入的 texture 声明。
struct RenderGraphTextureDesc {
    std::string name; ///< graph 内唯一名称。
    TextureDesc desc{}; ///< graph 创建内部 texture 时使用的描述。
    RenderGraphResourceFlags flags = RenderGraphResourceFlags::None; ///< graph 资源标志。
    bool imported = false; ///< true 表示资源由外部创建，例如 swapchain image。
    TextureHandle externalHandle{}; ///< imported 为 true 时使用的外部资源句柄。
};

/// RenderGraph pass 使用的 attachment。
struct RenderGraphAttachmentDesc {
    std::string resourceName; ///< attachment 对应的 graph texture 名称。
    TextureAspect aspect = TextureAspect::Color; ///< attachment 使用的 aspect。
    std::uint32_t mipLevel = 0; ///< attachment 使用的 mip。
    std::uint32_t arrayLayer = 0; ///< attachment 使用的数组层。
    LoadOp loadOp = LoadOp::Clear; ///< pass 开始行为。
    StoreOp storeOp = StoreOp::Store; ///< pass 结束行为。
    ClearValue clearValue{}; ///< 清屏值。
};

/// RenderGraph pass 类型，调度器可据此选择命令队列和合法命令集合。
enum class RenderGraphPassType : std::uint8_t {
    Raster,
    Compute,
    Copy,
    Present
};

/// RenderGraph 中的一个 pass 声明，只描述依赖和附件，不直接包含 draw call。
struct RenderGraphPassDesc {
    std::string name; ///< pass 名称，需和 RenderPassWorkload::passName 对应。
    RenderGraphPassType type = RenderGraphPassType::Raster; ///< pass 类型。
    QueueType queue = QueueType::Graphics; ///< pass 希望运行在哪类队列。
    std::vector<RenderGraphResourceRef> reads; ///< pass 读取的资源及访问状态。
    std::vector<RenderGraphResourceRef> writes; ///< pass 写入的资源及访问状态。
    std::vector<RenderGraphAttachmentDesc> colorAttachments; ///< color attachments。
    std::optional<RenderGraphAttachmentDesc> depthStencilAttachment; ///< 可选 depth-stencil attachment。
    bool allowAsyncCompute = false; ///< compute pass 是否允许异步调度，后端需验证队列和同步支持。
    bool cullable = true; ///< 如果 pass 结果未被使用，RenderGraph 是否允许裁剪该 pass。
    bool hasSideEffect = false; ///< true 表示 pass 有外部副作用，即使输出未被读取也不能裁剪。
};

/// 整帧 RenderGraph 描述。调度器可据此排序 pass、分配临时资源并生成 barrier。
struct RenderGraphDesc {
    std::vector<RenderGraphBufferDesc> buffers; ///< graph 管理或导入的 buffers。
    std::vector<RenderGraphTextureDesc> textures; ///< graph 管理或导入的 textures。
    std::vector<RenderGraphPassDesc> passes; ///< pass 列表，顺序可作为初始拓扑顺序。
};

/// 当前帧的全局渲染设置。
struct FrameRenderSettings {
    Extent2D drawableSize{}; ///< 当前可绘制区域尺寸。
    Viewport viewport{}; ///< 默认 viewport。
    Rect2D scissor{}; ///< 默认 scissor。
    std::uint64_t frameIndex = 0; ///< 递增帧号，可用于环形 buffer 和 temporal 资源。
    float deltaTimeSeconds = 0.0F; ///< 与上一帧的时间差。
    std::uint32_t maxFramesInFlight = 2; ///< CPU/GPU 并行帧数。
    bool enableVsync = true; ///< 是否希望开启垂直同步。
    bool enableHdr = false; ///< 是否希望使用 HDR swapchain/中间颜色格式。
};

/// 提交给渲染后端的一帧完整数据包。
struct FramePacket {
    FrameRenderSettings settings{}; ///< 当前帧设置。
    SwapchainDesc swapchain{}; ///< 当前帧目标 swapchain 需求。
    UploadBatchDesc uploads{}; ///< 本帧开始前需要执行的资源上传。
    RenderSceneDesc scene{}; ///< 高层场景数据，可用于自动生成 draw 列表。
    RenderGraphDesc graph{}; ///< pass 和资源依赖图。
    std::vector<RenderPassWorkload> workloads; ///< 每个 pass 的具体 draw/dispatch 命令。
    std::vector<QueueSubmitDesc> submissions; ///< 队列提交计划；为空时后端可按 graph 自动生成。
    std::optional<PresentDesc> present; ///< 可选呈现请求，离屏渲染帧可以为空。
};

/// 后端和物理设备能力，用于初始化时选择渲染路径和做功能降级。
struct RenderCapabilities {
    GraphicsApi api = GraphicsApi::Unknown; ///< 当前后端 API。
    std::string adapterName; ///< GPU/适配器名称。
    std::uint64_t dedicatedVideoMemory = 0; ///< 独立显存字节数；集成显卡可为 0 或估算值。
    std::uint64_t sharedSystemMemory = 0; ///< 可共享系统内存字节数。
    RenderFeature features = RenderFeature::None; ///< 实际启用的功能位。
    std::uint32_t maxTexture2DSize = 0; ///< 支持的最大 2D texture 边长。
    std::uint32_t maxTexture3DSize = 0; ///< 支持的最大 3D texture 边长。
    std::uint32_t maxTextureCubeSize = 0; ///< 支持的最大 cube texture 边长。
    std::uint32_t maxTextureArrayLayers = 0; ///< 最大 texture array 层数。
    std::uint32_t maxColorAttachments = 0; ///< 单个 pass 最大 color attachment 数。
    std::uint32_t maxBindGroups = 0; ///< 单个 pipeline 最大 bind group 数。
    std::uint32_t maxBindingsPerGroup = 0; ///< 单个 bind group 最大 binding 数。
    std::uint32_t maxVertexBuffers = 0; ///< 单次绘制最大 vertex buffer binding 数。
    std::uint32_t maxVertexAttributes = 0; ///< 单个 pipeline 最大顶点属性数。
    std::uint32_t maxPushConstantSize = 0; ///< push constant/root constant 最大字节数。
    std::uint64_t minUniformBufferOffsetAlignment = 0; ///< 动态 uniform buffer offset 对齐。
    std::uint64_t minStorageBufferOffsetAlignment = 0; ///< 动态 storage buffer offset 对齐。
    std::uint64_t optimalBufferCopyOffsetAlignment = 0; ///< buffer copy offset 推荐对齐。
    std::uint64_t optimalBufferCopyRowPitchAlignment = 0; ///< buffer-texture copy 每行 pitch 推荐对齐。
    SampleCount maxSampleCount = SampleCount::Count1; ///< 支持的最大 MSAA 采样数。
    float maxSamplerAnisotropy = 1.0F; ///< 最大各向异性等级。
    bool supportsCompute = false; ///< 是否支持 compute shader。
    bool supportsGeometryShader = false; ///< 是否支持 geometry shader。
    bool supportsTessellation = false; ///< 是否支持曲面细分。
    bool supportsMeshShader = false; ///< 是否支持 mesh/task shader。
    bool supportsRayTracing = false; ///< 是否支持硬件光追接口。
    bool supportsBindless = false; ///< 是否支持大规模无绑定/动态索引资源绑定。
    bool supportsSamplerAnisotropy = false; ///< 是否支持各向异性采样。
    bool supportsSamplerCompare = false; ///< 是否支持比较采样器。
    bool supportsTimestampQuery = false; ///< 是否支持 timestamp query。
    bool supportsOcclusionQuery = false; ///< 是否支持 occlusion query。
    bool supportsPipelineStatisticsQuery = false; ///< 是否支持 pipeline statistics query。
    bool supportsIndirectDraw = false; ///< 是否支持 indirect draw。
    bool supportsDrawIndirectCount = false; ///< 是否支持 GPU count buffer 控制 indirect draw 数量。
    bool supportsDynamicRendering = false; ///< 是否支持无传统 render pass 的动态渲染。
    bool supportsDebugMarkers = false; ///< 是否支持 GPU 调试标记。
    bool supportsTextureCompressionBC = false; ///< 是否支持 BC 压缩格式。
    bool supportsTextureCompressionETC2 = false; ///< 是否支持 ETC2 压缩格式。
    bool supportsTextureCompressionASTC = false; ///< 是否支持 ASTC 压缩格式。
};

} // namespace Engine::Render
