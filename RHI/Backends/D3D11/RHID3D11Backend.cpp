#include "RHI/Backends/RHINativeBackends.hpp"
#include "../Common/RHINativeDeviceBase.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <array>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace RHI
{
namespace
{

using Microsoft::WRL::ComPtr;

/** 将 Windows 宽字符串转换为 UTF-8。 */
std::string ToUtf8(const wchar_t* value)
{
    const int size = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), size, nullptr, nullptr);
    result.pop_back();
    return result;
}

/** 将 RHI 格式映射为 D3D11 格式。 */
DXGI_FORMAT ToDxgiFormat(const RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::RGBA8_UNorm_sRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case RHIFormat::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case RHIFormat::BGRA8_UNorm_sRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case RHIFormat::RGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case RHIFormat::R32_Float: return DXGI_FORMAT_R32_FLOAT;
    case RHIFormat::D32_Float: return DXGI_FORMAT_D32_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

/** 将 RHI 顶点格式映射为 D3D11 输入布局格式。 */
DXGI_FORMAT ToDxgiVertexFormat(const RHIVertexFormat format)
{
    switch (format)
    {
    case RHIVertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
    case RHIVertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHIVertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHIVertexFormat::UNorm8x4: return DXGI_FORMAT_R8G8B8A8_UNORM;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

/** 按 RHI 属性位置选择标准 HLSL 语义。 */
const char* SemanticName(const std::uint32_t location)
{
    switch (location)
    {
    case 0: return "POSITION";
    case 1: return "NORMAL";
    default: return "TEXCOORD";
    }
}

/** 将 RHI 图元拓扑映射为 D3D11 拓扑。 */
D3D11_PRIMITIVE_TOPOLOGY ToTopology(const RHIPrimitiveTopology topology)
{
    switch (topology)
    {
    case RHIPrimitiveTopology::PointList: return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    case RHIPrimitiveTopology::LineList: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case RHIPrimitiveTopology::LineStrip: return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case RHIPrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    default: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

/** 编译 HLSL 文本，并将诊断转换为 RHI 状态。 */
RHIStatus CompileHlsl(const RHIShaderModuleDesc& module, const char* profile, ComPtr<ID3DBlob>& outBlob)
{
    if (module.CodeFormat != RHIShaderCodeFormat::SourceText || module.Code.empty())
    {
        return RHIStatus::Error(RHIResult::Unsupported, "D3D11 PBR 路径只接受 HLSL 源码着色器模块。");
    }
    ComPtr<ID3DBlob> diagnostics;
    const HRESULT result = D3DCompile(module.Code.data(), module.Code.size(), module.DebugName.c_str(), nullptr, nullptr,
        module.EntryPoint.c_str(), profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &outBlob, &diagnostics);
    if (FAILED(result))
    {
        std::string message = "D3D11 着色器编译失败。";
        if (diagnostics != nullptr)
        {
            message.append(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize());
        }
        return RHIStatus::Error(RHIResult::Failure, std::move(message));
    }
    return RHIStatus::Ok();
}

struct D3D11Buffer final { ComPtr<ID3D11Buffer> Resource; RHIBufferDesc Desc; };
struct D3D11Texture final { ComPtr<ID3D11Texture2D> Resource; RHITextureDesc Desc; };
struct D3D11TextureView final { ComPtr<ID3D11RenderTargetView> RenderTarget; ComPtr<ID3D11ShaderResourceView> ShaderResource; };
struct D3D11Sampler final { ComPtr<ID3D11SamplerState> State; };
struct D3D11Shader final { RHIShaderModuleDesc Desc; };
struct D3D11Fence final { ComPtr<ID3D11Query> Query; bool Submitted = false; };

struct D3D11Pipeline final
{
    ComPtr<ID3D11VertexShader> VertexShader;
    ComPtr<ID3D11PixelShader> PixelShader;
    ComPtr<ID3D11InputLayout> InputLayout;
    ComPtr<ID3D11RasterizerState> Rasterizer;
    ComPtr<ID3D11BlendState> Blend;
    D3D11_PRIMITIVE_TOPOLOGY Topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

class D3D11Device;

/** 使用 D3D11 即时上下文实现的 RHI 主命令列表。 */
class D3D11CommandList final : public IRHICommandList
{
public:
    /** 使用所属设备和创建参数构造命令列表。 */
    D3D11CommandList(D3D11Device& device, RHICommandListDesc desc) : m_device(device), m_desc(std::move(desc)) {}
    /** 返回命令列表所属队列类型。 */
    [[nodiscard]] RHIQueueType GetQueueType() const noexcept override { return m_desc.QueueType; }
    /** 返回命令录制状态。 */
    [[nodiscard]] RHICommandListState GetState() const noexcept override { return m_state; }
    /** 返回不可变创建参数。 */
    [[nodiscard]] const RHICommandListDesc& GetDesc() const noexcept override { return m_desc; }
    RHIStatus Reset() override;
    RHIStatus Begin() override;
    RHIStatus End() override;
    RHIStatus PushDebugLabel(std::string_view, const RHIColor&) override { return RHIStatus::Ok(); }
    RHIStatus PopDebugLabel() override { return RHIStatus::Ok(); }
    RHIStatus ResourceBarriers(std::span<const RHIBarrier>) override { return RHIStatus::Ok(); }
    RHIStatus BeginRendering(const RHIRenderingDesc& desc) override;
    RHIStatus EndRendering() override;
    RHIStatus BindGraphicsPipeline(RHIGraphicsPipelineHandle pipeline) override;
    RHIStatus BindComputePipeline(RHIComputePipelineHandle) override { return RHINativeDeviceBase::Unsupported("D3D11 计算管线"); }
    RHIStatus BindRayTracingPipeline(RHIRayTracingPipelineHandle) override { return RHINativeDeviceBase::Unsupported("D3D11 光线追踪管线"); }
    RHIStatus BindSet(std::uint32_t, RHIBindSetHandle, std::span<const std::uint32_t>) override { return RHINativeDeviceBase::Unsupported("D3D11 绑定集"); }
    RHIStatus PushConstants(RHIShaderStage, std::uint32_t offset, std::span<const std::byte> data) override;
    RHIStatus BindVertexBuffers(std::uint32_t firstBinding, std::span<const RHIVertexBufferBinding> bindings) override;
    RHIStatus BindIndexBuffer(const RHIIndexBufferBinding& binding) override;
    RHIStatus SetViewports(std::uint32_t, std::span<const RHIViewport> viewports) override;
    RHIStatus SetScissors(std::uint32_t, std::span<const RHIScissor> scissors) override;
    RHIStatus SetBlendConstant(const RHIColor&) override { return RHIStatus::Ok(); }
    RHIStatus SetDepthBias(float, float, float) override { return RHIStatus::Ok(); }
    RHIStatus SetStencilReference(std::uint32_t) override { return RHIStatus::Ok(); }
    RHIStatus SetStencilCompareMask(std::uint32_t) override { return RHIStatus::Ok(); }
    RHIStatus SetStencilWriteMask(std::uint32_t) override { return RHIStatus::Ok(); }
    RHIStatus SetDepthBounds(float, float) override { return RHINativeDeviceBase::Unsupported("D3D11 深度范围"); }
    RHIStatus SetLineWidth(float) override { return RHINativeDeviceBase::Unsupported("D3D11 线宽"); }
    RHIStatus Draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::uint32_t firstInstance) override;
    RHIStatus DrawIndexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t offset, std::uint32_t firstInstance) override;
    RHIStatus DrawIndirect(RHIBufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D11 间接绘制"); }
    RHIStatus DrawIndexedIndirect(RHIBufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D11 间接索引绘制"); }
    RHIStatus DrawMeshTasks(std::uint32_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D11 网格着色"); }
    RHIStatus DrawMeshTasksIndirect(RHIBufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D11 间接网格着色"); }
    RHIStatus Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D11 计算调度"); }
    RHIStatus DispatchIndirect(RHIBufferHandle, std::uint64_t) override { return RHINativeDeviceBase::Unsupported("D3D11 间接计算调度"); }
    RHIStatus CopyBuffer(RHIBufferHandle, RHIBufferHandle, std::span<const RHIBufferCopyRegion>) override { return RHINativeDeviceBase::Unsupported("D3D11 缓冲区复制"); }
    RHIStatus CopyBufferToTexture(RHIBufferHandle, RHITextureHandle, std::span<const RHITextureBufferCopyRegion>) override { return RHINativeDeviceBase::Unsupported("D3D11 缓冲区到纹理复制"); }
    RHIStatus CopyTextureToBuffer(RHITextureHandle, RHIBufferHandle, std::span<const RHITextureBufferCopyRegion>) override { return RHINativeDeviceBase::Unsupported("D3D11 纹理到缓冲区复制"); }
    RHIStatus CopyTexture(RHITextureHandle, RHITextureHandle, std::span<const RHITextureCopyRegion>) override { return RHINativeDeviceBase::Unsupported("D3D11 纹理复制"); }
    RHIStatus ResolveTexture(RHITextureHandle, RHITextureHandle, std::span<const RHITextureCopyRegion>, RHIResolveMode) override { return RHINativeDeviceBase::Unsupported("D3D11 多重采样解析"); }
    RHIStatus ClearColorTexture(RHITextureHandle, const RHIClearColorValue&, std::span<const RHISubresourceRange>) override { return RHINativeDeviceBase::Unsupported("D3D11 纹理清除"); }
    RHIStatus ClearDepthStencilTexture(RHITextureHandle, const RHIClearDepthStencilValue&, std::span<const RHISubresourceRange>) override { return RHINativeDeviceBase::Unsupported("D3D11 深度清除"); }
    RHIStatus BeginQuery(RHIQueryPoolHandle, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D11 查询"); }
    RHIStatus EndQuery(RHIQueryPoolHandle, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D11 查询"); }
    RHIStatus WriteTimestamp(RHIQueryPoolHandle, std::uint32_t, RHIPipelineStage) override { return RHINativeDeviceBase::Unsupported("D3D11 时间戳"); }
    RHIStatus ResolveQueryData(RHIQueryPoolHandle, std::uint32_t, std::uint32_t, RHIBufferHandle, std::uint64_t) override { return RHINativeDeviceBase::Unsupported("D3D11 查询解析"); }
    RHIStatus BuildAccelerationStructure(const RHIAccelerationStructureBuildDesc&) override { return RHINativeDeviceBase::Unsupported("D3D11 加速结构"); }
    RHIStatus TraceRays(const RHIShaderTableDesc&, std::uint32_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D11 光线追踪"); }
    RHIStatus ExecuteSecondary(std::span<IRHICommandList* const>) override { return RHINativeDeviceBase::Unsupported("D3D11 次级命令列表"); }

private:
    D3D11Device& m_device;
    RHICommandListDesc m_desc;
    RHICommandListState m_state = RHICommandListState::Initial;
    bool m_rendering = false;
};

/** D3D11 原生设备，覆盖 PBR 所需资源、管线、命令和围栏路径。 */
class D3D11Device final : public RHINativeDeviceBase
{
public:
    D3D11Device(RHIAdapterInfo adapter, ComPtr<ID3D11Device> device, ComPtr<ID3D11DeviceContext> context)
        : RHINativeDeviceBase(std::move(adapter)), m_device(std::move(device)), m_context(std::move(context))
    {
        m_limits.MaxTextureDimension2D = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_limits.MaxColorAttachments = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
        m_limits.MaxVertexBuffers = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        m_limits.MaxPushConstantBytes = 256;
        m_limits.MinConstantBufferOffsetAlignment = 16;
    }

    RHIStatus CreateBuffer(const RHIBufferDesc& desc, RHIBufferHandle& outBuffer) override;
    void DestroyBuffer(RHIBufferHandle buffer) override { std::scoped_lock lock(m_mutex); m_buffers.erase(buffer.Value); }
    RHIStatus CreateTexture(const RHITextureDesc& desc, RHITextureHandle& outTexture) override;
    void DestroyTexture(RHITextureHandle texture) override { std::scoped_lock lock(m_mutex); m_textures.erase(texture.Value); }
    RHIStatus CreateTextureView(const RHITextureViewDesc& desc, RHITextureViewHandle& outView) override;
    void DestroyTextureView(RHITextureViewHandle view) override { std::scoped_lock lock(m_mutex); m_views.erase(view.Value); }
    RHIStatus CreateSampler(const RHISamplerDesc& desc, RHISamplerHandle& outSampler) override;
    void DestroySampler(RHISamplerHandle sampler) override { std::scoped_lock lock(m_mutex); m_samplers.erase(sampler.Value); }
    RHIStatus MapBuffer(RHIBufferHandle buffer, std::uint64_t offset, std::uint64_t size, void*& outData) override;
    RHIStatus FlushMappedBufferRange(RHIBufferHandle, std::uint64_t, std::uint64_t) override { return RHIStatus::Ok(); }
    RHIStatus InvalidateMappedBufferRange(RHIBufferHandle, std::uint64_t, std::uint64_t) override { return RHIStatus::Ok(); }
    RHIStatus UnmapBuffer(RHIBufferHandle buffer) override;
    RHIStatus CreateShaderModule(const RHIShaderModuleDesc& desc, RHIShaderModuleHandle& outShaderModule) override;
    void DestroyShaderModule(RHIShaderModuleHandle module) override { std::scoped_lock lock(m_mutex); m_shaders.erase(module.Value); }
    RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, RHIGraphicsPipelineHandle& outPipeline) override;
    void DestroyGraphicsPipeline(RHIGraphicsPipelineHandle pipeline) override { std::scoped_lock lock(m_mutex); m_pipelines.erase(pipeline.Value); }
    RHIStatus CreateCommandList(const RHICommandListDesc& desc, RHICommandListPtr& outCommandList) override;
    RHIStatus CreateFence(const RHIFenceDesc& desc, RHIFenceHandle& outFence) override;
    void DestroyFence(RHIFenceHandle fence) override { std::scoped_lock lock(m_mutex); m_fences.erase(fence.Value); }
    RHIStatus WaitForFence(RHIFenceHandle fence, std::uint64_t timeoutNanoseconds) override;
    RHIStatus ResetFence(RHIFenceHandle fence) override;
    RHIStatus Submit(RHIQueueType queue, const RHIQueueSubmitDesc& desc) override;
    RHIStatus WaitIdle() override { m_context->Flush(); return RHIStatus::Ok(); }

    /** 返回即时上下文，仅供同后端命令列表调用。 */
    ID3D11DeviceContext* Context() const noexcept { return m_context.Get(); }
    /** 查找已创建的缓冲区。 */
    D3D11Buffer* FindBuffer(RHIBufferHandle handle) { auto it = m_buffers.find(handle.Value); return it == m_buffers.end() ? nullptr : &it->second; }
    /** 查找已创建的纹理视图。 */
    D3D11TextureView* FindView(RHITextureViewHandle handle) { auto it = m_views.find(handle.Value); return it == m_views.end() ? nullptr : &it->second; }
    /** 查找已创建的图形管线。 */
    D3D11Pipeline* FindPipeline(RHIGraphicsPipelineHandle handle) { auto it = m_pipelines.find(handle.Value); return it == m_pipelines.end() ? nullptr : &it->second; }
    /** 绑定写入推送常量的动态常量缓冲区。 */
    RHIStatus SetPushConstants(std::uint32_t offset, std::span<const std::byte> data);

private:
    std::uint64_t NextHandle() noexcept { return m_nextHandle++; }
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<ID3D11Buffer> m_pushConstantBuffer;
    std::array<std::byte, 256> m_pushConstants{};
    std::uint64_t m_nextHandle = 1;
    std::mutex m_mutex;
    std::unordered_map<std::uint64_t, D3D11Buffer> m_buffers;
    std::unordered_map<std::uint64_t, D3D11Texture> m_textures;
    std::unordered_map<std::uint64_t, D3D11TextureView> m_views;
    std::unordered_map<std::uint64_t, D3D11Sampler> m_samplers;
    std::unordered_map<std::uint64_t, D3D11Shader> m_shaders;
    std::unordered_map<std::uint64_t, D3D11Pipeline> m_pipelines;
    std::unordered_map<std::uint64_t, D3D11Fence> m_fences;
};

RHIStatus D3D11Device::CreateBuffer(const RHIBufferDesc& desc, RHIBufferHandle& outBuffer)
{
    if (desc.SizeInBytes == 0 || desc.SizeInBytes > (std::numeric_limits<UINT>::max)()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 缓冲区大小无效。");
    D3D11_BUFFER_DESC native{};
    native.ByteWidth = static_cast<UINT>(desc.SizeInBytes);
    native.BindFlags = ((static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(RHIBufferUsage::Vertex)) ? D3D11_BIND_VERTEX_BUFFER : 0u) |
        ((static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(RHIBufferUsage::Index)) ? D3D11_BIND_INDEX_BUFFER : 0u) |
        ((static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(RHIBufferUsage::Constant)) ? D3D11_BIND_CONSTANT_BUFFER : 0u);
    if (desc.MemoryUsage == RHIMemoryUsage::CpuToGpu)
    {
        native.Usage = D3D11_USAGE_DYNAMIC;
        native.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    }
    else
    {
        native.Usage = D3D11_USAGE_DEFAULT;
    }
    ComPtr<ID3D11Buffer> resource;
    if (FAILED(m_device->CreateBuffer(&native, nullptr, &resource))) return RHIStatus::Error(RHIResult::OutOfMemory, "无法创建 D3D11 缓冲区。");
    std::scoped_lock lock(m_mutex);
    outBuffer.Value = NextHandle();
    m_buffers.emplace(outBuffer.Value, D3D11Buffer{std::move(resource), desc});
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::CreateTexture(const RHITextureDesc& desc, RHITextureHandle& outTexture)
{
    if (desc.Dimension != RHITextureDimension::Texture2D || !desc.Extent.IsValid() || ToDxgiFormat(desc.Format) == DXGI_FORMAT_UNKNOWN)
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 PBR 路径只支持有效的二维已知格式纹理。");
    D3D11_TEXTURE2D_DESC native{};
    native.Width = desc.Extent.Width;
    native.Height = desc.Extent.Height;
    native.MipLevels = desc.MipLevels;
    native.ArraySize = desc.ArrayLayers;
    native.Format = ToDxgiFormat(desc.Format);
    native.SampleDesc.Count = static_cast<UINT>(desc.SampleCount);
    native.Usage = D3D11_USAGE_DEFAULT;
    const std::uint32_t usage = static_cast<std::uint32_t>(desc.Usage);
    if ((usage & static_cast<std::uint32_t>(RHITextureUsage::ColorAttachment)) != 0u) native.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if ((usage & static_cast<std::uint32_t>(RHITextureUsage::ShaderRead)) != 0u) native.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if (native.BindFlags == 0) return RHIStatus::Error(RHIResult::Unsupported, "D3D11 纹理至少需要颜色附件或着色器读取用途。");
    ComPtr<ID3D11Texture2D> resource;
    if (FAILED(m_device->CreateTexture2D(&native, nullptr, &resource))) return RHIStatus::Error(RHIResult::OutOfMemory, "无法创建 D3D11 纹理。");
    std::scoped_lock lock(m_mutex);
    outTexture.Value = NextHandle();
    m_textures.emplace(outTexture.Value, D3D11Texture{std::move(resource), desc});
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::CreateTextureView(const RHITextureViewDesc& desc, RHITextureViewHandle& outView)
{
    std::scoped_lock lock(m_mutex);
    const auto texture = m_textures.find(desc.Texture.Value);
    if (texture == m_textures.end()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 纹理视图引用了未知纹理。");
    D3D11TextureView view;
    const std::uint32_t usage = static_cast<std::uint32_t>(texture->second.Desc.Usage);
    if ((usage & static_cast<std::uint32_t>(RHITextureUsage::ColorAttachment)) != 0u && FAILED(m_device->CreateRenderTargetView(texture->second.Resource.Get(), nullptr, &view.RenderTarget)))
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D11 渲染目标视图。");
    if ((usage & static_cast<std::uint32_t>(RHITextureUsage::ShaderRead)) != 0u && FAILED(m_device->CreateShaderResourceView(texture->second.Resource.Get(), nullptr, &view.ShaderResource)))
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D11 着色器资源视图。");
    outView.Value = NextHandle();
    m_views.emplace(outView.Value, std::move(view));
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::CreateSampler(const RHISamplerDesc&, RHISamplerHandle& outSampler)
{
    D3D11_SAMPLER_DESC native{};
    native.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    native.AddressU = native.AddressV = native.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
    native.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11Sampler sampler;
    if (FAILED(m_device->CreateSamplerState(&native, &sampler.State))) return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D11 采样器。");
    std::scoped_lock lock(m_mutex);
    outSampler.Value = NextHandle();
    m_samplers.emplace(outSampler.Value, std::move(sampler));
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::MapBuffer(RHIBufferHandle buffer, std::uint64_t offset, std::uint64_t size, void*& outData)
{
    std::scoped_lock lock(m_mutex);
    D3D11Buffer* resource = FindBuffer(buffer);
    if (resource == nullptr || offset + size > resource->Desc.SizeInBytes || resource->Desc.MemoryUsage != RHIMemoryUsage::CpuToGpu)
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 缓冲区映射范围无效或不可写。");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(resource->Resource.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return RHIStatus::Error(RHIResult::Failure, "无法映射 D3D11 缓冲区。");
    outData = static_cast<std::byte*>(mapped.pData) + offset;
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::UnmapBuffer(RHIBufferHandle buffer)
{
    std::scoped_lock lock(m_mutex);
    D3D11Buffer* resource = FindBuffer(buffer);
    if (resource == nullptr) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 取消映射引用了未知缓冲区。");
    m_context->Unmap(resource->Resource.Get(), 0);
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::CreateShaderModule(const RHIShaderModuleDesc& desc, RHIShaderModuleHandle& outShaderModule)
{
    if (desc.Code.empty()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 着色器模块代码为空。");
    std::scoped_lock lock(m_mutex);
    outShaderModule.Value = NextHandle();
    m_shaders.emplace(outShaderModule.Value, D3D11Shader{desc});
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, RHIGraphicsPipelineHandle& outPipeline)
{
    std::scoped_lock lock(m_mutex);
    const RHIShaderStageDesc* vertex = nullptr;
    const RHIShaderStageDesc* pixel = nullptr;
    for (const RHIShaderStageDesc& stage : desc.ShaderStages)
    {
        if (stage.Stage == RHIShaderStage::Vertex) vertex = &stage;
        if (stage.Stage == RHIShaderStage::Pixel) pixel = &stage;
    }
    if (vertex == nullptr || pixel == nullptr) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 图形管线需要顶点和像素着色器。");
    const auto vertexModule = m_shaders.find(vertex->Module.Value);
    const auto pixelModule = m_shaders.find(pixel->Module.Value);
    if (vertexModule == m_shaders.end() || pixelModule == m_shaders.end()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 图形管线引用了未知着色器模块。");
    RHIShaderModuleDesc vertexDesc = vertexModule->second.Desc; vertexDesc.EntryPoint = vertex->EntryPoint;
    RHIShaderModuleDesc pixelDesc = pixelModule->second.Desc; pixelDesc.EntryPoint = pixel->EntryPoint;
    ComPtr<ID3DBlob> vertexCode;
    ComPtr<ID3DBlob> pixelCode;
    RHIStatus status = CompileHlsl(vertexDesc, "vs_5_0", vertexCode);
    if (!status.Succeeded()) return status;
    if (!(status = CompileHlsl(pixelDesc, "ps_5_0", pixelCode)).Succeeded()) return status;
    D3D11Pipeline pipeline;
    if (FAILED(m_device->CreateVertexShader(vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), nullptr, &pipeline.VertexShader)) ||
        FAILED(m_device->CreatePixelShader(pixelCode->GetBufferPointer(), pixelCode->GetBufferSize(), nullptr, &pipeline.PixelShader)))
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D11 图形着色器。");
    std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
    if (desc.VertexBuffers.empty()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 PBR 管线需要顶点布局。");
    for (std::uint32_t slot = 0; slot < desc.VertexBuffers.size(); ++slot)
        for (const RHIVertexAttribute& attribute : desc.VertexBuffers[slot].Attributes)
            elements.push_back({SemanticName(attribute.Location), attribute.Location > 1 ? attribute.Location - 2 : 0, ToDxgiVertexFormat(attribute.Format), slot, attribute.Offset,
                desc.VertexBuffers[slot].StepMode == RHIVertexStepMode::PerInstance ? D3D11_INPUT_PER_INSTANCE_DATA : D3D11_INPUT_PER_VERTEX_DATA, 0});
    if (FAILED(m_device->CreateInputLayout(elements.data(), static_cast<UINT>(elements.size()), vertexCode->GetBufferPointer(), vertexCode->GetBufferSize(), &pipeline.InputLayout)))
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D11 顶点输入布局。");
    D3D11_RASTERIZER_DESC rasterizer{};
    rasterizer.FillMode = desc.Rasterizer.FillMode == RHIFillMode::Wireframe ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
    rasterizer.CullMode = desc.Rasterizer.CullMode == RHICullMode::None ? D3D11_CULL_NONE : desc.Rasterizer.CullMode == RHICullMode::Front ? D3D11_CULL_FRONT : D3D11_CULL_BACK;
    rasterizer.DepthClipEnable = TRUE;
    if (FAILED(m_device->CreateRasterizerState(&rasterizer, &pipeline.Rasterizer))) return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D11 光栅状态。");
    D3D11_BLEND_DESC blend{};
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(m_device->CreateBlendState(&blend, &pipeline.Blend))) return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D11 混合状态。");
    pipeline.Topology = ToTopology(desc.Topology);
    outPipeline.Value = NextHandle();
    m_pipelines.emplace(outPipeline.Value, std::move(pipeline));
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::CreateCommandList(const RHICommandListDesc& desc, RHICommandListPtr& outCommandList)
{
    if (desc.QueueType != RHIQueueType::Graphics || desc.Level != RHICommandListLevel::Primary) return RHIStatus::Error(RHIResult::Unsupported, "D3D11 PBR 路径只支持图形主命令列表。");
    outCommandList = std::make_shared<D3D11CommandList>(*this, desc);
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::CreateFence(const RHIFenceDesc&, RHIFenceHandle& outFence)
{
    D3D11_QUERY_DESC query{D3D11_QUERY_EVENT, 0};
    D3D11Fence fence;
    if (FAILED(m_device->CreateQuery(&query, &fence.Query))) return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D11 围栏查询。");
    std::scoped_lock lock(m_mutex);
    outFence.Value = NextHandle();
    m_fences.emplace(outFence.Value, std::move(fence));
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::WaitForFence(RHIFenceHandle fence, const std::uint64_t timeoutNanoseconds)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::nanoseconds(timeoutNanoseconds);
    std::scoped_lock lock(m_mutex);
    const auto it = m_fences.find(fence.Value);
    if (it == m_fences.end()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 等待引用了未知围栏。");
    while (it->second.Submitted && m_context->GetData(it->second.Query.Get(), nullptr, 0, 0) == S_FALSE)
    {
        if (timeoutNanoseconds != (std::numeric_limits<std::uint64_t>::max)() && std::chrono::steady_clock::now() >= deadline) return RHIStatus::Error(RHIResult::NotReady, "等待 D3D11 围栏超时。");
        std::this_thread::yield();
    }
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::ResetFence(RHIFenceHandle fence)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_fences.find(fence.Value);
    if (it == m_fences.end()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 重置引用了未知围栏。");
    it->second.Submitted = false;
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::Submit(RHIQueueType queue, const RHIQueueSubmitDesc& desc)
{
    if (queue != RHIQueueType::Graphics) return RHIStatus::Error(RHIResult::Unsupported, "D3D11 PBR 路径只支持图形队列提交。");
    for (const RHICommandListPtr& list : desc.CommandLists)
        if (list == nullptr || list->GetState() != RHICommandListState::Executable) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 提交包含未结束的命令列表。");
    if (desc.SignalFence.IsValid())
    {
        std::scoped_lock lock(m_mutex);
        const auto it = m_fences.find(desc.SignalFence.Value);
        if (it == m_fences.end()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 提交引用了未知围栏。");
        m_context->End(it->second.Query.Get());
        it->second.Submitted = true;
    }
    m_context->Flush();
    return RHIStatus::Ok();
}

RHIStatus D3D11Device::SetPushConstants(const std::uint32_t offset, const std::span<const std::byte> data)
{
    if (offset + data.size() > m_pushConstants.size()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 推送常量超过 256 字节限制。");
    std::memcpy(m_pushConstants.data() + offset, data.data(), data.size());
    if (m_pushConstantBuffer == nullptr)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(m_pushConstants.size());
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_pushConstantBuffer))) return RHIStatus::Error(RHIResult::OutOfMemory, "无法创建 D3D11 PBR 常量缓冲区。");
    }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(m_context->Map(m_pushConstantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return RHIStatus::Error(RHIResult::Failure, "无法映射 D3D11 PBR 常量缓冲区。");
    std::memcpy(mapped.pData, m_pushConstants.data(), m_pushConstants.size());
    m_context->Unmap(m_pushConstantBuffer.Get(), 0);
    ID3D11Buffer* constantBuffer = m_pushConstantBuffer.Get();
    m_context->VSSetConstantBuffers(0, 1, &constantBuffer);
    m_context->PSSetConstantBuffers(0, 1, &constantBuffer);
    return RHIStatus::Ok();
}

RHIStatus D3D11CommandList::Reset() { if (m_state == RHICommandListState::Pending) return RHIStatus::Error(RHIResult::NotReady, "D3D11 命令列表仍在执行。"); m_state = RHICommandListState::Initial; return RHIStatus::Ok(); }
RHIStatus D3D11CommandList::Begin() { if (m_state != RHICommandListState::Initial) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 命令列表必须先重置再开始。"); m_state = RHICommandListState::Recording; return RHIStatus::Ok(); }
RHIStatus D3D11CommandList::End() { if (m_state != RHICommandListState::Recording || m_rendering) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 命令列表不能在渲染区域内结束。"); m_state = RHICommandListState::Executable; return RHIStatus::Ok(); }

RHIStatus D3D11CommandList::BeginRendering(const RHIRenderingDesc& desc)
{
    if (m_state != RHICommandListState::Recording || m_rendering || desc.ColorAttachments.size() != 1) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 PBR 路径需要一个颜色附件的有效渲染区域。");
    D3D11TextureView* view = m_device.FindView(desc.ColorAttachments[0].View);
    if (view == nullptr || view->RenderTarget == nullptr) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 渲染区域引用了无效颜色视图。");
    ID3D11RenderTargetView* target = view->RenderTarget.Get();
    m_device.Context()->OMSetRenderTargets(1, &target, nullptr);
    if (desc.ColorAttachments[0].LoadOp == RHILoadOp::Clear)
    {
        const auto& color = desc.ColorAttachments[0].ClearValue;
        const float values[] = {color.R, color.G, color.B, color.A};
        m_device.Context()->ClearRenderTargetView(target, values);
    }
    m_rendering = true;
    return RHIStatus::Ok();
}

RHIStatus D3D11CommandList::EndRendering() { if (!m_rendering) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 当前没有活跃渲染区域。"); m_rendering = false; return RHIStatus::Ok(); }

RHIStatus D3D11CommandList::BindGraphicsPipeline(RHIGraphicsPipelineHandle handle)
{
    D3D11Pipeline* pipeline = m_device.FindPipeline(handle);
    if (m_state != RHICommandListState::Recording || pipeline == nullptr) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 绑定了无效图形管线。");
    ID3D11DeviceContext* context = m_device.Context();
    context->IASetPrimitiveTopology(pipeline->Topology);
    context->IASetInputLayout(pipeline->InputLayout.Get());
    context->VSSetShader(pipeline->VertexShader.Get(), nullptr, 0);
    context->PSSetShader(pipeline->PixelShader.Get(), nullptr, 0);
    context->RSSetState(pipeline->Rasterizer.Get());
    const float blendFactor[] = {0, 0, 0, 0};
    context->OMSetBlendState(pipeline->Blend.Get(), blendFactor, 0xffffffffu);
    return RHIStatus::Ok();
}

RHIStatus D3D11CommandList::PushConstants(RHIShaderStage, std::uint32_t offset, std::span<const std::byte> data) { return m_device.SetPushConstants(offset, data); }

RHIStatus D3D11CommandList::BindVertexBuffers(std::uint32_t firstBinding, std::span<const RHIVertexBufferBinding> bindings)
{
    if (bindings.empty()) return RHIStatus::Ok();
    std::vector<ID3D11Buffer*> resources; std::vector<UINT> strides; std::vector<UINT> offsets;
    for (const auto& binding : bindings)
    {
        D3D11Buffer* buffer = m_device.FindBuffer(binding.Buffer);
        if (buffer == nullptr || binding.Offset > (std::numeric_limits<UINT>::max)()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 顶点缓冲区绑定无效。");
        resources.push_back(buffer->Resource.Get()); strides.push_back(sizeof(float) * 8); offsets.push_back(static_cast<UINT>(binding.Offset));
    }
    m_device.Context()->IASetVertexBuffers(firstBinding, static_cast<UINT>(resources.size()), resources.data(), strides.data(), offsets.data());
    return RHIStatus::Ok();
}

RHIStatus D3D11CommandList::BindIndexBuffer(const RHIIndexBufferBinding& binding)
{
    D3D11Buffer* buffer = m_device.FindBuffer(binding.Buffer);
    if (buffer == nullptr || binding.Offset > (std::numeric_limits<UINT>::max)()) return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 索引缓冲区绑定无效。");
    m_device.Context()->IASetIndexBuffer(buffer->Resource.Get(), binding.Type == RHIIndexType::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT, static_cast<UINT>(binding.Offset));
    return RHIStatus::Ok();
}

RHIStatus D3D11CommandList::SetViewports(std::uint32_t, std::span<const RHIViewport> viewports)
{
    std::vector<D3D11_VIEWPORT> native;
    for (const auto& view : viewports) native.push_back({view.X, view.Y, view.Width, view.Height, view.MinDepth, view.MaxDepth});
    m_device.Context()->RSSetViewports(static_cast<UINT>(native.size()), native.data());
    return RHIStatus::Ok();
}

RHIStatus D3D11CommandList::SetScissors(std::uint32_t, std::span<const RHIScissor> scissors)
{
    std::vector<D3D11_RECT> native;
    for (const auto& scissor : scissors) native.push_back({scissor.X, scissor.Y, scissor.X + static_cast<LONG>(scissor.Width), scissor.Y + static_cast<LONG>(scissor.Height)});
    m_device.Context()->RSSetScissorRects(static_cast<UINT>(native.size()), native.data());
    return RHIStatus::Ok();
}

RHIStatus D3D11CommandList::Draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::uint32_t) { m_device.Context()->DrawInstanced(count, instances, first, 0); return RHIStatus::Ok(); }
RHIStatus D3D11CommandList::DrawIndexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t offset, std::uint32_t) { m_device.Context()->DrawIndexedInstanced(count, instances, first, offset, 0); return RHIStatus::Ok(); }

class D3D11Backend final : public IRHIBackend
{
public:
    [[nodiscard]] const RHIBackendInfo& GetInfo() const noexcept override { return m_info; }
    RHIStatus EnumerateAdapters(std::vector<RHIAdapterInfo>& outAdapters) override;
    RHIStatus CreateDevice(const RHIDeviceDesc& desc, RHIDevicePtr& outDevice) override;
private:
    RHIBackendInfo m_info{{"d3d11"}, "Direct3D 11", {1, 1, 0}};
};

RHIStatus D3D11Backend::EnumerateAdapters(std::vector<RHIAdapterInfo>& outAdapters)
{
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return RHIStatus::Error(RHIResult::Failure, "无法创建 DXGI 工厂。");
    outAdapters.clear();
    for (UINT index = 0;; ++index)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break;
        DXGI_ADAPTER_DESC1 native{}; adapter->GetDesc1(&native);
        outAdapters.push_back({ToUtf8(native.Description), std::to_string(index), native.DedicatedVideoMemory, (native.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u});
    }
    return RHIStatus::Ok();
}

RHIStatus D3D11Backend::CreateDevice(const RHIDeviceDesc& desc, RHIDevicePtr& outDevice)
{
    ComPtr<IDXGIFactory6> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return RHIStatus::Error(RHIResult::Failure, "无法创建 DXGI 工厂。");
    UINT selected = 0;
    if (!desc.AdapterIdentifier.empty())
    {
        try { selected = static_cast<UINT>(std::stoul(desc.AdapterIdentifier)); }
        catch (...) { return RHIStatus::Error(RHIResult::InvalidArgument, "D3D11 适配器标识必须是枚举索引。"); }
    }
    ComPtr<IDXGIAdapter1> adapter;
    if (FAILED(factory->EnumAdapters1(selected, &adapter))) return RHIStatus::Error(RHIResult::InvalidArgument, "请求的 D3D11 适配器不存在。");
    UINT flags = desc.EnableValidation ? D3D11_CREATE_DEVICE_DEBUG : 0u;
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level{};
    HRESULT result = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(result) && desc.EnableValidation)
        result = D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(result)) return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D11 设备。");
    DXGI_ADAPTER_DESC1 native{}; adapter->GetDesc1(&native);
    outDevice = std::make_shared<D3D11Device>(RHIAdapterInfo{ToUtf8(native.Description), std::to_string(selected), native.DedicatedVideoMemory, (native.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u}, std::move(device), std::move(context));
    return RHIStatus::Ok();
}

class D3D11Factory final : public IRHIBackendFactory
{
public:
    [[nodiscard]] const RHIBackendInfo& GetInfo() const noexcept override { return m_info; }
    RHIStatus CreateBackend(RHIBackendPtr& outBackend) override { outBackend = std::make_shared<D3D11Backend>(); return RHIStatus::Ok(); }
private:
    RHIBackendInfo m_info{{"d3d11"}, "Direct3D 11", {1, 1, 0}};
};

} // namespace

RHIStatus RegisterD3D11Backend(RHIBackendRegistry& registry)
{
    return registry.RegisterFactory(std::make_shared<D3D11Factory>());
}

} // namespace RHI
