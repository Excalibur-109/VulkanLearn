#pragma once

// 渲染硬件抽象接口（RHI）。
//
// 该头文件是示例程序唯一需要依赖的接口合同。
// 它不包含 Vulkan、Direct3D、Win32 或窗口系统头文件；各后端负责将这些
// 通用描述和命令翻译为对应图形 API 的原生对象。

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rhi {

// 创建设备时选择一次后端，应用层不需要再区分具体图形 API。
enum class Backend {
    Software,
    Vulkan,
    D3D11,
    D3D12,
};

enum class Format {
    Unknown,
    RGBA8_UNorm,
    BGRA8_UNorm,
    RGBA16_Float,
    RGBA32_Float,
    D32_Float,
};

// 资源用途是位掩码，因为同一个纹理可能既是渲染目标又是着色器输入，
// 例如阴影深度通道结束后的阴影贴图。
enum class ResourceUsage : uint32_t {
    None          = 0,
    Vertex        = 1u << 0,
    Index         = 1u << 1,
    Uniform       = 1u << 2,
    Storage       = 1u << 3,
    Texture       = 1u << 4,
    RenderTarget  = 1u << 5,
    DepthStencil  = 1u << 6,
};

inline ResourceUsage operator|(ResourceUsage lhs, ResourceUsage rhs) {
    return static_cast<ResourceUsage>(static_cast<uint32_t>(lhs) |
                                      static_cast<uint32_t>(rhs));
}

inline bool has(ResourceUsage value, ResourceUsage flag) {
    return (static_cast<uint32_t>(value) & static_cast<uint32_t>(flag)) != 0;
}

enum class ShaderStage {
    Vertex,
    Fragment,
    Compute,
};

enum class LoadOp {
    Load,
    Clear,
    DontCare,
};

enum class PrimitiveTopology {
    TriangleList,
    TriangleStrip,
    LineList,
};

enum class CompareOp {
    Never,
    Less,
    LessEqual,
    Equal,
    GreaterEqual,
    Greater,
    Always,
};

// 显式资源状态让“写入后读取”的转换出现在命令流中。
// Vulkan 和 D3D12 需要真正的 barrier，D3D11 则通过资源冲突跟踪完成。
enum class ResourceState {
    Undefined,
    CopySource,
    CopyDestination,
    ShaderRead,
    RenderTarget,
    DepthWrite,
    Present,
};

enum class BindingType {
    UniformBuffer,
    StorageBuffer,
    SampledTexture,
    Sampler,
};

struct Extent2D {
    uint32_t width = 1;
    uint32_t height = 1;
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct ClearValue {
    Color color{};
    float depth = 1.0f;
    uint32_t stencil = 0;
};

struct BufferDesc {
    size_t size = 0;
    ResourceUsage usage = ResourceUsage::None;
    bool cpuVisible = false;
    std::string debugName;
};

struct TextureDesc {
    Extent2D extent{};
    uint32_t mipLevels = 1;
    uint32_t layers = 1;
    Format format = Format::Unknown;
    ResourceUsage usage = ResourceUsage::None;
    std::string debugName;
};

struct SamplerDesc {
    bool linear = true;
    bool repeat = true;
    float anisotropy = 1.0f;
};

struct ShaderDesc {
    ShaderStage stage{};
    std::string entry = "main";
    std::vector<uint32_t> bytecode;
    std::string source;
    std::string debugName;
};

struct ShaderBinding {
    uint32_t slot = 0;
    BindingType type = BindingType::UniformBuffer;
    ShaderStage visibility = ShaderStage::Vertex;
};

struct VertexAttribute {
    uint32_t location = 0;
    uint32_t offset = 0;
    Format format = Format::RGBA32_Float;
};

// 不可变的图形管线状态。原生后端会把它转换为 VkPipeline、D3D11 状态对象
// 或 D3D12 管线状态对象。
struct PipelineDesc {
    std::shared_ptr<class Shader> vertexShader;
    std::shared_ptr<class Shader> fragmentShader;
    std::vector<VertexAttribute> attributes;
    std::vector<ShaderBinding> bindings;

    Format colorFormat = Format::BGRA8_UNorm;
    Format depthFormat = Format::D32_Float;
    PrimitiveTopology topology = PrimitiveTopology::TriangleList;
    CompareOp depthCompare = CompareOp::LessEqual;

    bool depthTest = true;
    bool depthWrite = true;
    bool blend = false;
    float depthBiasConstant = 0.0f;
    float depthBiasSlope = 0.0f;
    std::string debugName;
};

struct RenderPassDesc {
    Format colorFormat = Format::BGRA8_UNorm;
    Format depthFormat = Format::D32_Float;
    LoadOp colorLoad = LoadOp::Clear;
    LoadOp depthLoad = LoadOp::Clear;
    bool depthReadOnly = false;
    ClearValue clear{};
    std::string debugName;
};

// 不透明资源句柄。具体实现把原生对象保存在类的私有成员中。
class Buffer {
public:
    virtual ~Buffer() = default;
    virtual const BufferDesc& desc() const = 0;
};

class Texture {
public:
    virtual ~Texture() = default;
    virtual const TextureDesc& desc() const = 0;
};

class Sampler {
public:
    virtual ~Sampler() = default;
    virtual const SamplerDesc& desc() const = 0;
};

class Shader {
public:
    virtual ~Shader() = default;
    virtual ShaderStage stage() const = 0;
};

class Pipeline {
public:
    virtual ~Pipeline() = default;
};

class CommandList {
public:
    virtual ~CommandList() = default;

    virtual void begin() = 0;
    virtual void end() = 0;

    virtual void beginRenderPass(const RenderPassDesc& desc,
                                 Texture* color,
                                 Texture* depth) = 0;
    virtual void endRenderPass() = 0;

    virtual void setPipeline(Pipeline* pipeline) = 0;
    virtual void setViewport(float x, float y, float width, float height,
                             float minDepth = 0.0f,
                             float maxDepth = 1.0f) = 0;
    virtual void setScissor(int x, int y, uint32_t width, uint32_t height) = 0;
    virtual void setVertexBuffer(Buffer* buffer, uint32_t slot = 0) = 0;
    virtual void setIndexBuffer(Buffer* buffer) = 0;
    virtual void setUniformBuffer(Buffer* buffer, uint32_t slot = 0) = 0;

    virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount,
                             uint32_t firstIndex = 0,
                             int32_t vertexOffset = 0) = 0;
    virtual void dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

    // 这些是可选命令，默认空实现使最小验证后端也能工作；原生后端按需覆盖。
    virtual void setTexture(Texture*, uint32_t) {}
    virtual void setSampler(Sampler*, uint32_t) {}
    virtual void setPushConstants(const void*, size_t, ShaderStage) {}
    virtual void transition(Texture*, ResourceState, ResourceState) {}
    virtual void barrier(Texture*) {}
};

class Device {
public:
    virtual ~Device() = default;

    virtual Backend backend() const = 0;
    virtual const char* name() const = 0;
    virtual std::unique_ptr<Buffer> createBuffer(const BufferDesc&,
                                                  const void* data = nullptr) = 0;
    virtual std::unique_ptr<Texture> createTexture(const TextureDesc&,
                                                    const void* data = nullptr) = 0;
    virtual std::unique_ptr<Sampler> createSampler(const SamplerDesc&) = 0;
    virtual std::unique_ptr<Shader> createShader(const ShaderDesc&) = 0;
    virtual std::unique_ptr<Pipeline> createPipeline(const PipelineDesc&) = 0;
    virtual std::unique_ptr<CommandList> createCommandList() = 0;

    virtual void submit(CommandList&) = 0;
    virtual void waitIdle() = 0;
    virtual bool present() = 0;
    virtual Extent2D drawableExtent() const = 0;
};

struct DeviceCreateInfo {
    Backend backend = Backend::Software;
    Extent2D extent{1280, 720};
    std::string applicationName = "RHI PBR Demo";
};

std::unique_ptr<Device> createDevice(const DeviceCreateInfo& info);
const char* backendName(Backend backend);

} // 命名空间 rhi
