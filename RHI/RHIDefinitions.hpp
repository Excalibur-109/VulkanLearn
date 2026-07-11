#pragma once

// Standalone RHI definitions. No legacy Render dependency.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#include <glm/glm.hpp>

/// 固定宽度无符号 8 位整数。
namespace rhi {

using u8 = std::uint8_t;

/// 固定宽度无符号 16 位整数。
using u16 = std::uint16_t;

/// 固定宽度无符号 32 位整数。
using u32 = std::uint32_t;

/// 固定宽度无符号 64 位整数。
using u64 = std::uint64_t;

/// 固定宽度有符号 32 位整数。
using i32 = std::int32_t;

/**
 * @file RHIDefinitions.hpp
 * @brief 跨图形 API 的渲染描述层。
 *
 * 这个文件只定义“渲染引擎准备阶段”需要的数据结构，不包含任何 Vulkan、D3D、Metal
 * 或 OpenGL 的原生类型。后端实现应该读取这些通用结构，再转换成具体 API 对象，例如：
 *
 * - RHIBufferDesc -> VkBuffer / ID3D12Resource / MTLBuffer
 * - RHITextureDesc -> VkImage / ID3D12Resource(Texture) / MTLTexture
 * - RHIGraphicsPipelineDesc -> VkPipeline / D3D12 PSO / MTLRenderPipelineState
 * - RHIRenderGraphDesc -> 后端命令录制顺序、资源状态转换和同步 barrier
 *
 * 设计目标：
 * - 上层渲染逻辑只依赖这些结构，不关心底层 API。
 * - 每个结构都尽量表达“意图”，而不是表达某个 API 的创建参数。
 * - 后端可以根据 RHICapabilities 降级或选择不同实现路径。
 */
/// 无效数组下标，常用于 optional index 或查找失败的返回值。
inline constexpr u32 RHI_INVALID_INDEX = std::numeric_limits<u32>::max();

/// 0 被保留为无效渲染资源句柄，真实后端资源句柄从非 0 值开始分配。
inline constexpr u64 RHI_INVALID_HANDLE_VALUE = 0;

/// 表示 buffer binding 或 barrier 覆盖资源剩余全部范围。
inline constexpr u64 RHI_WHOLE_SIZE = std::numeric_limits<u64>::max();

/**
 * @brief 类型安全的轻量资源句柄。
 *
 * RHIHandle 只保存引擎自己的逻辑 id，不直接保存 VkBuffer、ID3D12Resource* 等后端对象。
 * 后端资源管理器可以用这个 id 去索引真正的 API 资源。不同 Tag 让 RHIBuffer 和
 * RHITexture 不能互相误传。
 */
template <typename Tag>
struct RHIHandle {
    /// 资源管理器分配的逻辑 id；0 表示无效。
    u64 value = RHI_INVALID_HANDLE_VALUE;

    constexpr RHIHandle() noexcept = default;

    /// 显式从资源 id 构造句柄，避免整数被意外隐式转换成资源句柄。
    explicit constexpr RHIHandle(u64 handleValue) noexcept
        : value(handleValue) {
    }

    /// 判断句柄是否指向一个已分配的逻辑资源。
    [[nodiscard]] constexpr bool isValid() const noexcept {
        return value != RHI_INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return isValid();
    }

    friend constexpr bool operator==(RHIHandle lhs, RHIHandle rhs) noexcept {
        return lhs.value == rhs.value;
    }

    friend constexpr bool operator!=(RHIHandle lhs, RHIHandle rhs) noexcept {
        return !(lhs == rhs);
    }
};

// 这些空 tag 只用于让 RHIHandle<T> 变成不同的 C++ 类型，不参与运行期逻辑。
struct RHIBufferTag {};
struct RHITextureTag {};
struct RHITextureViewTag {};
struct RHISamplerTag {};
struct RHIShaderTag {};
struct RHIPipelineLayoutTag {};
struct RHIPipelineTag {};
struct RHIRenderPassTag {};
struct RHIFramebufferTag {};
struct RHISwapchainTag {};
struct RHIBindGroupLayoutTag {};
struct RHIBindGroupTag {};
struct RHIQueryPoolTag {};
struct RHIPipelineCacheTag {};
struct RHIGPUWaitGPUSignalTag {};
struct RHICPUWaitGPUSignalTag {};
struct RHIMeshTag {};
struct RHIMaterialTag {};

/// GPU buffer 资源，例如顶点、索引、uniform、storage、上传 staging buffer。
using RHIBuffer = RHIHandle<RHIBufferTag>;

/// GPU texture/image 资源，例如贴图、渲染目标、深度缓冲、swapchain image。
using RHITexture = RHIHandle<RHITextureTag>;

/// texture 的视图，描述 mip/layer/维度/格式重解释。
using RHITextureView = RHIHandle<RHITextureViewTag>;

/// 采样器状态对象，描述过滤、寻址、各向异性和比较采样。
using RHISampler = RHIHandle<RHISamplerTag>;

/// 着色器对象或着色器模块。
using RHIShader = RHIHandle<RHIShaderTag>;

/// 资源绑定布局集合，对应 Vulkan pipeline layout / D3D root signature / Metal argument layout。
using RHIPipelineLayout = RHIHandle<RHIPipelineLayoutTag>;

/// 图形或计算管线对象。
using RHIPipeline = RHIHandle<RHIPipelineTag>;

/// 渲染通道对象；在现代后端中也可以只是动态渲染/附件配置的缓存 key。
using RHIRenderPass = RHIHandle<RHIRenderPassTag>;

/// framebuffer 或一组绑定到 render pass 的附件视图。
using RHIFramebuffer = RHIHandle<RHIFramebufferTag>;

/// swapchain 对象句柄，表示窗口后备缓冲队列。
using RHISwapchain = RHIHandle<RHISwapchainTag>;

/// 一组资源槽位的布局，对应 Vulkan descriptor set layout / D3D descriptor table。
using RHIBindGroupLayout = RHIHandle<RHIBindGroupLayoutTag>;

/// 一组实际绑定资源，对应 Vulkan descriptor set / D3D descriptor heap 区间 / Metal argument buffer。
using RHIBindGroup = RHIHandle<RHIBindGroupTag>;

/// GPU 查询池句柄，例如 timestamp、occlusion、pipeline statistics。
using RHIQueryPool = RHIHandle<RHIQueryPoolTag>;

/// 管线缓存句柄，用于复用后端 pipeline 编译结果。
using RHIPipelineCache = RHIHandle<RHIPipelineCacheTag>;

/// GPU 等待 GPU 的同步信号；Vulkan 映射为 binary/timeline semaphore。
using RHIGPUWaitGPUSignal = RHIHandle<RHIGPUWaitGPUSignalTag>;

/// CPU 等待 GPU 完成的同步信号；Vulkan 映射为 fence。
using RHICPUWaitGPUSignal = RHIHandle<RHICPUWaitGPUSignalTag>;

/// 引擎层 mesh 资源句柄。
using RHIMesh = RHIHandle<RHIMeshTag>;

/// 引擎层 material 资源句柄。
using RHIMaterial = RHIHandle<RHIMaterialTag>;

template <typename Enum>
[[nodiscard]] constexpr auto RHIEnumToUnderlying(Enum value) noexcept {
    return static_cast<std::underlying_type_t<Enum>>(value);
}

template <typename Enum>
[[nodiscard]] constexpr Enum RHIEnumBitOr(Enum lhs, Enum rhs) noexcept {
    return static_cast<Enum>(RHIEnumToUnderlying(lhs) | RHIEnumToUnderlying(rhs));
}

template <typename Enum>
[[nodiscard]] constexpr Enum RHIEnumBitAnd(Enum lhs, Enum rhs) noexcept {
    return static_cast<Enum>(RHIEnumToUnderlying(lhs) & RHIEnumToUnderlying(rhs));
}

template <typename Enum>
[[nodiscard]] constexpr bool RHIHasAny(Enum value, Enum flags) noexcept {
    return (RHIEnumToUnderlying(value) & RHIEnumToUnderlying(flags)) != 0;
}

template <typename Enum>
[[nodiscard]] constexpr bool RHIHasAll(Enum value, Enum flags) noexcept {
    return (RHIEnumToUnderlying(value) & RHIEnumToUnderlying(flags)) == RHIEnumToUnderlying(flags);
}

/// 当前后端使用的图形 API，用于能力查询、日志和后端分支。
enum class RHIGraphicsAPI : u8 {
    Unknown, ///< 未指定或无法识别的图形 API；用于默认值、错误回退和日志占位。
    Vulkan, ///< 使用 Vulkan 后端，适合显式资源状态、跨平台桌面/移动渲染路径。
    Direct3D11, ///< 使用 Direct3D 11 后端，面向 Windows 兼容路径和较传统的隐式状态模型。
    Direct3D12, ///< 使用 Direct3D 12 后端，面向 Windows 显式同步、资源状态和现代 GPU 功能。
    Metal, ///< 使用 Apple Metal 后端，面向 macOS/iOS 平台渲染。
    OpenGL, ///< 使用 OpenGL 后端，主要用于兼容旧平台或调试路径。
    WebGPU ///< 使用 WebGPU 后端，面向浏览器或 WebGPU 原生实现。
};

/// GPU 队列类型。不是所有 API 都公开独立队列，后端可以把多个类型映射到同一个实际队列。
enum class RHIQueueType : u8 {
    Graphics, ///< 图形队列，可执行 draw、render pass，也通常兼容 copy/compute。
    Compute, ///< 计算队列，可执行 compute dispatch，支持异步计算时可与图形队列并行。
    Transfer, ///< 传输队列，专用于 buffer/texture copy、upload、blit 等数据搬运。
    Present ///< 呈现队列，用于把 swapchain image 提交给窗口系统显示。
};

/// GPU 选择偏好。移动端/笔记本上可用于选择省电或高性能适配器。
enum class RHIPowerPreference : u8 {
    Default, ///< 不强制选择功耗档位，由后端或操作系统按默认策略选择适配器。
    LowPower, ///< 偏向低功耗适配器，常用于集显、移动端或省电模式。
    HighPerformance ///< 偏向高性能适配器，常用于独显和需要最大渲染吞吐的场景。
};

/// 后端验证层开关级别。
enum class RHIValidationMode : u8 {
    Disabled, ///< 关闭验证层/调试层，适合发布版本或性能测试。
    Enabled, ///< 启用常规 API 验证和调试消息，适合开发期捕获资源和同步错误。
    GpuAssisted ///< 启用 GPU 辅助验证，额外检查 shader 侧越界等问题，开销更高。
};

/// 引擎希望启用的渲染特性位。后端初始化时可根据设备能力做裁剪或报错。
enum class RHIRenderFeature : u64 {
    None = 0, ///< 不请求任何额外功能。
    Compute = 1ull << 0, ///< 启用 compute shader 和 compute queue/pass。
    GeometryShader = 1ull << 1, ///< 启用 geometry shader 阶段。
    Tessellation = 1ull << 2, ///< 启用 tessellation control/evaluation shader 阶段。
    MeshShader = 1ull << 3, ///< 启用 task/mesh shader 现代几何管线。
    RayTracing = 1ull << 4, ///< 启用硬件光追相关资源和 shader 阶段。
    Bindless = 1ull << 5, ///< 启用大规模资源数组和 shader 动态索引。
    SamplerAnisotropy = 1ull << 6, ///< 启用各向异性纹理过滤。
    SamplerCompare = 1ull << 7, ///< 启用比较采样器，常用于 shadow map。
    TimestampQuery = 1ull << 8, ///< 启用 GPU timestamp 查询，用于性能计时。
    OcclusionQuery = 1ull << 9, ///< 启用遮挡查询，用于可见性判断。
    PipelineStatisticsQuery = 1ull << 10, ///< 启用管线统计查询，例如 shader 调用次数。
    IndirectDraw = 1ull << 11, ///< 启用 GPU 参数驱动的 indirect draw/dispatch。
    DrawIndirectCount = 1ull << 12, ///< 启用 GPU count buffer 控制 indirect draw 数量。
    DynamicRendering = 1ull << 13, ///< 启用无传统 render pass/framebuffer 的动态渲染路径。
    ConservativeRasterization = 1ull << 14, ///< 启用保守光栅化，常用于遮挡或体素化。
    TextureCompressionBC = 1ull << 15, ///< 启用 BC/DXT 系列压缩纹理格式。
    TextureCompressionETC2 = 1ull << 16, ///< 启用 ETC2 压缩纹理格式，移动端常见。
    TextureCompressionASTC = 1ull << 17, ///< 启用 ASTC 压缩纹理格式，移动端和现代 GPU 常见。
    Multiview = 1ull << 18, ///< 启用单次 pass 渲染多个 view，常用于 VR/立体渲染。
    DebugMarkers = 1ull << 19 ///< 启用 GPU 调试标记和对象命名。
};

[[nodiscard]] constexpr RHIRenderFeature operator|(RHIRenderFeature lhs, RHIRenderFeature rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHIRenderFeature operator&(RHIRenderFeature lhs, RHIRenderFeature rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHIRenderFeature& operator|=(RHIRenderFeature& lhs, RHIRenderFeature rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 渲染后端初始化参数。窗口原生句柄由平台层保存，这里只描述渲染器自己的需求。
struct RHIBackendDesc {
    std::string applicationName; ///< 应用名称，用于后端实例创建和调试器显示。
    std::string engineName = "VulkanLearn"; ///< 引擎名称，用于后端实例创建和调试器显示。
    RHIGraphicsAPI preferredApi = RHIGraphicsAPI::Vulkan; ///< 优先使用的图形 API。
    RHIPowerPreference powerPreference = RHIPowerPreference::HighPerformance; ///< GPU 选择偏好。
    RHIValidationMode validation = RHIValidationMode::Enabled; ///< 是否启用验证层/调试层。
    RHIRenderFeature requiredFeatures = RHIRenderFeature::None; ///< 必须支持的功能，不支持时初始化应失败。
    RHIRenderFeature optionalFeatures = RHIRenderFeature::DebugMarkers | RHIRenderFeature::TimestampQuery; ///< 可选功能，后端尽量开启。
    u32 framesInFlight = 2; ///< CPU/GPU 并行帧数。
    bool enableGpuCrashDumps = false; ///< 是否启用 GPU 崩溃转储，具体支持由后端决定。
    bool enablePipelineCache = true; ///< 是否启用管线缓存。
};

/// 队列族/队列能力描述，供后端选择 graphics/compute/transfer/present 队列。
struct RHIQueueDesc {
    RHIQueueType type = RHIQueueType::Graphics; ///< 队列类型。
    u32 count = 1; ///< 希望创建的队列数量。
    float priority = 1.0F; ///< 队列优先级，范围通常是 0 到 1。
};

/// 物理适配器摘要。初始化 UI 或日志可以直接展示这些信息。
struct RHIAdapterDesc {
    std::string name; ///< GPU/适配器名称。
    RHIGraphicsAPI api = RHIGraphicsAPI::Unknown; ///< 该适配器所属后端 API。
    u64 dedicatedVideoMemory = 0; ///< 独立显存字节数。
    u64 sharedSystemMemory = 0; ///< 可共享系统内存字节数。
    bool isIntegrated = false; ///< 是否集成显卡。
    bool isSoftware = false; ///< 是否软件适配器。
};

/**
 * @brief 引擎统一像素/顶点数据格式。
 *
 * RHIFormat 需要覆盖“资源创建”和“管线附件格式”两类使用场景。后端转换时要注意：
 * - sRGB 格式只应该用于颜色贴图或 swapchain，不适合作为普通线性数据。
 * - Depth/Stencil 格式不能当普通 color attachment。
 * - 压缩格式 BC* 不是所有平台都支持，需要根据 RHICapabilities 或后端能力降级。
 */
enum class RHIFormat : u16 {
    Undefined, ///< 未指定格式；用于默认值、自动推导或无附件。

    R8_UNorm, ///< 单通道 8 位无符号归一化，shader 读取为 0..1；常用于灰度/遮罩。
    R8_SNorm, ///< 单通道 8 位有符号归一化，shader 读取为 -1..1；常用于方向/偏移数据。
    R8_UInt, ///< 单通道 8 位无符号整数，shader 读取为 uint；常用于 ID/索引。
    R8_SInt, ///< 单通道 8 位有符号整数，shader 读取为 int。
    RG8_UNorm, ///< 双通道 8 位无符号归一化；常用于 RG mask、压缩法线的 XY。
    RG8_SNorm, ///< 双通道 8 位有符号归一化；常用于单位向量 XY。
    RG8_UInt, ///< 双通道 8 位无符号整数。
    RG8_SInt, ///< 双通道 8 位有符号整数。
    RGBA8_UNorm, ///< 四通道 8 位无符号归一化；常用 LDR 颜色/普通贴图格式。
    RGBA8_SNorm, ///< 四通道 8 位有符号归一化；可用于需要 -1..1 的向量数据。
    RGBA8_UInt, ///< 四通道 8 位无符号整数；常用于整数属性或 ID buffer。
    RGBA8_SInt, ///< 四通道 8 位有符号整数。
    RGBA8_SRGB, ///< 四通道 8 位 sRGB 颜色；采样时转换到线性空间。
    BGRA8_UNorm, ///< BGRA 排列 8 位无符号归一化；Windows swapchain 常见。
    BGRA8_SRGB, ///< BGRA 排列 8 位 sRGB；Windows sRGB swapchain 常见。

    R16_UNorm, ///< 单通道 16 位无符号归一化；比 R8 更高精度的 mask/高度图。
    R16_SNorm, ///< 单通道 16 位有符号归一化；高精度 -1..1 数据。
    R16_UInt, ///< 单通道 16 位无符号整数。
    R16_SInt, ///< 单通道 16 位有符号整数。
    R16_Float, ///< 单通道 16 位浮点；常用于半精度高度/亮度/中间结果。
    RG16_UNorm, ///< 双通道 16 位无符号归一化。
    RG16_SNorm, ///< 双通道 16 位有符号归一化。
    RG16_UInt, ///< 双通道 16 位无符号整数。
    RG16_SInt, ///< 双通道 16 位有符号整数。
    RG16_Float, ///< 双通道 16 位浮点；常用于半精度向量数据。
    RGBA16_UNorm, ///< 四通道 16 位无符号归一化；高精度颜色但非 HDR 浮点。
    RGBA16_SNorm, ///< 四通道 16 位有符号归一化。
    RGBA16_UInt, ///< 四通道 16 位无符号整数。
    RGBA16_SInt, ///< 四通道 16 位有符号整数。
    RGBA16_Float, ///< 四通道 16 位浮点；常用 HDR 渲染目标格式。

    R32_UInt, ///< 单通道 32 位无符号整数；常用于大 ID、计数器、索引图。
    R32_SInt, ///< 单通道 32 位有符号整数。
    R32_Float, ///< 单通道 32 位浮点；高精度标量、深度拷贝或计算结果。
    RG32_UInt, ///< 双通道 32 位无符号整数。
    RG32_SInt, ///< 双通道 32 位有符号整数。
    RG32_Float, ///< 双通道 32 位浮点。
    RGB32_UInt, ///< 三通道 32 位无符号整数；支持度可能弱于 RGBA32。
    RGB32_SInt, ///< 三通道 32 位有符号整数；支持度可能弱于 RGBA32。
    RGB32_Float, ///< 三通道 32 位浮点；支持度可能弱于 RGBA32。
    RGBA32_UInt, ///< 四通道 32 位无符号整数。
    RGBA32_SInt, ///< 四通道 32 位有符号整数。
    RGBA32_Float, ///< 四通道 32 位浮点；高精度 HDR/计算格式，带宽成本高。

    RGB10A2_UNorm, ///< RGB 各 10 位、A 2 位无符号归一化；常用于 HDR-ish swapchain/颜色。
    R11G11B10_Float, ///< 11/11/10 位无符号浮点 RGB；常用于紧凑 HDR 渲染目标，无 alpha。

    D16_UNorm, ///< 16 位无符号归一化深度格式。
    D24_UNorm, ///< 24 位无符号归一化深度格式；平台支持差异较大。
    S8_UInt, ///< 8 位 stencil-only 格式。
    D24_UNorm_S8_UInt, ///< 24 位深度 + 8 位 stencil 的 depth-stencil 格式。
    D32_Float, ///< 32 位浮点深度格式；常用高精度 depth attachment。
    D32_Float_S8_UInt, ///< 32 位浮点深度 + 8 位 stencil；内存成本较高。

    BC1RGBA_UNorm, ///< BC1/DXT1 压缩 RGBA 线性格式；适合低 alpha 需求贴图。
    BC1RGBA_SRGB, ///< BC1/DXT1 压缩 sRGB 颜色格式。
    BC3RGBA_UNorm, ///< BC3/DXT5 压缩 RGBA 线性格式；适合带 alpha 贴图。
    BC3RGBA_SRGB, ///< BC3/DXT5 压缩 sRGB 颜色格式。
    BC5RG_UNorm, ///< BC5 双通道无符号压缩；常用于法线/向量 XY 数据。
    BC5RG_SNorm, ///< BC5 双通道有符号压缩；常用于 signed normal XY。
    BC7RGBA_UNorm, ///< BC7 高质量 RGBA 压缩线性格式；桌面 GPU 常用。
    BC7RGBA_SRGB, ///< BC7 高质量 sRGB 颜色压缩格式；桌面 GPU 常用。

    ETC2RGB8_UNorm, ///< ETC2 RGB 8 位压缩线性格式；移动端常见。
    ETC2RGB8_SRGB, ///< ETC2 RGB 8 位压缩 sRGB 格式；移动端常见。
    ETC2RGBA8_UNorm, ///< ETC2 RGBA 8 位压缩线性格式。
    ETC2RGBA8_SRGB, ///< ETC2 RGBA 8 位压缩 sRGB 格式。
    ASTC4x4_UNorm, ///< ASTC 4x4 高质量压缩线性格式；质量高、体积相对大。
    ASTC4x4_SRGB, ///< ASTC 4x4 高质量压缩 sRGB 格式。
    ASTC8x8_UNorm, ///< ASTC 8x8 高压缩率线性格式；质量低于 4x4。
    ASTC8x8_SRGB ///< ASTC 8x8 高压缩率 sRGB 格式。
};

/// 判断格式是否包含深度分量，用于选择 depth attachment 或 depth texture 采样路径。
[[nodiscard]] constexpr bool isDepthFormat(RHIFormat format) noexcept {
    return format == RHIFormat::D16_UNorm ||
           format == RHIFormat::D24_UNorm ||
           format == RHIFormat::D24_UNorm_S8_UInt ||
           format == RHIFormat::D32_Float ||
           format == RHIFormat::D32_Float_S8_UInt;
}

/// 判断格式是否包含 stencil 分量，用于模板测试和 depth-stencil attachment 创建。
[[nodiscard]] constexpr bool hasStencilFormat(RHIFormat format) noexcept {
    return format == RHIFormat::S8_UInt ||
           format == RHIFormat::D24_UNorm_S8_UInt ||
           format == RHIFormat::D32_Float_S8_UInt;
}

/// 多重采样数量，对应 Vulkan sample count / D3D sample count / Metal sampleCount。
enum class RHISampleCount : u8 {
    Count1 = 1, ///< 单采样，无 MSAA；普通纹理、swapchain 和大多数后处理目标使用。
    Count2 = 2, ///< 2x MSAA，较低成本的边缘抗锯齿采样数。
    Count4 = 4, ///< 4x MSAA，质量和成本较常用的平衡点。
    Count8 = 8, ///< 8x MSAA，更高质量抗锯齿，带宽和存储成本明显增加。
    Count16 = 16, ///< 16x MSAA，高端或特殊离屏渲染使用，设备支持度有限。
    Count32 = 32, ///< 32x MSAA，少数设备支持，通常只用于能力完整性表达。
    Count64 = 64 ///< 64x MSAA，极少实际使用，主要映射底层 API 的完整 sample count。
};

/**
 * @brief swapchain 呈现模式。
 *
 * Fifo 是最通用的垂直同步模式；Immediate/Mailbox 可能不被所有平台支持。
 */
enum class RHIPresentMode : u8 {
    Immediate, ///< 立即呈现，不等待垂直同步；延迟低但可能产生 tearing。
    Mailbox, ///< 三缓冲低延迟垂直同步模式，新帧替换等待队列中的旧帧。
    FIFO, ///< 标准垂直同步队列模式，所有平台通常都支持，避免 tearing。
    FIFORelaxed ///< FIFO 的宽松模式；错过垂直同步时可立即显示以降低卡顿。
};

/**
 * @brief 资源在 GPU 管线中的逻辑状态。
 *
 * 显式 API（Vulkan/D3D12）需要把这些状态翻译成 image layout/resource state/barrier。
 * 隐式 API（OpenGL/D3D11）可以把它用于调试验证或减少不必要的绑定错误。
 */
enum class RHIResourceState : u16 {
    Undefined, ///< 内容或状态未定义；资源首次使用、丢弃旧内容或无需保留数据时使用。
    Common, ///< 通用状态；适合作为跨队列/跨 API 的默认可转换状态。
    CopySource, ///< 作为拷贝源读取，用于 buffer/texture copy、readback 或 blit source。
    CopyDestination, ///< 作为拷贝目标写入，用于上传、清理、resolve/blit destination。
    VertexBuffer, ///< 作为顶点缓冲读取，供输入装配阶段获取 vertex attribute。
    IndexBuffer, ///< 作为索引缓冲读取，供 indexed draw 获取索引数据。
    ConstantBuffer, ///< 作为常量/uniform buffer 读取，供 shader 访问只读小块参数。
    ShaderRead, ///< 作为 shader 只读资源访问，例如 sampled texture、SRV 或只读 storage buffer。
    ShaderWrite, ///< 作为 shader 可写资源访问，例如 UAV、storage texture 或 writable storage buffer。
    RenderTarget, ///< 作为 color attachment/render target 写入，也可能支持同 pass 内读取。
    DepthRead, ///< 作为只读 depth-stencil attachment 或 depth texture 读取。
    DepthWrite, ///< 作为可写 depth-stencil attachment 参与深度/模板测试。
    ResolveSource, ///< 作为 MSAA resolve 源资源读取。
    ResolveDestination, ///< 作为 MSAA resolve 目标资源写入。
    Present, ///< 作为 swapchain image 等待窗口系统呈现。
    IndirectArgument, ///< 作为 indirect draw/dispatch 参数缓冲由命令处理器读取。
    AccelerationStructureRead, ///< 作为光追加速结构只读访问，用于 ray tracing 查询。
    AccelerationStructureWrite, ///< 作为光追加速结构构建或更新目标写入。
    ShadingRateTexture ///< 作为可变速率着色/VRS 图像读取，控制屏幕区域 shading rate。
};

/// GPU 管线阶段位。需要精确同步时可配合 RHIAccessFlags 构造 barrier。
enum class RHIPipelineStage : u64 {
    None = 0, ///< 不指定任何管线阶段；常用于空 barrier 或由状态自动推导。
    TopOfPipe = 1ull << 0, ///< 管线最开始的同步点，用于等待命令进入 GPU 执行流。
    DrawIndirect = 1ull << 1, ///< 读取 indirect draw/dispatch 参数的阶段。
    VertexInput = 1ull << 2, ///< 读取 vertex/index buffer 并装配图元的阶段。
    VertexShader = 1ull << 3, ///< 执行 vertex shader 的阶段。
    TessControlShader = 1ull << 4, ///< 执行 tessellation control/hull shader 的阶段。
    TessEvaluationShader = 1ull << 5, ///< 执行 tessellation evaluation/domain shader 的阶段。
    GeometryShader = 1ull << 6, ///< 执行 geometry shader 的阶段。
    FragmentShader = 1ull << 7, ///< 执行 fragment/pixel shader 的阶段。
    EarlyFragmentTests = 1ull << 8, ///< 早期深度/模板测试阶段，可在片元着色前发生。
    LateFragmentTests = 1ull << 9, ///< 后期深度/模板测试阶段，可在片元着色后发生。
    ColorAttachmentOutput = 1ull << 10, ///< color attachment blend、logic op 和写入阶段。
    ComputeShader = 1ull << 11, ///< 执行 compute shader dispatch 的阶段。
    Transfer = 1ull << 12, ///< 执行 copy、blit、clear、resolve 等传输命令的阶段。
    BottomOfPipe = 1ull << 13, ///< 管线末尾同步点，常用于 timestamp 或等待前序命令完成。
    Host = 1ull << 14, ///< CPU/主机访问阶段，用于 map、readback 或上传内存同步。
    RayTracingShader = 1ull << 15, ///< 执行 ray generation、hit、miss 等光追 shader 的阶段。
    AccelerationStructureBuild = 1ull << 16, ///< 构建或更新光追加速结构的阶段。
    TaskShader = 1ull << 17, ///< 执行 task/amplification shader 的阶段。
    MeshShader = 1ull << 18, ///< 执行 mesh shader 并生成图元的阶段。
    AllGraphics = 1ull << 19, ///< 覆盖所有图形管线阶段的聚合标志。
    AllCommands = 1ull << 20 ///< 覆盖队列中所有命令和阶段的保守同步标志。
};

[[nodiscard]] constexpr RHIPipelineStage operator|(RHIPipelineStage lhs, RHIPipelineStage rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHIPipelineStage operator&(RHIPipelineStage lhs, RHIPipelineStage rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHIPipelineStage& operator|=(RHIPipelineStage& lhs, RHIPipelineStage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// GPU 资源访问类型位。比 RHIResourceState 更接近后端 barrier 所需的 access mask。
enum class RHIAccessFlags : u64 {
    None = 0, ///< 不指定访问类型；常用于无内存依赖或由 RHIResourceState 自动推导。
    IndirectCommandRead = 1ull << 0, ///< GPU 读取 indirect draw/dispatch 参数。
    IndexRead = 1ull << 1, ///< 输入装配阶段读取 index buffer。
    VertexAttributeRead = 1ull << 2, ///< 输入装配阶段读取 vertex attribute 数据。
    UniformRead = 1ull << 3, ///< shader 读取 uniform/constant buffer。
    InputAttachmentRead = 1ull << 4, ///< fragment shader 读取 input attachment/subpass input。
    ShaderRead = 1ull << 5, ///< shader 读取 sampled texture、只读 buffer 或其他 SRV 资源。
    ShaderWrite = 1ull << 6, ///< shader 写入 storage buffer、storage texture 或 UAV 资源。
    ColorAttachmentRead = 1ull << 7, ///< color attachment 读取，常见于 blending 或 framebuffer fetch。
    ColorAttachmentWrite = 1ull << 8, ///< color attachment 写入渲染目标。
    DepthStencilRead = 1ull << 9, ///< depth/stencil attachment 或 depth texture 的只读访问。
    DepthStencilWrite = 1ull << 10, ///< depth/stencil attachment 的写入访问。
    TransferRead = 1ull << 11, ///< copy/blit/resolve/clear 等传输命令读取资源。
    TransferWrite = 1ull << 12, ///< copy/blit/resolve/clear 等传输命令写入资源。
    HostRead = 1ull << 13, ///< CPU 从映射内存或读回资源读取数据。
    HostWrite = 1ull << 14, ///< CPU 向映射内存或上传资源写入数据。
    MemoryRead = 1ull << 15, ///< 泛化内存读取，用于无法细分或需要保守同步的 barrier。
    MemoryWrite = 1ull << 16, ///< 泛化内存写入，用于无法细分或需要保守同步的 barrier。
    AccelerationStructureRead = 1ull << 17, ///< ray tracing 或构建过程读取加速结构。
    AccelerationStructureWrite = 1ull << 18 ///< 构建或更新过程写入加速结构。
};

[[nodiscard]] constexpr RHIAccessFlags operator|(RHIAccessFlags lhs, RHIAccessFlags rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHIAccessFlags operator&(RHIAccessFlags lhs, RHIAccessFlags rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHIAccessFlags& operator|=(RHIAccessFlags& lhs, RHIAccessFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 二维尺寸，常用于窗口、swapchain、viewport/scissor 和 2D 纹理。
struct RHIExtent2D {
    u32 width = 1;
    u32 height = 1;
};

/// 三维尺寸，2D 纹理时 depth 通常为 1，数组层数单独由 arrayLayers 表示。
struct RHIExtent3D {
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;
};

/// 二维整数偏移，主要用于 scissor、copy region 和贴图区域更新。
struct RHIOffset2D {
    i32 x = 0;
    i32 y = 0;
};

/// 三维整数偏移，主要用于 buffer-to-texture 上传和 3D texture copy。
struct RHIOffset3D {
    i32 x = 0;
    i32 y = 0;
    i32 z = 0;
};

/// 二维矩形区域。offset 是左上角，extent 是宽高。
struct RHIRect2D {
    RHIOffset2D offset{};
    RHIExtent2D extent{};
};

/// 浮点 viewport。深度范围一般是 [0, 1]，具体后端负责坐标系差异转换。
struct RHIViewport {
    float x = 0.0F;
    float y = 0.0F;
    float width = 1.0F;
    float height = 1.0F;
    float minDepth = 0.0F;
    float maxDepth = 1.0F;
};

/// color attachment 的清屏颜色，默认透明黑。
struct RHIClearColor {
    float r = 0.0F;
    float g = 0.0F;
    float b = 0.0F;
    float a = 1.0F;
};

/// depth-stencil attachment 的清屏值，默认 depth=1 表示远平面。
struct RHIClearDepthStencil {
    float depth = 1.0F;
    u32 stencil = 0;
};

/// 同时容纳颜色和深度模板清屏值，RenderGraph attachment 可按实际类型读取对应字段。
struct RHIClearValue {
    RHIClearColor color{};
    RHIClearDepthStencil depthStencil{};
};

/// texture 子资源 aspect 位，用于区分 color/depth/stencil/plane。
enum class RHITextureAspect : u32 {
    None = 0, ///< 不指定任何 aspect；用于空范围或由格式推导前的占位值。
    Color = 1u << 0, ///< color aspect，适用于普通颜色纹理和 color attachment。
    Depth = 1u << 1, ///< depth aspect，适用于深度纹理或 depth-stencil 格式中的深度平面。
    Stencil = 1u << 2, ///< stencil aspect，适用于模板纹理或 depth-stencil 格式中的模板平面。
    Plane0 = 1u << 3, ///< 多平面格式的第 0 平面，例如 YUV 图像的主亮度平面。
    Plane1 = 1u << 4, ///< 多平面格式的第 1 平面，例如 YUV 图像的色度平面。
    Plane2 = 1u << 5, ///< 多平面格式的第 2 平面，用于三平面 YUV 等格式。
    All = 0xFFFFFFFFu ///< 覆盖资源所有可用 aspect；后端可按实际格式展开。
};

[[nodiscard]] constexpr RHITextureAspect operator|(RHITextureAspect lhs, RHITextureAspect rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHITextureAspect operator&(RHITextureAspect lhs, RHITextureAspect rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHITextureAspect& operator|=(RHITextureAspect& lhs, RHITextureAspect rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// buffer 的用途位。创建 buffer 时必须声明后续会如何使用，显式 API 会用它设置 usage flags。
enum class RHIBufferUsage : u32 {
    None = 0, ///< 未声明用途；只适合默认值，真实 buffer 创建通常应指定至少一个用途。
    TransferSource = 1u << 0, ///< 可作为 copy/blit/readback 的源 buffer。
    TransferDestination = 1u << 1, ///< 可作为 upload、copy 或清零操作的目标 buffer。
    Vertex = 1u << 2, ///< 可绑定为 vertex buffer，供输入装配阶段读取顶点数据。
    Index = 1u << 3, ///< 可绑定为 index buffer，供 indexed draw 读取索引数据。
    Uniform = 1u << 4, ///< 可绑定为 uniform/constant buffer，供 shader 读取常量参数。
    Storage = 1u << 5, ///< 可绑定为 storage/UAV buffer，供 shader 读写结构化或原始数据。
    Indirect = 1u << 6, ///< 可作为 indirect draw/dispatch 参数 buffer 由 GPU 命令处理器读取。
    ShaderDeviceAddress = 1u << 7 ///< 允许 shader 通过设备地址访问 buffer，常用于 bindless 或光追数据。
};

[[nodiscard]] constexpr RHIBufferUsage operator|(RHIBufferUsage lhs, RHIBufferUsage rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHIBufferUsage operator&(RHIBufferUsage lhs, RHIBufferUsage rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHIBufferUsage& operator|=(RHIBufferUsage& lhs, RHIBufferUsage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// buffer 创建附加标志，用于表达生命周期和后端内存选择提示。
enum class RHIBufferCreateFlags : u32 {
    None = 0, ///< 无额外创建要求，使用后端默认分配策略。
    DedicatedMemory = 1u << 0, ///< 倾向单独分配内存，适合大 buffer 或需要避免与其他资源混用的场景。
    SparseBinding = 1u << 1, ///< 请求稀疏绑定/虚拟内存能力，允许按页提交 buffer 存储。
    RingBuffer = 1u << 2, ///< 表示 buffer 用作环形分配区，常用于动态 uniform 或 per-frame 上传。
    Transient = 1u << 3 ///< 表示短生命周期临时 buffer，资源分配器可优先复用或延迟实际分配。
};

[[nodiscard]] constexpr RHIBufferCreateFlags operator|(RHIBufferCreateFlags lhs, RHIBufferCreateFlags rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHIBufferCreateFlags operator&(RHIBufferCreateFlags lhs, RHIBufferCreateFlags rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHIBufferCreateFlags& operator|=(RHIBufferCreateFlags& lhs, RHIBufferCreateFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// texture/image 的用途位。一个 texture 可以同时是采样贴图、渲染目标或拷贝目标。
enum class RHITextureUsage : u32 {
    None = 0, ///< 未声明用途；只适合默认值，真实 texture 创建通常应指定至少一个用途。
    TransferSource = 1u << 0, ///< 可作为 copy、blit、resolve 或 readback 的源 texture。
    TransferDestination = 1u << 1, ///< 可作为 upload、copy、blit、resolve 或 clear 的目标 texture。
    Sampled = 1u << 2, ///< 可创建 sampled view 并在 shader 中作为只读纹理采样。
    Storage = 1u << 3, ///< 可创建 storage image/UAV view 并在 shader 中随机读写。
    ColorAttachment = 1u << 4, ///< 可作为 color render target 写入。
    DepthStencilAttachment = 1u << 5, ///< 可作为 depth-stencil attachment 参与深度/模板测试。
    Present = 1u << 6, ///< 可作为 swapchain/backbuffer image 交给窗口系统呈现。
    Transient = 1u << 7 ///< 表示临时 attachment，后端可使用 lazily allocated 或 RenderGraph 复用策略。
};

[[nodiscard]] constexpr RHITextureUsage operator|(RHITextureUsage lhs, RHITextureUsage rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHITextureUsage operator&(RHITextureUsage lhs, RHITextureUsage rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHITextureUsage& operator|=(RHITextureUsage& lhs, RHITextureUsage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// texture 创建附加标志，用于表达 cube、格式重解释、稀疏资源等需求。
enum class RHITextureCreateFlags : u32 {
    None = 0, ///< 无额外创建要求，使用普通 texture 创建路径。
    CubeCompatible = 1u << 0, ///< 允许 2D array texture 创建 cube/cube array view，层数需满足 6 的倍数。
    MutableFormat = 1u << 1, ///< 允许 view 使用兼容格式重解释底层存储格式。
    DedicatedMemory = 1u << 2, ///< 倾向为该 texture 单独分配内存，适合大 render target 或特殊资源。
    SparseBinding = 1u << 3, ///< 请求稀疏纹理绑定能力，允许按 tile/page 提交纹理存储。
    GenerateMips = 1u << 4, ///< 表示资源创建后需要生成 mipmap，后端可预留必要 usage 和状态。
    RenderGraphTransient = 1u << 5 ///< 表示 RenderGraph 内部临时纹理，可参与别名和生命周期裁剪。
};

[[nodiscard]] constexpr RHITextureCreateFlags operator|(RHITextureCreateFlags lhs, RHITextureCreateFlags rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHITextureCreateFlags operator&(RHITextureCreateFlags lhs, RHITextureCreateFlags rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHITextureCreateFlags& operator|=(RHITextureCreateFlags& lhs, RHITextureCreateFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 资源内存访问方向。后端可据此选择显存、本地可映射内存或读回内存。
enum class RHIMemoryUsage : u8 {
    GpuOnly, ///< GPU 本地内存，CPU 不直接访问；适合静态贴图、渲染目标和长期驻留资源。
    CpuToGpu, ///< CPU 写入、GPU 读取的上传内存，适合 staging、动态 uniform 和流式顶点数据。
    GpuToCpu, ///< GPU 写入、CPU 读取的读回内存，适合截图、查询结果和调试 readback。
    CpuOnly ///< 仅 CPU 访问的后备内存或模拟资源，通常用于工具、测试或无 GPU 路径。
};

/// RHIMemory is the concise public name for RHIMemoryUsage.
using RHIMemory = RHIMemoryUsage;

/// 资源生命周期提示，帮助资源分配器决定是否池化、复用或立即释放。
enum class RHIResourceLifetime : u8 {
    Persistent, ///< 长期驻留资源，跨多帧保存内容和句柄，例如材质贴图、mesh buffer。
    PerFrame, ///< 每帧轮转资源，通常按 frames-in-flight 分配，适合动态常量和临时上传。
    Transient ///< 单个 pass 或 RenderGraph 生命周期内有效，可被分配器快速复用或别名。
};

/// 纹理资源本身的维度，不包含 view 维度重解释。
enum class RHITextureDimension : u8 {
    Texture1D, ///< 一维纹理资源，常用于 LUT、曲线表或少量线性采样数据。
    Texture2D, ///< 二维纹理资源，最常用的贴图、render target、depth target 和 cube array 基础维度。
    Texture3D ///< 三维体纹理资源，常用于体积数据、噪声场、体渲染或 3D LUT。
};

/// 纹理视图维度。一个 2D array texture 可以被创建为 View2DArray 或单层 View2D。
enum class RHITextureViewDimension : u8 {
    View1D, ///< 暴露单个一维纹理 view。
    View1DArray, ///< 暴露一维纹理数组 view，可访问多个 array layer。
    View2D, ///< 暴露单个二维纹理 view，常用于普通采样贴图和 render target。
    View2DArray, ///< 暴露二维纹理数组 view，常用于 texture array、shadow map array 或 multiview。
    View3D, ///< 暴露三维体纹理 view。
    Cube, ///< 暴露 6 层二维数组为单个 cube map view。
    CubeArray ///< 暴露多个 cube map 组成的 cube array view，层数为 6 的倍数。
};

/// 纹理采样过滤方式。
enum class RHIFilterMode : u8 {
    Nearest, ///< 最近点采样，保留硬边像素，适合像素风、整数数据或无需插值的查表。
    Linear ///< 线性插值采样，适合普通颜色/法线贴图和缩放时的平滑过滤。
};

/// mip 层级之间的过滤方式。
enum class RHIMipmapMode : u8 {
    Nearest, ///< 选择最接近的单个 mip 层，不在两个 mip 之间插值。
    Linear ///< 在相邻 mip 层之间线性插值，减少层级切换带来的闪烁。
};

/// UVW 坐标越界时的寻址方式。
enum class RHIAddressMode : u8 {
    Repeat, ///< 坐标越界时按 1.0 周期重复纹理。
    MirroredRepeat, ///< 坐标越界时镜像重复纹理，减少平铺接缝方向突变。
    ClampToEdge, ///< 坐标越界时钳制到边缘 texel，常用于 UI、shadow map 和后处理纹理。
    ClampToBorder ///< 坐标越界时返回指定边框颜色，常用于 shadow map 边界处理。
};

/// ClampToBorder 模式下采样器返回的边框颜色。
enum class RHIBorderColor : u8 {
    TransparentBlack, ///< 边框返回透明黑色 (0,0,0,0)，适合 alpha 参与裁剪的采样场景。
    OpaqueBlack, ///< 边框返回不透明黑色 (0,0,0,1)，适合默认无光照或深色边界。
    OpaqueWhite ///< 边框返回不透明白色 (1,1,1,1)，常用于 shadow compare 的安全边界。
};

/// 通用比较函数，深度测试、模板测试和 shadow sampler 都会使用。
enum class RHICompareOp : u8 {
    Never, ///< 比较永远失败，用于禁用通过路径或特殊模板写入控制。
    Less, ///< 输入值小于参考值时通过，常用于标准深度测试。
    Equal, ///< 输入值等于参考值时通过，常用于模板标记或精确深度匹配。
    LessOrEqual, ///< 输入值小于等于参考值时通过，常用于 skybox、shadow compare 或反转差异适配。
    Greater, ///< 输入值大于参考值时通过，常用于 reversed-Z 深度测试。
    NotEqual, ///< 输入值不等于参考值时通过，常用于模板轮廓或遮罩反选。
    GreaterOrEqual, ///< 输入值大于等于参考值时通过，常用于 reversed-Z 的宽松比较。
    Always ///< 比较永远通过，用于关闭比较约束但保留对应测试/写入流程。
};

/// GPU buffer 创建描述，不包含初始数据；初始数据通过 RHIUploadBatchDesc 提交。
struct RHIBufferDesc {
    std::string debugName; ///< 调试名称，用于后端对象命名和 GPU 调试器显示。
    u64 size = 0; ///< buffer 字节数。创建真实 GPU buffer 时必须大于 0。
    RHIBufferUsage usage = RHIBufferUsage::None; ///< 声明 buffer 的用途位，后端据此设置 usage flags。
    RHIBufferCreateFlags flags = RHIBufferCreateFlags::None; ///< 创建附加标志，例如 transient、dedicated memory。
    RHIMemoryUsage memoryUsage = RHIMemoryUsage::GpuOnly; ///< 内存访问模式，决定资源放在显存、上传堆或读回堆。
    RHIResourceLifetime lifetime = RHIResourceLifetime::Persistent; ///< 生命周期提示，影响分配器复用策略。
    bool persistentlyMapped = false; ///< 是否希望 CPU 长期映射该 buffer，通常用于动态 uniform/staging 数据。
};

/// GPU texture/image 创建描述，不包含 view 和 sampler。
struct RHITextureDesc {
    std::string debugName; ///< 调试名称，用于后端对象命名。
    RHITextureDimension dimension = RHITextureDimension::Texture2D; ///< 资源维度。cube texture 本质上仍是 2D array。
    RHIExtent3D extent{}; ///< texture 的像素尺寸。2D texture 的 depth 应为 1。
    u32 arrayLayers = 1; ///< 数组层数；cube 为 6，cube array 为 6 的倍数。
    u32 mipLevels = 1; ///< mip 层数；如果需要自动生成 mip，创建时仍要预留层数。
    RHIFormat format = RHIFormat::RGBA8_UNorm; ///< 资源存储格式。
    RHISampleCount samples = RHISampleCount::Count1; ///< MSAA 采样数；普通采样贴图一般为 Count1。
    RHITextureUsage usage = RHITextureUsage::Sampled; ///< texture 后续用途，影响后端 image usage/resource flags。
    RHITextureCreateFlags flags = RHITextureCreateFlags::None; ///< 创建附加标志，例如 cube compatible、mutable format。
    RHIResourceLifetime lifetime = RHIResourceLifetime::Persistent; ///< 生命周期提示，RenderGraph 临时图一般设为 Transient。
    RHIResourceState initialState = RHIResourceState::Undefined; ///< 创建后的逻辑初始状态，后端可据此插入首个 transition。
};

/// texture view 创建描述，用于选择 texture 的一部分或重解释可兼容格式。
struct RHITextureViewDesc {
    std::string debugName; ///< 调试名称。
    RHITexture texture{}; ///< 被查看的底层 texture。
    RHITextureViewDimension dimension = RHITextureViewDimension::View2D; ///< view 暴露给 shader/attachment 的维度。
    RHIFormat format = RHIFormat::Undefined; ///< Undefined 表示沿用底层 texture 格式。
    RHITextureAspect aspect = RHITextureAspect::Color; ///< view 覆盖的 aspect，depth/stencil view 需要显式设置。
    u32 baseMipLevel = 0; ///< view 起始 mip。
    u32 mipLevelCount = 1; ///< view 覆盖 mip 数。
    u32 baseArrayLayer = 0; ///< view 起始数组层。
    u32 arrayLayerCount = 1; ///< view 覆盖数组层数。
};

/// sampler 创建描述。sampler 只描述采样规则，不持有 texture。
struct RHISamplerDesc {
    std::string debugName; ///< 调试名称。
    RHIFilterMode minFilter = RHIFilterMode::Linear; ///< 缩小时的过滤方式。
    RHIFilterMode magFilter = RHIFilterMode::Linear; ///< 放大时的过滤方式。
    RHIMipmapMode mipmapMode = RHIMipmapMode::Linear; ///< mip 层之间的过滤方式。
    RHIAddressMode addressU = RHIAddressMode::Repeat; ///< U 方向寻址。
    RHIAddressMode addressV = RHIAddressMode::Repeat; ///< V 方向寻址。
    RHIAddressMode addressW = RHIAddressMode::Repeat; ///< W 方向寻址，3D texture 时使用。
    float mipLodBias = 0.0F; ///< mip LOD 偏移。
    float minLod = 0.0F; ///< 可采样的最小 mip LOD。
    float maxLod = std::numeric_limits<float>::max(); ///< 可采样的最大 mip LOD。
    bool enableAnisotropy = false; ///< 是否启用各向异性过滤。
    float maxAnisotropy = 1.0F; ///< 各向异性等级，启用时后端需要 clamp 到设备上限。
    bool enableCompare = false; ///< 是否启用比较采样，常用于 shadow map。
    RHICompareOp compareOp = RHICompareOp::LessOrEqual; ///< 比较采样函数。
    RHIBorderColor borderColor = RHIBorderColor::OpaqueBlack; ///< ClampToBorder 时使用的边框颜色。
};

/// shader 阶段位掩码，用于描述 shader 自身阶段和资源可见性。
enum class RHIShaderStage : u32 {
    None = 0, ///< 不指定 shader 阶段；用于空可见性、默认值或反射失败占位。
    Vertex = 1u << 0, ///< vertex shader 阶段，处理顶点输入并输出裁剪空间位置。
    TessControl = 1u << 1, ///< tessellation control/hull shader 阶段，生成 patch 细分控制数据。
    TessEvaluation = 1u << 2, ///< tessellation evaluation/domain shader 阶段，计算细分后的顶点位置。
    Geometry = 1u << 3, ///< geometry shader 阶段，可按图元生成、扩展或丢弃几何。
    Fragment = 1u << 4, ///< fragment/pixel shader 阶段，计算片元颜色、深度或其他输出。
    Compute = 1u << 5, ///< compute shader 阶段，用于通用 GPU 计算和非图形 pass。
    Task = 1u << 6, ///< task/amplification shader 阶段，用于 mesh shader 前的工作生成。
    Mesh = 1u << 7, ///< mesh shader 阶段，直接生成图元并替代传统 vertex input 管线。
    AllGraphics = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 6) | (1u << 7), ///< 覆盖所有图形 shader 阶段，不包含 compute。
    All = 0xFFFFFFFFu ///< 覆盖所有 shader 阶段，适合全局资源可见性或保守绑定布局。
};

[[nodiscard]] constexpr RHIShaderStage operator|(RHIShaderStage lhs, RHIShaderStage rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHIShaderStage operator&(RHIShaderStage lhs, RHIShaderStage rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHIShaderStage& operator|=(RHIShaderStage& lhs, RHIShaderStage rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// shader 源码或字节码的语言/中间格式。
enum class RHIShaderLanguage : u8 {
    Unknown, ///< 未指定 shader 语言或字节码格式；后端需根据文件扩展名或配置推导。
    GLSL, ///< GLSL 源码，常用于 OpenGL/Vulkan 开发期编译路径。
    HLSL, ///< HLSL 源码，常用于 Direct3D 或跨编译到 SPIR-V 的路径。
    Slang, ///< Slang 源码，适合多后端 shader 生成和高级参数化。
    MSL, ///< Metal Shading Language 源码，供 Metal 后端直接编译或加载。
    SPIRV, ///< SPIR-V 字节码，通常供 Vulkan 后端直接创建 shader module。
    DXIL ///< DXIL 字节码，通常供 Direct3D 12 或 DXC 编译路径使用。
};

/// shader 宏定义，用于同一份源码生成不同变体。
struct RHIShaderDefine {
    std::string name; ///< 宏名称。
    std::string value = "1"; ///< 宏值，默认定义为 1。
};

/// shader 编译参数。运行期加载 bytecode 时可以忽略。
struct RHIShaderCompileOptions {
    std::string targetProfile; ///< 目标 profile，例如 vs_6_6、ps_6_6、spirv_1_6。
    std::vector<std::string> includeDirectories; ///< include 搜索路径。
    std::vector<RHIShaderDefine> defines; ///< 编译宏列表。
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
struct RHIShaderDesc {
    std::string debugName; ///< 调试名称。
    RHIShaderStage stage = RHIShaderStage::Vertex; ///< 当前 shader 属于哪个管线阶段。
    RHIShaderLanguage language = RHIShaderLanguage::Unknown; ///< 源码或 bytecode 的语言/格式。
    std::string entryPoint = "main"; ///< shader 入口函数名。
    std::string filePath; ///< shader 文件路径；为空表示不从文件加载。
    std::string source; ///< 内存中的 shader 源码；适合工具生成或热重载。
    std::vector<std::byte> bytecode; ///< 已编译字节码；例如 SPIR-V、DXIL、Metal library 数据。
    RHIShaderCompileOptions compileOptions{}; ///< 需要编译 source/filePath 时使用的编译参数。
};

/// shader specialization constant，用于在创建管线时固化常量并触发后端优化。
struct RHIShaderSpecializationConstant {
    u32 constantId = 0; ///< shader 中声明的 specialization constant id。
    std::vector<std::byte> data; ///< 常量原始字节数据，后端按 shader 反射信息解释类型。
};

/// 资源绑定槽类型，描述 shader 看到的资源类别。
enum class RHIBindingType : u8 {
    UniformBuffer, ///< 只读常量/uniform buffer 绑定，适合小块频繁读取参数。
    StorageBuffer, ///< storage/UAV buffer 绑定，允许 shader 读取或写入结构化数据。
    SampledTexture, ///< 只读 sampled texture view，需配合 sampler 或独立采样器使用。
    StorageTexture, ///< storage image/UAV texture 绑定，允许 shader 随机读写像素。
    Sampler, ///< 独立 sampler 状态绑定，只描述过滤、寻址和比较规则。
    CombinedTextureSampler, ///< texture 与 sampler 组合绑定，适配 GLSL combined image sampler 模型。
    PushConstant, ///< push constant/root constant 小块数据绑定，适合高频少量参数。
    AccelerationStructure ///< ray tracing acceleration structure 绑定，供光追 shader 查询场景几何。
};

/// sampled texture 在 shader 中的采样数据类型。
enum class RHITextureSampleType : u8 {
    Float, ///< 可过滤浮点/归一化纹理采样类型，支持普通线性过滤。
    UnfilterableFloat, ///< 不可过滤浮点纹理采样类型，例如部分高精度 float 格式。
    SignedInteger, ///< 有符号整数纹理采样类型，shader 读取 int 值且不可线性过滤。
    UnsignedInteger, ///< 无符号整数纹理采样类型，shader 读取 uint 值且不可线性过滤。
    Depth ///< 深度纹理采样类型，可用于 depth compare 或读取 depth 值。
};

/// 单个绑定槽的布局信息，相当于 descriptor binding/root parameter/argument entry。
struct RHIBindGroupLayoutEntry {
    u32 binding = 0; ///< 绑定槽编号，对应 shader 中的 binding/register/argument index。
    RHIBindingType type = RHIBindingType::UniformBuffer; ///< 该槽位资源类型。
    RHIShaderStage visibility = RHIShaderStage::AllGraphics; ///< 哪些 shader stage 可以访问该槽位。
    u32 arrayCount = 1; ///< 资源数组长度；bindless 或数组纹理绑定可大于 1。
    bool writable = false; ///< storage buffer/texture 是否允许 shader 写入。
    RHITextureViewDimension textureViewDimension = RHITextureViewDimension::View2D; ///< texture 绑定期望的 view 维度。
    RHITextureSampleType textureSampleType = RHITextureSampleType::Float; ///< sampled texture 的采样类型。
    RHIFormat storageTextureFormat = RHIFormat::Undefined; ///< storage texture 格式，非 storage texture 时忽略。
};

/// 一组绑定槽布局。Vulkan 中通常对应一个 descriptor set layout。
struct RHIBindGroupLayoutDesc {
    std::string debugName; ///< 调试名称。
    u32 set = 0; ///< 绑定组编号，对应 Vulkan set / D3D register space / Metal buffer index 分组。
    std::vector<RHIBindGroupLayoutEntry> entries; ///< 该组内所有 binding 声明。
};

/// buffer 绑定到 shader 时的范围。
struct RHIBufferBinding {
    RHIBuffer buffer{}; ///< 被绑定的 buffer。
    u64 offset = 0; ///< 起始字节偏移。
    u64 size = RHI_WHOLE_SIZE; ///< 绑定字节范围，RHI_WHOLE_SIZE 表示从 offset 到末尾。
};

/// texture 绑定到 shader 时的 view。texture 字段可用于后端验证 view 来源。
struct RHITextureBinding {
    RHITextureView view{}; ///< 实际暴露给 shader 的 texture view。
    RHITexture texture{}; ///< 底层 texture，可选但有助于调试和状态跟踪。
};

/// 一个实际资源绑定写入项。
struct RHIResourceBinding {
    u32 binding = 0; ///< 目标绑定槽编号，必须存在于 RHIBindGroupLayoutEntry 中。
    u32 arrayElement = 0; ///< 写入资源数组的第几个元素。
    RHIBindingType type = RHIBindingType::UniformBuffer; ///< 本次写入的资源类型。
    RHIBufferBinding buffer{}; ///< buffer 类型绑定时使用。
    RHITextureBinding texture{}; ///< texture 类型绑定时使用。
    RHISampler sampler{}; ///< sampler 或 combined texture sampler 绑定时使用。
};

/// 一组实际绑定资源。渲染命令只绑定 RHIBindGroup，不直接散落绑定单个资源。
struct RHIBindGroupDesc {
    std::string debugName; ///< 调试名称。
    RHIBindGroupLayout layout{}; ///< 该绑定组使用的布局。
    std::vector<RHIResourceBinding> bindings; ///< 初始资源绑定列表。
};

/// push constant/root constant 的可见范围。
struct RHIPushConstantRange {
    RHIShaderStage stages = RHIShaderStage::AllGraphics; ///< 哪些 shader stage 可以访问该常量范围。
    u32 offset = 0; ///< 字节偏移。
    u32 size = 0; ///< 字节大小。
};

/// 管线资源布局。所有 graphics/compute pipeline 都应该引用一个布局。
struct RHIPipelineLayoutDesc {
    std::string debugName; ///< 调试名称。
    std::vector<RHIBindGroupLayout> bindGroupLayouts; ///< 管线可绑定的 bind group layout 列表，顺序代表 set/space。
    std::vector<RHIPushConstantRange> pushConstants; ///< 小块高频常量范围。
};

/// shader 反射得到的资源绑定信息，可用于自动生成 RHIBindGroupLayoutDesc。
struct RHIShaderResourceReflection {
    std::string name; ///< shader 中的资源名称。
    u32 set = 0; ///< 资源所在 set/space。
    u32 binding = 0; ///< 资源 binding/register。
    RHIBindingType type = RHIBindingType::UniformBuffer; ///< 资源类型。
    RHIShaderStage stages = RHIShaderStage::None; ///< 使用该资源的 shader stage。
    u32 arrayCount = 1; ///< 数组长度。
    u32 size = 0; ///< buffer 或 push constant 字节大小，未知时为 0。
};

/// shader 输入/输出参数反射信息。
struct RHIShaderParameterReflection {
    std::string name; ///< 参数名称。
    std::string semanticName; ///< HLSL 语义名，GLSL/Slang 可为空。
    u32 semanticIndex = 0; ///< 语义索引。
    u32 location = 0; ///< shader location。
    RHIFormat format = RHIFormat::Undefined; ///< 参数格式，无法推导时为 Undefined。
};

/// 一个 shader 或完整 shader program 的反射结果。
struct RHIShaderReflectionDesc {
    std::vector<RHIShaderResourceReflection> resources; ///< 资源绑定反射列表。
    std::vector<RHIShaderParameterReflection> inputs; ///< shader 输入参数。
    std::vector<RHIShaderParameterReflection> outputs; ///< shader 输出参数。
    std::vector<RHIPushConstantRange> pushConstants; ///< push constant 范围。
};

/// 顶点属性格式，描述单个 attribute 在 vertex buffer 中的存储类型。
enum class RHIVertexFormat : u8 {
    Float32, ///< 单个 32 位浮点属性，常用于标量权重或自定义数据。
    Float32x2, ///< 两个 32 位浮点属性，常用于 UV、屏幕坐标或二维向量。
    Float32x3, ///< 三个 32 位浮点属性，常用于 position、normal、tangent.xyz。
    Float32x4, ///< 四个 32 位浮点属性，常用于 tangent、color 或矩阵列。
    UInt32, ///< 单个 32 位无符号整数属性，shader 以 uint 读取。
    UInt32x2, ///< 两个 32 位无符号整数属性，适合 ID、索引或压缩数据。
    UInt32x3, ///< 三个 32 位无符号整数属性，适合自定义整数向量。
    UInt32x4, ///< 四个 32 位无符号整数属性，常用于 bone indices 的高精度存储。
    SInt32, ///< 单个 32 位有符号整数属性，shader 以 int 读取。
    SInt32x2, ///< 两个 32 位有符号整数属性。
    SInt32x3, ///< 三个 32 位有符号整数属性。
    SInt32x4, ///< 四个 32 位有符号整数属性。
    UNorm8x4, ///< 四个 8 位无符号归一化属性，常用于顶点颜色或 packed weights。
    SNorm8x4, ///< 四个 8 位有符号归一化属性，常用于压缩法线/切线向量。
    UInt16x2, ///< 两个 16 位无符号整数属性，shader 以 uint 分量读取。
    UInt16x4, ///< 四个 16 位无符号整数属性，常用于压缩索引或骨骼编号。
    SInt16x2, ///< 两个 16 位有符号整数属性。
    SInt16x4, ///< 四个 16 位有符号整数属性。
    UNorm16x2, ///< 两个 16 位无符号归一化属性，适合高精度 UV 或权重。
    UNorm16x4, ///< 四个 16 位无符号归一化属性，适合高精度颜色或权重。
    SNorm16x2, ///< 两个 16 位有符号归一化属性，适合压缩二维方向数据。
    SNorm16x4 ///< 四个 16 位有符号归一化属性，适合高精度压缩切线/法线数据。
};

/// 顶点 buffer 的步进频率，区分逐顶点数据和实例化数据。
enum class RHIVertexInputRate : u8 {
    PerVertex, ///< 每个顶点前进一次，适用于 position、normal、uv 等网格顶点数据。
    PerInstance ///< 每个实例前进一次，适用于 instance transform、颜色或自定义实例参数。
};

/// 单个顶点属性描述，例如 position/normal/uv/color。
struct RHIVertexAttributeDesc {
    std::string semanticName; ///< 语义名，D3D/HLSL 常用；Vulkan/GL 可用于反射和调试。
    u32 semanticIndex = 0; ///< 同名语义的索引，例如 TEXCOORD0/TEXCOORD1。
    u32 location = 0; ///< shader 输入 location。
    u32 binding = 0; ///< 来自哪个 vertex buffer binding。
    RHIVertexFormat format = RHIVertexFormat::Float32x3; ///< 属性格式。
    u64 offset = 0; ///< 在单个顶点结构内的字节偏移。
};

/// 一个 vertex buffer binding 的布局，可包含多个属性。
struct RHIVertexBufferLayoutDesc {
    u32 binding = 0; ///< vertex buffer binding 编号。
    u64 stride = 0; ///< 相邻顶点/实例数据之间的字节跨度。
    RHIVertexInputRate inputRate = RHIVertexInputRate::PerVertex; ///< 按顶点还是按实例前进。
    u32 stepRate = 1; ///< 实例化步进倍率；大多数后端常用 1。
    std::vector<RHIVertexAttributeDesc> attributes; ///< 该 binding 提供的属性列表。
};

/// 输入图元拓扑，描述顶点流如何被解释成点、线、三角形或 patch。
enum class RHIPrimitiveTopology : u8 {
    PointList, ///< 每个顶点生成一个点图元，常用于粒子、调试点或点云。
    LineList, ///< 每两个顶点生成一条独立线段，常用于调试线框和辅助线。
    LineStrip, ///< 顶点按顺序连接为连续折线，可用 primitive restart 分段。
    TriangleList, ///< 每三个顶点生成一个独立三角形，最常用的网格拓扑。
    TriangleStrip, ///< 顶点按条带生成连续三角形，减少顶点数量但排序约束更强。
    PatchList ///< patch 图元，用于 tessellation 阶段，控制点数量由管线状态指定。
};

/// 多边形栅格化模式。Line/Point 不是所有平台和设备都完全支持。
enum class RHIPolygonMode : u8 {
    Fill, ///< 填充三角形内部，标准实体渲染模式。
    Line, ///< 只栅格化多边形边线，用于线框显示或调试。
    Point ///< 只栅格化多边形顶点，用于特殊调试或点模式渲染。
};

/// 面剔除模式。
enum class RHICullMode : u8 {
    None, ///< 不剔除任何面，双面材质或调试时使用。
    Front, ///< 剔除正面三角形，常用于特殊 shadow 或反面渲染技巧。
    Back, ///< 剔除背面三角形，实体网格最常用的模式。
    FrontAndBack ///< 同时剔除正面和背面，通常只用于禁用光栅输出的特殊路径。
};

/// 正面三角形绕序。注意不同 API 的 framebuffer 坐标系可能影响最终正反面判断。
enum class RHIFrontFace : u8 {
    CounterClockwise, ///< 顶点在屏幕空间逆时针排列时视为正面。
    Clockwise ///< 顶点在屏幕空间顺时针排列时视为正面。
};

/// 模板测试操作。
enum class RHIStencilOp : u8 {
    Keep, ///< 保留当前 stencil 值不变。
    Zero, ///< 将 stencil 值写为 0。
    Replace, ///< 将 stencil 值替换为 reference 值。
    IncrementClamp, ///< stencil 值加 1，并在最大值处饱和。
    DecrementClamp, ///< stencil 值减 1，并在 0 处饱和。
    Invert, ///< 按位反转 stencil 值。
    IncrementWrap, ///< stencil 值加 1，超过最大值后回绕到 0。
    DecrementWrap ///< stencil 值减 1，低于 0 后回绕到最大值。
};

/// 混合因子，用于颜色和 alpha blend。
enum class RHIBlendFactor : u8 {
    Zero, ///< 混合因子为 0，完全忽略对应输入。
    One, ///< 混合因子为 1，完整保留对应输入。
    SourceColor, ///< 使用源颜色 RGB 作为混合因子。
    OneMinusSourceColor, ///< 使用 1 - 源颜色 RGB 作为混合因子。
    DestinationColor, ///< 使用目标颜色 RGB 作为混合因子。
    OneMinusDestinationColor, ///< 使用 1 - 目标颜色 RGB 作为混合因子。
    SourceAlpha, ///< 使用源 alpha 作为混合因子，常用于普通透明混合。
    OneMinusSourceAlpha, ///< 使用 1 - 源 alpha 作为混合因子，常用于普通透明混合目标项。
    DestinationAlpha, ///< 使用目标 alpha 作为混合因子。
    OneMinusDestinationAlpha, ///< 使用 1 - 目标 alpha 作为混合因子。
    ConstantColor, ///< 使用 RHIBlendState::blendConstants 的 RGB 作为混合因子。
    OneMinusConstantColor, ///< 使用 1 - 常量颜色 RGB 作为混合因子。
    ConstantAlpha, ///< 使用 RHIBlendState::blendConstants 的 alpha 作为混合因子。
    OneMinusConstantAlpha ///< 使用 1 - 常量 alpha 作为混合因子。
};

/// 混合运算。
enum class RHIBlendOp : u8 {
    Add, ///< 源项加目标项，最常用的颜色/alpha 混合运算。
    Subtract, ///< 源项减目标项，用于特殊合成效果。
    ReverseSubtract, ///< 目标项减源项，用于反向差值合成。
    Min, ///< 取源项和目标项的逐分量最小值。
    Max ///< 取源项和目标项的逐分量最大值。
};

/// 逻辑颜色运算。现代渲染中较少使用，部分后端可能不支持。
enum class RHILogicOp : u8 {
    Clear, ///< 输出全 0，忽略源和目标颜色。
    And, ///< 输出 source AND destination。
    AndReverse, ///< 输出 source AND (NOT destination)。
    Copy, ///< 输出 source，等价于直接写入源颜色。
    AndInverted, ///< 输出 (NOT source) AND destination。
    NoOp, ///< 保留 destination，不写入源颜色。
    Xor, ///< 输出 source XOR destination。
    Or, ///< 输出 source OR destination。
    Nor, ///< 输出 NOT (source OR destination)。
    Equivalent, ///< 输出 NOT (source XOR destination)，即逐位等价。
    Invert, ///< 输出 NOT destination，反转目标颜色位。
    OrReverse, ///< 输出 source OR (NOT destination)。
    CopyInverted, ///< 输出 NOT source。
    OrInverted, ///< 输出 (NOT source) OR destination。
    Nand, ///< 输出 NOT (source AND destination)。
    Set ///< 输出全 1，忽略源和目标颜色。
};

/// color attachment 写通道掩码。
enum class RHIColorWriteMask : u8 {
    None = 0, ///< 不写入任何颜色通道，适合只写深度/模板的 pass。
    R = 1u << 0, ///< 允许写入红色通道。
    G = 1u << 1, ///< 允许写入绿色通道。
    B = 1u << 2, ///< 允许写入蓝色通道。
    A = 1u << 3, ///< 允许写入 alpha 通道。
    All = 0x0F ///< 允许写入 RGBA 四个颜色通道。
};

[[nodiscard]] constexpr RHIColorWriteMask operator|(RHIColorWriteMask lhs, RHIColorWriteMask rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHIColorWriteMask operator&(RHIColorWriteMask lhs, RHIColorWriteMask rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHIColorWriteMask& operator|=(RHIColorWriteMask& lhs, RHIColorWriteMask rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 创建管线时不固定、录制命令时动态设置的状态。
enum class RHIDynamicState : u8 {
    RHIViewport, ///< viewport 在命令录制时设置，而不是固定在 pipeline 中。
    Scissor, ///< scissor 矩形在命令录制时设置，便于不同 pass 或窗口尺寸复用管线。
    LineWidth, ///< line width 在命令录制时设置，主要用于线框或调试绘制。
    DepthBias, ///< depth bias 在命令录制时设置，常用于 shadow map 按 pass 调整偏移。
    BlendConstants, ///< blend constant 在命令录制时设置，供 ConstantColor/ConstantAlpha 因子使用。
    StencilReference ///< stencil reference 在命令录制时设置，便于不同对象使用不同模板值。
};

/// 输入装配阶段状态，描述顶点如何组成图元。
struct RHIInputAssemblyState {
    RHIPrimitiveTopology topology = RHIPrimitiveTopology::TriangleList; ///< 图元拓扑，最常见是 TriangleList。
    bool primitiveRestart = false; ///< strip 拓扑中是否允许特殊索引重启图元。
    u32 patchControlPoints = 0; ///< PatchList 使用的控制点数量，非曲面细分时为 0。
};

/// 光栅化阶段状态。
struct RHIRasterState {
    RHIPolygonMode polygonMode = RHIPolygonMode::Fill; ///< 填充、线框或点模式。
    RHICullMode cullMode = RHICullMode::Back; ///< 剔除哪些面。
    RHIFrontFace frontFace = RHIFrontFace::CounterClockwise; ///< 判定正面的顶点绕序。
    bool depthClampEnable = false; ///< 是否把超出 near/far 的深度 clamp，而不是裁剪。
    bool depthBiasEnable = false; ///< 是否启用深度偏移，常用于 shadow map。
    float depthBiasConstantFactor = 0.0F; ///< 常量深度偏移。
    float depthBiasClamp = 0.0F; ///< 深度偏移 clamp。
    float depthBiasSlopeFactor = 0.0F; ///< 与斜率相关的深度偏移。
    float lineWidth = 1.0F; ///< 线宽；多数现代后端只保证 1.0。
};

/// 单面的模板测试状态。
struct RHIStencilFaceState {
    RHIStencilOp failOp = RHIStencilOp::Keep; ///< 模板测试失败时的操作。
    RHIStencilOp passOp = RHIStencilOp::Keep; ///< 模板和深度测试都通过时的操作。
    RHIStencilOp depthFailOp = RHIStencilOp::Keep; ///< 模板通过但深度失败时的操作。
    RHICompareOp compareOp = RHICompareOp::Always; ///< 模板比较函数。
    u32 compareMask = 0xFFFFFFFFu; ///< 比较时使用的读掩码。
    u32 writeMask = 0xFFFFFFFFu; ///< 写入 stencil buffer 时的写掩码。
    u32 reference = 0; ///< 模板参考值；也可以通过 RHIDynamicState 动态设置。
};

/// 深度和模板测试状态。
struct RHIDepthStencilState {
    bool depthTestEnable = true; ///< 是否读取 depth buffer 做深度测试。
    bool depthWriteEnable = true; ///< 深度测试通过后是否写入 depth buffer。
    RHICompareOp depthCompareOp = RHICompareOp::Less; ///< 深度比较函数。
    bool depthBoundsTestEnable = false; ///< 是否启用深度范围测试，部分后端不支持。
    float minDepthBounds = 0.0F; ///< 深度范围测试下限。
    float maxDepthBounds = 1.0F; ///< 深度范围测试上限。
    bool stencilTestEnable = false; ///< 是否启用 stencil 测试。
    RHIStencilFaceState front{}; ///< 正面模板状态。
    RHIStencilFaceState back{}; ///< 背面模板状态。
};

/// 多重采样状态。
struct RHIMultisampleState {
    RHISampleCount samples = RHISampleCount::Count1; ///< MSAA 采样数，必须与 render target 采样数兼容。
    bool sampleShadingEnable = false; ///< 是否启用 per-sample shading。
    float minSampleShading = 1.0F; ///< per-sample shading 的最小采样比例。
    u64 sampleMask = std::numeric_limits<u64>::max(); ///< 采样位掩码。
    bool alphaToCoverageEnable = false; ///< 是否把 alpha 转成 coverage，常用于植被边缘。
    bool alphaToOneEnable = false; ///< 是否把 alpha 强制为 1，支持度有限。
};

/// 单个 color attachment 的混合状态。
struct RHIColorBlendAttachmentState {
    bool blendEnable = false; ///< 是否启用 blending。
    RHIBlendFactor sourceColor = RHIBlendFactor::One; ///< 源颜色因子。
    RHIBlendFactor destinationColor = RHIBlendFactor::Zero; ///< 目标颜色因子。
    RHIBlendOp colorOp = RHIBlendOp::Add; ///< 颜色混合运算。
    RHIBlendFactor sourceAlpha = RHIBlendFactor::One; ///< 源 alpha 因子。
    RHIBlendFactor destinationAlpha = RHIBlendFactor::Zero; ///< 目标 alpha 因子。
    RHIBlendOp alphaOp = RHIBlendOp::Add; ///< alpha 混合运算。
    RHIColorWriteMask writeMask = RHIColorWriteMask::All; ///< 允许写入的颜色通道。
};

/// 整个管线的混合状态，attachments 数量应与 colorFormats 数量一致。
struct RHIBlendState {
    bool logicOpEnable = false; ///< 是否启用逻辑运算，启用时通常不能同时使用普通 blending。
    RHILogicOp logicOp = RHILogicOp::Copy; ///< 逻辑运算类型。
    std::array<float, 4> blendConstants{0.0F, 0.0F, 0.0F, 0.0F}; ///< 常量混合颜色。
    std::vector<RHIColorBlendAttachmentState> attachments; ///< 每个 color attachment 的混合设置。
};

/// 图形管线完整描述。后端可以用它生成 PSO/pipeline，并缓存相同描述。
struct RHIGraphicsPipelineDesc {
    std::string debugName; ///< 调试名称。
    RHIPipelineCache cache{}; ///< 可选管线缓存；无效句柄表示使用后端默认缓存策略。
    RHIPipelineLayout layout{}; ///< 资源绑定布局。
    std::vector<RHIShaderDesc> shaders; ///< 图形阶段 shader 列表，通常至少包含 vertex 和 fragment。
    std::vector<RHIShaderSpecializationConstant> specializationConstants; ///< 管线创建时固化的 shader 常量。
    std::vector<RHIVertexBufferLayoutDesc> vertexBuffers; ///< 顶点输入布局。
    RHIInputAssemblyState inputAssembly{}; ///< 输入装配状态。
    RHIRasterState raster{}; ///< 光栅化状态。
    RHIDepthStencilState depthStencil{}; ///< 深度模板状态。
    RHIMultisampleState multisample{}; ///< MSAA 状态。
    RHIBlendState blend{}; ///< 颜色混合状态。
    std::vector<RHIDynamicState> dynamicStates{RHIDynamicState::RHIViewport, RHIDynamicState::Scissor}; ///< 命令录制时动态设置的状态。
    std::vector<RHIFormat> colorFormats; ///< color attachment 格式列表；动态渲染后端可直接使用。
    RHIFormat depthStencilFormat = RHIFormat::Undefined; ///< depth-stencil attachment 格式，没有深度则为 Undefined。
    RHIRenderPass compatibleRenderPass{}; ///< 传统 render pass 后端的兼容 render pass。
    u32 subpass = 0; ///< 使用 compatibleRenderPass 时的 subpass index。
};

/// 计算管线描述。
struct RHIComputePipelineDesc {
    std::string debugName; ///< 调试名称。
    RHIPipelineCache cache{}; ///< 可选管线缓存；无效句柄表示使用后端默认缓存策略。
    RHIPipelineLayout layout{}; ///< 资源绑定布局。
    RHIShaderDesc shader{}; ///< compute shader。
    std::vector<RHIShaderSpecializationConstant> specializationConstants; ///< 管线创建时固化的 shader 常量。
};

/// 管线缓存描述。后端可以把 initialData 解释为上次保存的 pipeline cache blob。
struct RHIPipelineCacheDesc {
    std::string debugName; ///< 调试名称。
    std::vector<std::byte> initialData; ///< 初始缓存数据，空表示新建。
    bool allowSerialization = true; ///< 是否允许在退出时导出缓存数据。
};

/// GPU 查询类型。
enum class RHIQueryType : u8 {
    Timestamp, ///< GPU 时间戳查询，用于测量 pass 或命令区间的 GPU 执行时间。
    Occlusion, ///< 遮挡查询，用于统计通过深度/模板测试的样本数或可见性。
    PipelineStatistics ///< 管线统计查询，用于统计 shader 调用、图元数量等性能计数。
};

/// pipeline statistics 查询的统计项位。
enum class RHIPipelineStatisticFlags : u32 {
    None = 0, ///< 不启用任何管线统计项。
    InputAssemblyVertices = 1u << 0, ///< 统计输入装配阶段读取的顶点数量。
    InputAssemblyPrimitives = 1u << 1, ///< 统计输入装配阶段生成的图元数量。
    VertexShaderInvocations = 1u << 2, ///< 统计 vertex shader 调用次数。
    GeometryShaderInvocations = 1u << 3, ///< 统计 geometry shader 调用次数。
    GeometryShaderPrimitives = 1u << 4, ///< 统计 geometry shader 输出的图元数量。
    ClippingInvocations = 1u << 5, ///< 统计进入裁剪阶段的图元数量。
    ClippingPrimitives = 1u << 6, ///< 统计通过裁剪并继续光栅化的图元数量。
    FragmentShaderInvocations = 1u << 7, ///< 统计 fragment/pixel shader 调用次数。
    TessControlShaderPatches = 1u << 8, ///< 统计 tessellation control shader 处理的 patch 数量。
    TessEvaluationShaderInvocations = 1u << 9, ///< 统计 tessellation evaluation shader 调用次数。
    ComputeShaderInvocations = 1u << 10 ///< 统计 compute shader invocation 数量。
};

[[nodiscard]] constexpr RHIPipelineStatisticFlags operator|(RHIPipelineStatisticFlags lhs, RHIPipelineStatisticFlags rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHIPipelineStatisticFlags operator&(RHIPipelineStatisticFlags lhs, RHIPipelineStatisticFlags rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHIPipelineStatisticFlags& operator|=(RHIPipelineStatisticFlags& lhs, RHIPipelineStatisticFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// 查询池描述。timestamp 用于 GPU 时间，occlusion 用于遮挡查询。
struct RHIQueryPoolDesc {
    std::string debugName; ///< 调试名称。
    RHIQueryType type = RHIQueryType::Timestamp; ///< 查询类型。
    u32 queryCount = 1; ///< 查询槽数量。
    RHIPipelineStatisticFlags statistics = RHIPipelineStatisticFlags::None; ///< PipelineStatistics 查询时需要的统计项。
};

/// 渲染开始时 attachment 内容如何处理。
enum class RHILoadOp : u8 {
    Load, ///< pass 开始时保留并读取 attachment 原有内容。
    Clear, ///< pass 开始时使用 clear value 清除 attachment。
    DontCare ///< pass 开始时不关心 attachment 原有内容，后端可丢弃以节省带宽。
};

/// 渲染结束时 attachment 内容如何处理。
enum class RHIStoreOp : u8 {
    Store, ///< pass 结束时保留 attachment 内容，供后续 pass、采样或呈现使用。
    DontCare ///< pass 结束时不需要保留 attachment 内容，后端可丢弃或不写回内存。
};

/// MSAA resolve 行为。Average 是最常见的颜色 resolve；深度 resolve 后端支持差异较大。
enum class RHIResolveMode : u8 {
    None, ///< 不执行 resolve，MSAA attachment 内容保持多采样形式。
    Average, ///< 对多个 sample 求平均，最常见的颜色 MSAA resolve 模式。
    Min, ///< 取多个 sample 的最小值，常用于特定深度 resolve 策略。
    Max, ///< 取多个 sample 的最大值，常用于 reversed-Z 或特殊深度 resolve 策略。
    SampleZero ///< 直接取第 0 个 sample，成本低但质量取决于采样位置。
};

/// 单个 color render target 的绑定和 load/store 行为。
struct RHIRenderTargetDesc {
    RHITextureView view{}; ///< color attachment view。
    RHITextureView resolveView{}; ///< MSAA resolve 目标；无效句柄表示不 resolve。
    RHIResolveMode resolveMode = RHIResolveMode::None; ///< resolve 行为。
    RHILoadOp loadOp = RHILoadOp::Clear; ///< pass 开始时如何处理已有内容。
    RHIStoreOp storeOp = RHIStoreOp::Store; ///< pass 结束时是否保留结果。
    RHIClearColor clearColor{}; ///< loadOp 为 Clear 时使用的清屏颜色。
    RHIResourceState stateBefore = RHIResourceState::Undefined; ///< pass 开始前期望状态。
    RHIResourceState stateAfter = RHIResourceState::RenderTarget; ///< pass 结束后目标状态。
};

/// depth-stencil attachment 的绑定和 load/store 行为。
struct RHIDepthStencilTargetDesc {
    RHITextureView view{}; ///< depth-stencil attachment view。
    RHITextureView depthResolveView{}; ///< depth resolve 目标；无效句柄表示不 resolve。
    RHITextureView stencilResolveView{}; ///< stencil resolve 目标；无效句柄表示不 resolve。
    RHIResolveMode depthResolveMode = RHIResolveMode::None; ///< depth resolve 行为。
    RHIResolveMode stencilResolveMode = RHIResolveMode::None; ///< stencil resolve 行为。
    RHILoadOp depthLoadOp = RHILoadOp::Clear; ///< depth pass 开始行为。
    RHIStoreOp depthStoreOp = RHIStoreOp::Store; ///< depth pass 结束行为。
    RHILoadOp stencilLoadOp = RHILoadOp::DontCare; ///< stencil pass 开始行为。
    RHIStoreOp stencilStoreOp = RHIStoreOp::DontCare; ///< stencil pass 结束行为。
    RHIClearDepthStencil clearValue{}; ///< 清除 depth/stencil 时使用的值。
    RHIResourceState stateBefore = RHIResourceState::Undefined; ///< pass 开始前期望状态。
    RHIResourceState stateAfter = RHIResourceState::DepthWrite; ///< pass 结束后目标状态。
};

/// 一次 render pass 的附件集合和渲染区域。
struct RHIRenderPassDesc {
    std::string debugName; ///< 调试名称。
    RHIRect2D renderArea{}; ///< 渲染影响的区域，一般等于 framebuffer 尺寸。
    std::vector<RHIRenderTargetDesc> colorTargets; ///< color attachments。
    std::optional<RHIDepthStencilTargetDesc> depthStencilTarget; ///< 可选 depth-stencil attachment。
};

/// framebuffer 描述，一般用于传统 render pass API；动态渲染后端可把它当附件集合缓存。
struct RHIFramebufferDesc {
    std::string debugName; ///< 调试名称。
    RHIRenderPass renderPass{}; ///< 兼容的 render pass。
    std::vector<RHITextureView> attachments; ///< 实际绑定的附件 view。
    RHIExtent2D extent{}; ///< framebuffer 宽高。
    u32 layers = 1; ///< framebuffer 层数。
};

/// swapchain 色彩空间。后端需要根据平台支持选择最接近的实际色彩空间。
enum class RHIColorSpace : u8 {
    SRGBNonlinear, ///< 标准 sRGB 非线性色彩空间，普通 SDR swapchain 默认选择。
    DisplayP3Nonlinear, ///< Display P3 非线性色彩空间，适合广色域 SDR 输出。
    ExtendedSRGBLinear, ///< 扩展 sRGB 线性色彩空间，适合宽范围线性颜色输出。
    HDR10ST2084, ///< HDR10 PQ/ST2084 色彩空间，适合 HDR10 显示链路。
    HDR10HLG ///< HDR HLG 色彩空间，适合广播或 HLG HDR 输出。
};

/// swapchain 输出表面变换。移动端或可旋转窗口系统可能会用到。
enum class RHISurfaceTransform : u8 {
    Identity, ///< 不对 swapchain 输出做额外旋转或镜像。
    Rotate90, ///< 输出图像顺时针旋转 90 度后呈现。
    Rotate180, ///< 输出图像旋转 180 度后呈现。
    Rotate270, ///< 输出图像顺时针旋转 270 度后呈现。
    HorizontalMirror, ///< 输出图像水平镜像后呈现。
    HorizontalMirrorRotate90, ///< 输出图像水平镜像并旋转 90 度后呈现。
    HorizontalMirrorRotate180, ///< 输出图像水平镜像并旋转 180 度后呈现。
    HorizontalMirrorRotate270, ///< 输出图像水平镜像并旋转 270 度后呈现。
    Inherit ///< 继承窗口系统当前 surface transform，由平台决定最终变换。
};

/// swapchain alpha 合成模式，用于透明窗口或系统 compositor。
enum class RHICompositeAlphaMode : u8 {
    Opaque, ///< swapchain alpha 被视为不透明，窗口内容不参与透明合成。
    PreMultiplied, ///< 颜色已预乘 alpha，交给系统 compositor 做预乘透明合成。
    PostMultiplied, ///< 颜色未预乘 alpha，交给系统 compositor 做后乘透明合成。
    Inherit ///< 继承窗口系统默认 alpha 合成模式。
};

/// 交换链描述。窗口系统相关 handle 不放在这里，由平台层交给具体后端。
struct RHISwapchainDesc {
    std::string debugName; ///< 调试名称。
    RHIExtent2D extent{}; ///< drawable 尺寸。
    RHIFormat preferredFormat = RHIFormat::BGRA8_SRGB; ///< 期望后备缓冲格式，后端可根据平台支持选择最接近格式。
    RHIColorSpace colorSpace = RHIColorSpace::SRGBNonlinear; ///< 期望色彩空间。
    RHIPresentMode presentMode = RHIPresentMode::FIFO; ///< 呈现模式。
    u32 imageCount = 2; ///< swapchain image 数量，常见为 2 或 3。
    RHISurfaceTransform preTransform = RHISurfaceTransform::Identity; ///< 呈现前表面变换。
    RHICompositeAlphaMode compositeAlpha = RHICompositeAlphaMode::Opaque; ///< 与系统 compositor 的 alpha 合成方式。
    bool allowTearing = false; ///< 是否允许无垂直同步撕裂显示，平台不支持时应忽略。
    bool fullscreen = false; ///< 是否请求独占或无边框全屏，具体由平台层实现。
    bool hdr = false; ///< 是否请求 HDR 输出，后端需要结合 format/colorSpace 判断。
};

/// texture 子资源范围，用于 barrier、copy、view 等操作。
struct RHITextureSubresourceRange {
    RHITextureAspect aspect = RHITextureAspect::All; ///< 覆盖的 aspect，All 表示由格式自动推导。
    u32 baseMipLevel = 0; ///< 起始 mip。
    u32 mipLevelCount = 1; ///< mip 数量。
    u32 baseArrayLayer = 0; ///< 起始数组层。
    u32 arrayLayerCount = 1; ///< 数组层数量。
};

/// 全局内存 barrier，用于没有特定资源但需要约束前后访问顺序的情况。
struct RHIGlobalBarrier {
    RHIPipelineStage sourceStages = RHIPipelineStage::AllCommands; ///< barrier 前需要等待的管线阶段。
    RHIPipelineStage destinationStages = RHIPipelineStage::AllCommands; ///< barrier 后允许继续的管线阶段。
    RHIAccessFlags sourceAccess = RHIAccessFlags::MemoryWrite; ///< barrier 前的访问类型。
    RHIAccessFlags destinationAccess = RHIAccessFlags::MemoryRead; ///< barrier 后的访问类型。
};

/// texture 状态转换请求。显式 API 会翻译成 image barrier。
struct RHITextureBarrier {
    RHITexture texture{}; ///< 目标 texture。
    RHITextureSubresourceRange range{}; ///< 需要转换的子资源范围。
    RHIResourceState before = RHIResourceState::Undefined; ///< 转换前状态。
    RHIResourceState after = RHIResourceState::Common; ///< 转换后状态。
    RHIPipelineStage sourceStages = RHIPipelineStage::AllCommands; ///< 转换前需要等待的管线阶段。
    RHIPipelineStage destinationStages = RHIPipelineStage::AllCommands; ///< 转换后可继续的管线阶段。
    RHIAccessFlags sourceAccess = RHIAccessFlags::None; ///< 转换前资源访问类型；None 表示由 RHIResourceState 推导。
    RHIAccessFlags destinationAccess = RHIAccessFlags::None; ///< 转换后资源访问类型；None 表示由 RHIResourceState 推导。
    RHIQueueType sourceQueue = RHIQueueType::Graphics; ///< 源队列所有权。
    RHIQueueType destinationQueue = RHIQueueType::Graphics; ///< 目标队列所有权，跨队列时后端需要 ownership transfer。
    bool discardContents = false; ///< true 表示旧内容不需要保留，后端可使用更便宜的布局转换。
};

/// buffer 状态转换请求。显式 API 会翻译成 buffer/resource barrier。
struct RHIBufferBarrier {
    RHIBuffer buffer{}; ///< 目标 buffer。
    u64 offset = 0; ///< 起始字节偏移。
    u64 size = RHI_WHOLE_SIZE; ///< 覆盖字节范围。
    RHIResourceState before = RHIResourceState::Undefined; ///< 转换前状态。
    RHIResourceState after = RHIResourceState::Common; ///< 转换后状态。
    RHIPipelineStage sourceStages = RHIPipelineStage::AllCommands; ///< 转换前需要等待的管线阶段。
    RHIPipelineStage destinationStages = RHIPipelineStage::AllCommands; ///< 转换后可继续的管线阶段。
    RHIAccessFlags sourceAccess = RHIAccessFlags::None; ///< 转换前资源访问类型；None 表示由 RHIResourceState 推导。
    RHIAccessFlags destinationAccess = RHIAccessFlags::None; ///< 转换后资源访问类型；None 表示由 RHIResourceState 推导。
    RHIQueueType sourceQueue = RHIQueueType::Graphics; ///< 源队列所有权。
    RHIQueueType destinationQueue = RHIQueueType::Graphics; ///< 目标队列所有权。
};

/// 一批资源 barrier，通常在 pass 开始前或 copy/dispatch/draw 之间提交。
struct RHIResourceBarriers {
    std::vector<RHIGlobalBarrier> globals; ///< 全局内存 barriers。
    std::vector<RHITextureBarrier> textures; ///< texture barriers。
    std::vector<RHIBufferBarrier> buffers; ///< buffer barriers。
};

/// CPU 到 GPU buffer 上传请求。后端负责 staging buffer 和 copy command。
struct RHIBufferUploadDesc {
    RHIBuffer destination{}; ///< 目标 buffer。
    u64 destinationOffset = 0; ///< 写入目标 buffer 的字节偏移。
    std::vector<std::byte> data; ///< 待上传的原始字节。
};

/// CPU 到 GPU texture 上传请求。行对齐要求由后端处理。
struct RHITextureUploadDesc {
    RHITexture destination{}; ///< 目标 texture。
    u32 mipLevel = 0; ///< 目标 mip。
    u32 arrayLayer = 0; ///< 目标数组层。
    RHIOffset3D offset{}; ///< 写入区域偏移。
    RHIExtent3D extent{}; ///< 写入区域尺寸。
    u64 bytesPerRow = 0; ///< 源数据每行字节数；0 表示后端按格式和宽度推导。
    u64 rowsPerImage = 0; ///< 3D/array 数据每张 image 行数；0 表示后端推导。
    std::vector<std::byte> data; ///< 待上传的原始像素字节。
};

/// 一帧或一次加载阶段的上传请求集合。
struct RHIUploadBatchDesc {
    std::vector<RHIBufferUploadDesc> buffers; ///< buffer 上传列表。
    std::vector<RHITextureUploadDesc> textures; ///< texture 上传列表。
};

/// buffer 到 buffer 的拷贝区域。
struct RHIBufferCopyDesc {
    RHIBuffer source{}; ///< 源 buffer。
    RHIBuffer destination{}; ///< 目标 buffer。
    u64 sourceOffset = 0; ///< 源字节偏移。
    u64 destinationOffset = 0; ///< 目标字节偏移。
    u64 size = 0; ///< 拷贝字节数。
};

/// texture 拷贝位置，包含 texture 和具体子资源。
struct RHITextureCopyLocation {
    RHITexture texture{}; ///< 目标或源 texture。
    RHITextureAspect aspect = RHITextureAspect::Color; ///< 拷贝的 aspect。
    u32 mipLevel = 0; ///< mip 层。
    u32 arrayLayer = 0; ///< 数组层。
    RHIOffset3D offset{}; ///< 子资源内偏移。
};

/// texture 到 texture 的拷贝区域。
struct RHITextureCopyDesc {
    RHITextureCopyLocation source{}; ///< 源 texture 位置。
    RHITextureCopyLocation destination{}; ///< 目标 texture 位置。
    RHIExtent3D extent{}; ///< 拷贝尺寸。
};

/// buffer 和 texture 之间的拷贝区域。
struct RHIBufferTextureCopyDesc {
    RHIBuffer buffer{}; ///< 参与拷贝的 buffer。
    RHITextureCopyLocation texture{}; ///< 参与拷贝的 texture 位置。
    u64 bufferOffset = 0; ///< buffer 起始字节偏移。
    u64 bytesPerRow = 0; ///< buffer 中每行字节数；0 表示后端按格式推导。
    u64 rowsPerImage = 0; ///< buffer 中每张 image 行数；0 表示后端推导。
    RHIExtent3D extent{}; ///< 拷贝尺寸。
};

/// texture blit 区域，可用于缩放、格式兼容转换或 mipmap 生成。
struct RHITextureBlitDesc {
    RHITextureCopyLocation source{}; ///< 源 texture 位置。
    RHITextureCopyLocation destination{}; ///< 目标 texture 位置。
    RHIExtent3D sourceExtent{}; ///< 源区域尺寸。
    RHIExtent3D destinationExtent{}; ///< 目标区域尺寸。
    RHIFilterMode filter = RHIFilterMode::Linear; ///< 缩放过滤方式。
};

/// 自动生成 mipmap 的请求。
struct RHIMipmapGenerationDesc {
    RHITexture texture{}; ///< 目标 texture。
    RHITextureAspect aspect = RHITextureAspect::Color; ///< 生成哪个 aspect 的 mip。
    u32 baseArrayLayer = 0; ///< 起始数组层。
    u32 arrayLayerCount = 1; ///< 数组层数量。
    RHIFilterMode filter = RHIFilterMode::Linear; ///< 下采样过滤方式。
};

/// index buffer 中索引元素的整数类型。
enum class RHIIndexType : u8 {
    UInt16, ///< 16 位无符号索引，节省显存和带宽，单个 draw 可引用最多 65536 个顶点。
    UInt32 ///< 32 位无符号索引，支持大型 mesh，显存和带宽成本高于 UInt16。
};

/// draw call 绑定的单个 vertex stream。
struct RHIVertexStream {
    RHIBuffer buffer{}; ///< 顶点 buffer。
    u32 binding = 0; ///< 对应 RHIVertexBufferLayoutDesc::binding。
    u64 offset = 0; ///< buffer 起始字节偏移。
    u64 stride = 0; ///< 运行期 stride；0 表示使用 pipeline layout 中的 stride。
};

/// draw indexed 使用的 index stream。
struct RHIIndexStream {
    RHIBuffer buffer{}; ///< 索引 buffer。
    RHIIndexType indexType = RHIIndexType::UInt32; ///< 索引元素类型。
    u64 offset = 0; ///< 起始字节偏移。
    u32 indexCount = 0; ///< 可读取索引数量，便于验证 draw 范围。
};

/// 轴对齐包围盒，用于视锥裁剪、光源裁剪和加速结构构建。
struct RHIBoundingBox {
    glm::vec3 min{0.0F}; ///< 最小点。
    glm::vec3 max{0.0F}; ///< 最大点。
};

/// 包围球，用于更便宜的粗裁剪或 LOD 选择。
struct RHIBoundingSphere {
    glm::vec3 center{0.0F}; ///< 球心。
    float radius = 0.0F; ///< 半径。
};

/// mesh 内的一个可独立绘制部分。
struct RHISubmeshDesc {
    std::string name; ///< 子网格名称。
    u32 firstVertex = 0; ///< 非索引绘制的起始顶点。
    u32 vertexCount = 0; ///< 顶点数量。
    u32 firstIndex = 0; ///< 索引绘制的起始索引。
    u32 indexCount = 0; ///< 索引数量。
    u32 firstInstance = 0; ///< 默认起始实例。
    u32 instanceCount = 1; ///< 默认实例数量。
    i32 materialIndex = -1; ///< 默认材质索引，-1 表示未指定。
    glm::vec3 boundsMin{0.0F}; ///< 本地空间 AABB 最小点。
    glm::vec3 boundsMax{0.0F}; ///< 本地空间 AABB 最大点。
    RHIBoundingSphere boundsSphere{}; ///< 本地空间包围球。
};

/// mesh 资源描述，只引用 GPU buffer，不保存 CPU 顶点数组。
struct RHIMeshDesc {
    std::string debugName; ///< 调试名称。
    std::vector<RHIVertexStream> vertexStreams; ///< mesh 默认顶点流。
    std::optional<RHIIndexStream> indexStream; ///< 可选索引流。
    std::vector<RHISubmeshDesc> submeshes; ///< 子网格列表。
};

/// 材质中按名字记录的贴图槽，便于编辑器和材质系统做参数管理。
struct RHITextureSlot {
    std::string name; ///< 槽位名，例如 baseColor、normal、metallicRoughness。
    RHITextureView texture{}; ///< 绑定的贴图 view。
    RHISampler sampler{}; ///< 贴图采样器。
};

/// 材质参数类型，用于编辑器和自动打包 uniform/push constant 数据。
enum class RHIMaterialParameterType : u8 {
    Float, ///< 单个浮点参数，存放在 value.x，例如 roughness、metallic、exposure。
    Float2, ///< 二维浮点参数，存放在 value.xy，例如 tiling、offset 或屏幕尺寸。
    Float3, ///< 三维浮点参数，存放在 value.xyz，例如颜色、方向或位置。
    Float4, ///< 四维浮点参数，使用完整 value，例如 RGBA、向量或 packed 参数。
    Int, ///< 有符号整数参数，通过 value.x 转换读取，适合模式枚举或索引。
    UInt, ///< 无符号整数参数，通过 value.x 转换读取，适合 bitmask、ID 或计数。
    Bool ///< 布尔参数，通过 value.x 是否非 0 读取，适合开关型材质选项。
};

/// 命名材质参数。数值统一存放在 value 中，具体读取方式由 type 决定。
struct RHIMaterialParameter {
    std::string name; ///< 参数名，例如 roughness、metallic、baseColorFactor。
    RHIMaterialParameterType type = RHIMaterialParameterType::Float4; ///< 参数类型。
    glm::vec4 value{0.0F}; ///< 参数值，标量使用 x，bool 使用 x 是否非 0。
};

/// 材质描述，表示“用哪个 pipeline + 哪些资源参数绘制”。
struct RHIMaterialDesc {
    std::string debugName; ///< 调试名称。
    RHIPipeline pipeline{}; ///< 材质默认管线。
    std::vector<RHIBindGroup> bindGroups; ///< 材质级资源绑定组。
    std::vector<RHITextureSlot> textureSlots; ///< 材质贴图槽。
    std::vector<RHIMaterialParameter> parameters; ///< 命名材质参数。
    std::vector<std::byte> pushConstantData; ///< 材质 push constant/root constant 数据。
};

/// 非索引绘制命令。通常由可见性裁剪和排序阶段生成。
struct RHIDrawCommand {
    RHIPipeline pipeline{}; ///< 使用的图形管线。
    std::vector<RHIBindGroup> bindGroups; ///< 绘制前需要绑定的资源组。
    std::vector<RHIVertexStream> vertexStreams; ///< 本次绘制的顶点流，可覆盖 mesh 默认流。
    u32 vertexCount = 0; ///< 绘制顶点数。
    u32 instanceCount = 1; ///< 实例数量。
    u32 firstVertex = 0; ///< 起始顶点。
    u32 firstInstance = 0; ///< 起始实例。
};

/// 索引绘制命令。
struct RHIDrawIndexedCommand {
    RHIPipeline pipeline{}; ///< 使用的图形管线。
    std::vector<RHIBindGroup> bindGroups; ///< 绘制前需要绑定的资源组。
    std::vector<RHIVertexStream> vertexStreams; ///< 本次绘制的顶点流。
    RHIIndexStream indexStream{}; ///< 索引流。
    u32 indexCount = 0; ///< 绘制索引数。
    u32 instanceCount = 1; ///< 实例数量。
    u32 firstIndex = 0; ///< 起始索引。
    i32 vertexOffsetElements = 0; ///< 索引值加上的顶点偏移，单位是顶点不是字节。
    u32 firstInstance = 0; ///< 起始实例。
};

/// compute dispatch 命令。
struct RHIDispatchCommand {
    RHIPipeline pipeline{}; ///< 使用的计算管线。
    std::vector<RHIBindGroup> bindGroups; ///< dispatch 前需要绑定的资源组。
    u32 groupCountX = 1; ///< X 方向 workgroup 数量。
    u32 groupCountY = 1; ///< Y 方向 workgroup 数量。
    u32 groupCountZ = 1; ///< Z 方向 workgroup 数量。
};

/// 间接非索引绘制命令，参数从 GPU buffer 读取。
struct RHIDrawIndirectCommand {
    RHIPipeline pipeline{}; ///< 使用的图形管线。
    std::vector<RHIBindGroup> bindGroups; ///< 绘制前需要绑定的资源组。
    std::vector<RHIVertexStream> vertexStreams; ///< 本次绘制的顶点流。
    RHIBuffer argumentBuffer{}; ///< 间接参数 buffer。
    u64 argumentOffset = 0; ///< 第一个间接参数的字节偏移。
    u32 drawCount = 1; ///< 执行的间接绘制数量。
    u32 stride = 0; ///< 每个间接参数结构的字节跨度；0 表示使用后端默认结构大小。
    RHIBuffer countBuffer{}; ///< 可选 draw count buffer；无效句柄表示使用 drawCount。
    u64 countBufferOffset = 0; ///< countBuffer 中 draw count 的字节偏移。
};

/// 间接索引绘制命令。
struct RHIDrawIndexedIndirectCommand {
    RHIPipeline pipeline{}; ///< 使用的图形管线。
    std::vector<RHIBindGroup> bindGroups; ///< 绘制前需要绑定的资源组。
    std::vector<RHIVertexStream> vertexStreams; ///< 本次绘制的顶点流。
    RHIIndexStream indexStream{}; ///< 索引流。
    RHIBuffer argumentBuffer{}; ///< 间接参数 buffer。
    u64 argumentOffset = 0; ///< 第一个间接参数的字节偏移。
    u32 drawCount = 1; ///< 执行的间接绘制数量。
    u32 stride = 0; ///< 每个间接参数结构的字节跨度；0 表示使用后端默认结构大小。
    RHIBuffer countBuffer{}; ///< 可选 draw count buffer；无效句柄表示使用 drawCount。
    u64 countBufferOffset = 0; ///< countBuffer 中 draw count 的字节偏移。
};

/// 间接 compute dispatch 命令，groupCount 从 GPU buffer 读取。
struct RHIDispatchIndirectCommand {
    RHIPipeline pipeline{}; ///< 使用的计算管线。
    std::vector<RHIBindGroup> bindGroups; ///< dispatch 前需要绑定的资源组。
    RHIBuffer argumentBuffer{}; ///< 间接参数 buffer。
    u64 argumentOffset = 0; ///< 参数字节偏移。
};

/// GPU 调试标记，用于 RenderDoc、PIX、Xcode GPU Frame Debugger 等工具分组显示。
struct RHIDebugMarkerDesc {
    std::string name; ///< 标记名称。
    std::array<float, 4> color{0.2F, 0.6F, 1.0F, 1.0F}; ///< 标记颜色。
};

/// 写入 timestamp 查询的命令。
struct RHITimestampQueryCommand {
    RHIQueryPool queryPool{}; ///< timestamp 查询池。
    u32 queryIndex = 0; ///< 写入的查询槽。
    RHIPipelineStage stage = RHIPipelineStage::BottomOfPipe; ///< 记录时间戳的管线阶段。
};

/// 重置查询范围的命令。
struct RHIResetQueryCommand {
    RHIQueryPool queryPool{}; ///< 查询池。
    u32 firstQuery = 0; ///< 起始查询槽。
    u32 queryCount = 1; ///< 查询数量。
};

/// 拷贝查询结果到 buffer 的命令，便于 CPU 或后续 GPU pass 读取。
struct RHIResolveQueryCommand {
    RHIQueryPool queryPool{}; ///< 查询池。
    u32 firstQuery = 0; ///< 起始查询槽。
    u32 queryCount = 1; ///< 查询数量。
    RHIBuffer destination{}; ///< 结果写入的 buffer。
    u64 destinationOffset = 0; ///< 目标字节偏移。
    u64 stride = sizeof(u64); ///< 每个查询结果的字节跨度。
    bool waitForResults = true; ///< 是否等待查询结果可用。
};

/// 一个 render graph pass 对应的实际工作负载。
struct RHIRenderPassWorkload {
    std::string passName; ///< 对应 RHIRenderGraphPassDesc::name，用名字关联 pass 描述和命令。
    RHIViewport viewport{}; ///< 本 pass 默认 viewport。
    RHIRect2D scissor{}; ///< 本 pass 默认 scissor。
    RHIResourceBarriers barriers; ///< pass 内需要显式插入的额外 barrier。
    std::vector<RHIDebugMarkerDesc> debugMarkers; ///< pass 内调试标记；后端可按顺序插入 begin/end marker。
    std::vector<RHIBufferCopyDesc> bufferCopies; ///< buffer 到 buffer 拷贝命令。
    std::vector<RHITextureCopyDesc> textureCopies; ///< texture 到 texture 拷贝命令。
    std::vector<RHIBufferTextureCopyDesc> bufferToTextureCopies; ///< buffer 到 texture 拷贝命令。
    std::vector<RHIBufferTextureCopyDesc> textureToBufferCopies; ///< texture 到 buffer 拷贝命令。
    std::vector<RHITextureBlitDesc> textureBlits; ///< texture blit 命令。
    std::vector<RHIMipmapGenerationDesc> mipmapGenerations; ///< mipmap 生成命令。
    std::vector<RHIResetQueryCommand> queryResets; ///< 查询重置命令。
    std::vector<RHITimestampQueryCommand> timestampWrites; ///< timestamp 写入命令。
    std::vector<RHIResolveQueryCommand> queryResolves; ///< 查询结果拷贝命令。
    std::vector<RHIDrawCommand> draws; ///< 非索引绘制列表。
    std::vector<RHIDrawIndexedCommand> indexedDraws; ///< 索引绘制列表。
    std::vector<RHIDrawIndirectCommand> indirectDraws; ///< 间接非索引绘制列表。
    std::vector<RHIDrawIndexedIndirectCommand> indexedIndirectDraws; ///< 间接索引绘制列表。
    std::vector<RHIDispatchCommand> dispatches; ///< 计算 dispatch 列表。
    std::vector<RHIDispatchIndirectCommand> indirectDispatches; ///< 间接计算 dispatch 列表。
};

/// GPU-wait-GPU 信号类型。Timeline 使用递增计数，Binary 表达一次依赖。
enum class RHIGPUWaitGPUSignalType : u8 {
    Binary, ///< 二值 GPU 信号，只表达一次 wait/signal，用于单个提交或 present 依赖。
    Timeline ///< 时间线 GPU 信号，使用递增 u64 值表达多个 GPU 提交之间的有序同步。
};

/// GPU-wait-GPU 信号创建描述。
struct RHIGPUWaitGPUSignalDesc {
    std::string debugName; ///< 调试名称。
    RHIGPUWaitGPUSignalType type = RHIGPUWaitGPUSignalType::Binary; ///< 同步对象类型。
    u64 initialValue = 0; ///< Timeline 信号初始值，Binary 信号忽略。
};

/// CPU-wait-GPU 信号创建描述，用于 CPU 等待某次 GPU 提交完成。
struct RHICPUWaitGPUSignalDesc {
    std::string debugName; ///< 调试名称。
    bool signaled = false; ///< 创建时是否为已触发状态。
};

/// 队列提交前等待的 GPU 信号。
struct RHIQueueWaitDesc {
    RHIGPUWaitGPUSignal signal{}; ///< 需要等待的 GPU-wait-GPU 信号。
    u64 value = 0; ///< Timeline 等待值；Binary 信号忽略。
    RHIPipelineStage stages = RHIPipelineStage::AllCommands; ///< 等待影响的管线阶段。
};

/// 队列提交完成后触发的 GPU 信号。
struct RHIQueueSignalDesc {
    RHIGPUWaitGPUSignal signal{}; ///< 提交完成后触发的 GPU-wait-GPU 信号。
    u64 value = 0; ///< Timeline 触发值；Binary 信号忽略。
};

/// 一次队列提交描述。后端可把 passNames 映射到实际 command buffer 列表。
struct RHIQueueSubmitDesc {
    std::string debugName; ///< 调试名称。
    RHIQueueType queue = RHIQueueType::Graphics; ///< 提交到哪类队列。
    std::vector<std::string> passNames; ///< 本次提交包含的 RenderGraph pass 名称。
    std::vector<RHIQueueWaitDesc> waits; ///< 提交前等待的同步对象。
    std::vector<RHIQueueSignalDesc> signals; ///< 提交完成后 signal 的同步对象。
    RHICPUWaitGPUSignal cpuWaitGPUSignal{}; ///< 可选 CPU-wait-GPU 信号，用于 CPU 等待提交完成。
};

/// 呈现请求。窗口系统原生 surface 仍由平台层和后端保存。
struct RHIPresentDesc {
    RHISwapchain swapchain{}; ///< 目标 swapchain。
    u32 imageIndex = 0; ///< 要呈现的 swapchain image 下标。
    std::vector<RHIGPUWaitGPUSignal> waitSignals; ///< present 前需要等待的 GPU-wait-GPU 信号。
    RHIPresentMode presentMode = RHIPresentMode::FIFO; ///< 本次呈现期望模式，后端可按 swapchain 实际模式处理。
    bool allowTearing = false; ///< 本次呈现是否允许 tearing。
};

/// 物体变换数据，保留上一帧矩阵可用于 motion vector/TAA。
struct RHITransformData {
    glm::mat4 localToWorld{1.0F}; ///< 当前帧本地到世界矩阵。
    glm::mat4 previousLocalToWorld{1.0F}; ///< 上一帧本地到世界矩阵。
};

/// 相机数据。投影矩阵的坐标系差异应由创建矩阵或后端适配层统一处理。
struct RHICameraData {
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
enum class RHILightType : u8 {
    Directional, ///< 方向光，只有方向无位置，适合太阳光等无限远平行光源。
    Point, ///< 点光源，从世界位置向四周发光，受 range 控制影响半径。
    Spot ///< 聚光灯，从位置沿方向发光，并由内外锥角控制照射范围。
};

/// CPU 侧光源描述，后续可打包进 uniform/storage buffer。
struct RHILightData {
    RHILightType type = RHILightType::Directional; ///< 光源类型。
    glm::vec3 color{1.0F}; ///< 线性空间颜色。
    float intensity = 1.0F; ///< 光强，具体物理单位由渲染器约定。
    glm::vec3 direction{0.0F, -1.0F, 0.0F}; ///< 方向光/聚光灯方向。
    float range = 10.0F; ///< 点光/聚光影响半径。
    glm::vec3 position{0.0F}; ///< 点光/聚光世界位置。
    float innerConeRadians = 0.0F; ///< 聚光内锥角，单位弧度。
    float outerConeRadians = 0.7853981634F; ///< 聚光外锥角，默认约 45 度。
};

/// 渲染队列，用于粗粒度排序和选择 pass。
enum class RHIRenderQueue : u8 {
    Background, ///< 背景队列，通常最先绘制 sky、远景或清屏后背景内容。
    Opaque, ///< 不透明队列，通常按材质/管线优化排序并开启深度写入。
    AlphaTest, ///< Alpha 裁剪队列，用于植被、栅栏等需要 discard 但仍可深度写入的物体。
    Transparent, ///< 半透明队列，通常关闭深度写入并按深度从远到近排序。
    Overlay ///< 叠加队列，用于 UI、调试绘制、gizmo 或最后覆盖到画面的内容。
};

/// 场景中的一个可渲染物体实例。
struct RHIRenderObjectDesc {
    std::string debugName; ///< 调试名称。
    RHIMesh mesh{}; ///< 使用的 mesh。
    RHIMaterial material{}; ///< 使用的材质。
    RHITransformData transform{}; ///< 物体变换。
    u32 submeshIndex = 0; ///< 绘制 mesh 中的哪个 submesh。
    RHIRenderQueue queue = RHIRenderQueue::Opaque; ///< 渲染队列。
    u64 sortingKey = 0; ///< 精细排序 key，可编码 pipeline/material/depth 等信息。
    u32 layerMask = 0xFFFFFFFFu; ///< 渲染层掩码，供相机/pass 过滤。
    RHIBoundingBox worldBounds{}; ///< 世界空间包围盒，裁剪阶段可直接使用。
    RHIBoundingSphere worldBoundsSphere{}; ///< 世界空间包围球，粗裁剪和 LOD 可直接使用。
    bool visible = true; ///< 是否参与当前帧可见性处理。
    bool castsShadow = true; ///< 是否投射阴影。
    bool receivesShadow = true; ///< 是否接收阴影。
};

/// 场景环境参数，供 sky、IBL、曝光和后处理使用。
struct RHISceneEnvironmentDesc {
    glm::vec3 ambientColor{0.03F}; ///< 简单环境光颜色。
    float exposure = 1.0F; ///< 曝光倍率。
    RHITextureView skyTexture{}; ///< 可选天空贴图。
    RHITextureView irradianceTexture{}; ///< 可选漫反射 IBL。
    RHITextureView prefilteredReflectionTexture{}; ///< 可选预滤波反射 IBL。
    RHITextureView brdfLut{}; ///< 可选 BRDF LUT。
};

/// 当前帧参与渲染的相机集合。相机由相机系统/视图系统单独提交。
struct RHIRenderCameraSetDesc {
    RHICameraData main{}; ///< 主视图相机。
    std::vector<RHICameraData> additional; ///< 反射、调试视图、离屏渲染等额外相机。
};

/// 当前帧参与渲染的光源集合。光源由灯光系统单独提交。
struct RHIRenderLightSetDesc {
    std::vector<RHILightData> items; ///< 光源列表。
};

/// 当前帧参与渲染的物体集合。物体由场景/ECS 可见性阶段单独提交。
struct RHIRenderObjectSetDesc {
    std::vector<RHIRenderObjectDesc> items; ///< 可渲染物体列表。
};

/// RenderGraph 中声明的资源类型。
enum class RHIRenderGraphResourceType : u8 {
    Buffer, ///< RenderGraph 管理或引用的 buffer 资源。
    Texture, ///< RenderGraph 管理或引用的普通 texture/image 资源。
    SwapchainImage ///< 外部导入的 swapchain image，通常作为最终 present 目标。
};

/// RenderGraph 资源标志，用于别名、导入导出和临时资源优化。
enum class RHIRenderGraphResourceFlags : u32 {
    None = 0, ///< 无特殊 RenderGraph 行为，按普通内部资源处理。
    Imported = 1u << 0, ///< 资源由外部创建并传入 graph，graph 不负责创建和销毁。
    Exported = 1u << 1, ///< 资源结果需要在 graph 外继续使用，不能在最后一次内部读取后立即释放。
    Transient = 1u << 2, ///< 资源只在 graph 内短期使用，可参与池化和生命周期压缩。
    AllowAliasing = 1u << 3, ///< 允许与生命周期不重叠的其他资源共享底层内存。
    NeverCull = 1u << 4 ///< 即使看起来未被读取也不能裁剪，适合有调试、读回或外部副作用的资源。
};

[[nodiscard]] constexpr RHIRenderGraphResourceFlags operator|(RHIRenderGraphResourceFlags lhs, RHIRenderGraphResourceFlags rhs) noexcept {
    return RHIEnumBitOr(lhs, rhs);
}

[[nodiscard]] constexpr RHIRenderGraphResourceFlags operator&(RHIRenderGraphResourceFlags lhs, RHIRenderGraphResourceFlags rhs) noexcept {
    return RHIEnumBitAnd(lhs, rhs);
}

constexpr RHIRenderGraphResourceFlags& operator|=(RHIRenderGraphResourceFlags& lhs, RHIRenderGraphResourceFlags rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

/// pass 对某个 graph 资源的读写引用。
struct RHIRenderGraphResourceRef {
    std::string name; ///< graph 资源名，必须能在 RHIRenderGraphDesc 的资源列表中找到。
    RHIRenderGraphResourceType type = RHIRenderGraphResourceType::Texture; ///< 被引用资源类型。
    RHIResourceState state = RHIResourceState::ShaderRead; ///< pass 访问该资源时需要的状态。
    RHIPipelineStage stages = RHIPipelineStage::AllCommands; ///< 访问发生的管线阶段。
    RHIAccessFlags access = RHIAccessFlags::None; ///< 访问类型；None 表示由 state 推导。
};

/// RenderGraph 内部或外部导入的 buffer 声明。
struct RHIRenderGraphBufferDesc {
    std::string name; ///< graph 内唯一名称。
    RHIBufferDesc desc{}; ///< graph 创建内部 buffer 时使用的描述。
    RHIRenderGraphResourceFlags flags = RHIRenderGraphResourceFlags::None; ///< graph 资源标志。
    bool imported = false; ///< true 表示资源由外部创建，graph 不负责生命周期。
    RHIBuffer externalHandle{}; ///< imported 为 true 时使用的外部资源句柄。
};

/// RenderGraph 内部或外部导入的 texture 声明。
struct RHIRenderGraphTextureDesc {
    std::string name; ///< graph 内唯一名称。
    RHITextureDesc desc{}; ///< graph 创建内部 texture 时使用的描述。
    RHIRenderGraphResourceFlags flags = RHIRenderGraphResourceFlags::None; ///< graph 资源标志。
    bool imported = false; ///< true 表示资源由外部创建，例如 swapchain image。
    RHITexture externalHandle{}; ///< imported 为 true 时使用的外部资源句柄。
};

/// RenderGraph pass 使用的 attachment。
struct RHIRenderGraphAttachmentDesc {
    std::string resourceName; ///< attachment 对应的 graph texture 名称。
    RHITextureAspect aspect = RHITextureAspect::Color; ///< attachment 使用的 aspect。
    u32 mipLevel = 0; ///< attachment 使用的 mip。
    u32 arrayLayer = 0; ///< attachment 使用的数组层。
    RHILoadOp loadOp = RHILoadOp::Clear; ///< pass 开始行为。
    RHIStoreOp storeOp = RHIStoreOp::Store; ///< pass 结束行为。
    RHIClearValue clearValue{}; ///< 清屏值。
};

/// RenderGraph pass 类型，调度器可据此选择命令队列和合法命令集合。
enum class RHIRenderGraphPassType : u8 {
    Raster, ///< 图形渲染 pass，可使用 color/depth attachments 并执行 draw 命令。
    Compute, ///< 计算 pass，执行 compute dispatch，可选择异步 compute 队列。
    Copy, ///< 传输 pass，执行 buffer/texture copy、blit、resolve 或 mipmap 生成。
    Present ///< 呈现 pass，把 swapchain image 提交给窗口系统显示。
};

/// RenderGraph 中的一个 pass 声明，只描述依赖和附件，不直接包含 draw call。
struct RHIRenderGraphPassDesc {
    std::string name; ///< pass 名称，需和 RHIRenderPassWorkload::passName 对应。
    RHIRenderGraphPassType type = RHIRenderGraphPassType::Raster; ///< pass 类型。
    RHIQueueType queue = RHIQueueType::Graphics; ///< pass 希望运行在哪类队列。
    std::vector<RHIRenderGraphResourceRef> reads; ///< pass 读取的资源及访问状态。
    std::vector<RHIRenderGraphResourceRef> writes; ///< pass 写入的资源及访问状态。
    std::vector<RHIRenderGraphAttachmentDesc> colorAttachments; ///< color attachments。
    std::optional<RHIRenderGraphAttachmentDesc> depthStencilAttachment; ///< 可选 depth-stencil attachment。
    bool allowAsyncCompute = false; ///< compute pass 是否允许异步调度，后端需验证队列和同步支持。
    bool cullable = true; ///< 如果 pass 结果未被使用，RenderGraph 是否允许裁剪该 pass。
    bool hasSideEffect = false; ///< true 表示 pass 有外部副作用，即使输出未被读取也不能裁剪。
};

/// 整帧 RenderGraph 描述。调度器可据此排序 pass、分配临时资源并生成 barrier。
struct RHIRenderGraphDesc {
    std::vector<RHIRenderGraphBufferDesc> buffers; ///< graph 管理或导入的 buffers。
    std::vector<RHIRenderGraphTextureDesc> textures; ///< graph 管理或导入的 textures。
    std::vector<RHIRenderGraphPassDesc> passes; ///< pass 列表，顺序可作为初始拓扑顺序。
};

/// 当前帧的全局渲染设置。
struct RHIFrameRenderSettings {
    RHIExtent2D drawableSize{}; ///< 当前可绘制区域尺寸。
    RHIViewport viewport{}; ///< 默认 viewport。
    RHIRect2D scissor{}; ///< 默认 scissor。
    u64 frameIndex = 0; ///< 递增帧号，可用于环形 buffer 和 temporal 资源。
    float deltaTimeSeconds = 0.0F; ///< 与上一帧的时间差。
    u32 maxFramesInFlight = 2; ///< CPU/GPU 并行帧数。
    bool enableVsync = true; ///< 是否希望开启垂直同步。
    bool enableHdr = false; ///< 是否希望使用 HDR swapchain/中间颜色格式。
};

/// 提交给渲染后端的一帧完整数据包。
struct RHIFramePacket {
    RHIFrameRenderSettings settings{}; ///< 当前帧设置。
    RHISwapchainDesc swapchain{}; ///< 当前帧目标 swapchain 需求。
    RHIUploadBatchDesc uploads{}; ///< 本帧开始前需要执行的资源上传。
    RHIRenderCameraSetDesc cameras{}; ///< 相机输入，和物体/光源解耦。
    RHISceneEnvironmentDesc environment{}; ///< 场景环境和后处理基础参数。
    RHIRenderLightSetDesc lights{}; ///< 光源输入，和物体/相机解耦。
    RHIRenderObjectSetDesc objects{}; ///< 可渲染物体输入，和相机/光源解耦。
    RHIRenderGraphDesc graph{}; ///< pass 和资源依赖图。
    std::vector<RHIRenderPassWorkload> workloads; ///< 每个 pass 的具体 draw/dispatch 命令。
    std::vector<RHIQueueSubmitDesc> submissions; ///< 队列提交计划；为空时后端可按 graph 自动生成。
    std::optional<RHIPresentDesc> present; ///< 可选呈现请求，离屏渲染帧可以为空。
};

/// 后端和物理设备能力，用于初始化时选择渲染路径和做功能降级。
struct RHICapabilities {
    RHIGraphicsAPI api = RHIGraphicsAPI::Unknown; ///< 当前后端 API。
    std::string adapterName; ///< GPU/适配器名称。
    u64 dedicatedVideoMemory = 0; ///< 独立显存字节数；集成显卡可为 0 或估算值。
    u64 sharedSystemMemory = 0; ///< 可共享系统内存字节数。
    RHIRenderFeature features = RHIRenderFeature::None; ///< 实际启用的功能位。
    u32 maxTexture2DSize = 0; ///< 支持的最大 2D texture 边长。
    u32 maxTexture3DSize = 0; ///< 支持的最大 3D texture 边长。
    u32 maxTextureCubeSize = 0; ///< 支持的最大 cube texture 边长。
    u32 maxTextureArrayLayers = 0; ///< 最大 texture array 层数。
    u32 maxColorAttachments = 0; ///< 单个 pass 最大 color attachment 数。
    u32 maxBindGroups = 0; ///< 单个 pipeline 最大 bind group 数。
    u32 maxBindingsPerGroup = 0; ///< 单个 bind group 最大 binding 数。
    u32 maxVertexBuffers = 0; ///< 单次绘制最大 vertex buffer binding 数。
    u32 maxVertexAttributes = 0; ///< 单个 pipeline 最大顶点属性数。
    u32 maxPushConstantSize = 0; ///< push constant/root constant 最大字节数。
    u64 minUniformBufferOffsetAlignment = 0; ///< 动态 uniform buffer offset 对齐。
    u64 minStorageBufferOffsetAlignment = 0; ///< 动态 storage buffer offset 对齐。
    u64 optimalBufferCopyOffsetAlignment = 0; ///< buffer copy offset 推荐对齐。
    u64 optimalBufferCopyRowPitchAlignment = 0; ///< buffer-texture copy 每行 pitch 推荐对齐。
    RHISampleCount maxSampleCount = RHISampleCount::Count1; ///< 支持的最大 MSAA 采样数。
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

} // namespace rhi





