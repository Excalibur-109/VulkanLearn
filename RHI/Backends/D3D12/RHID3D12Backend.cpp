#include "RHI/Backends/RHINativeBackends.hpp"
#include "../Common/RHINativeDeviceBase.hpp"

#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

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

/** 将 RHI 颜色格式映射为 DXGI 格式。 */
DXGI_FORMAT ToDxgiFormat(const RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::RGBA8_UNorm_sRGB: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case RHIFormat::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case RHIFormat::BGRA8_UNorm_sRGB: return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case RHIFormat::RGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

/** 将 RHI 顶点格式映射为 DXGI 格式。 */
DXGI_FORMAT ToDxgiVertexFormat(const RHIVertexFormat format)
{
    switch (format)
    {
    case RHIVertexFormat::Float2: return DXGI_FORMAT_R32G32_FLOAT;
    case RHIVertexFormat::Float3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHIVertexFormat::Float4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

/** 按 RHI 属性位置选择 PBR HLSL 语义。 */
const char* SemanticName(const std::uint32_t location)
{
    switch (location)
    {
    case 0: return "POSITION";
    case 1: return "NORMAL";
    default: return "TEXCOORD";
    }
}

/** 编译 D3D12 PBR 使用的 HLSL 着色器。 */
RHIStatus CompileHlsl(const RHIShaderModuleDesc& module, const char* profile, ComPtr<ID3DBlob>& outBlob)
{
    if (module.CodeFormat != RHIShaderCodeFormat::SourceText || module.Code.empty())
        return RHIStatus::Error(RHIResult::Unsupported, "D3D12 PBR 路径只接受 HLSL 源码着色器模块。");
    ComPtr<ID3DBlob> diagnostics;
    const HRESULT result = D3DCompile(module.Code.data(), module.Code.size(), module.DebugName.c_str(), nullptr, nullptr,
        module.EntryPoint.c_str(), profile, D3DCOMPILE_ENABLE_STRICTNESS, 0, &outBlob, &diagnostics);
    if (FAILED(result))
    {
        std::string message = "D3D12 着色器编译失败。";
        if (diagnostics != nullptr) message.append(static_cast<const char*>(diagnostics->GetBufferPointer()), diagnostics->GetBufferSize());
        return RHIStatus::Error(RHIResult::Failure, std::move(message));
    }
    return RHIStatus::Ok();
}

/** 根据内存用途选择 D3D12 堆类型。 */
D3D12_HEAP_TYPE ToHeapType(const RHIMemoryUsage usage)
{
    return usage == RHIMemoryUsage::CpuToGpu ? D3D12_HEAP_TYPE_UPLOAD : D3D12_HEAP_TYPE_DEFAULT;
}

struct D3D12Buffer final { ComPtr<ID3D12Resource> Resource; RHIBufferDesc Desc; };
struct D3D12Texture final { ComPtr<ID3D12Resource> Resource; RHITextureDesc Desc; };
struct D3D12View final { ComPtr<ID3D12DescriptorHeap> Heap; D3D12_CPU_DESCRIPTOR_HANDLE Handle{}; };
struct D3D12Shader final { RHIShaderModuleDesc Desc; };
struct D3D12Fence final { ComPtr<ID3D12Fence> Fence; std::uint64_t Value = 0; };
struct D3D12Pipeline final { ComPtr<ID3D12RootSignature> RootSignature; ComPtr<ID3D12PipelineState> State; };

class D3D12Device;

/** D3D12 主命令列表，拥有一个命令分配器和原生命令列表。 */
class D3D12CommandList final : public IRHICommandList
{
public:
    /** 为所属 D3D12 设备创建一个图形主命令列表。 */
    D3D12CommandList(D3D12Device& device, RHICommandListDesc desc, ComPtr<ID3D12CommandAllocator> allocator, ComPtr<ID3D12GraphicsCommandList> commandList)
        : m_device(device), m_desc(std::move(desc)), m_allocator(std::move(allocator)), m_commandList(std::move(commandList)) {}
    /** 返回命令所属逻辑队列。 */
    [[nodiscard]] RHIQueueType GetQueueType() const noexcept override { return m_desc.QueueType; }
    /** 返回当前录制状态。 */
    [[nodiscard]] RHICommandListState GetState() const noexcept override { return m_state; }
    /** 返回创建命令列表时的描述。 */
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
    RHIStatus BindComputePipeline(RHIComputePipelineHandle) override { return RHINativeDeviceBase::Unsupported("D3D12 计算管线"); }
    RHIStatus BindRayTracingPipeline(RHIRayTracingPipelineHandle) override { return RHINativeDeviceBase::Unsupported("D3D12 光线追踪管线"); }
    RHIStatus BindSet(std::uint32_t, RHIBindSetHandle, std::span<const std::uint32_t>) override { return RHINativeDeviceBase::Unsupported("D3D12 绑定集"); }
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
    RHIStatus SetDepthBounds(float, float) override { return RHINativeDeviceBase::Unsupported("D3D12 深度范围"); }
    RHIStatus SetLineWidth(float) override { return RHINativeDeviceBase::Unsupported("D3D12 线宽"); }
    RHIStatus Draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::uint32_t firstInstance) override;
    RHIStatus DrawIndexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t offset, std::uint32_t firstInstance) override;
    RHIStatus DrawIndirect(RHIBufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D12 间接绘制"); }
    RHIStatus DrawIndexedIndirect(RHIBufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D12 间接索引绘制"); }
    RHIStatus DrawMeshTasks(std::uint32_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D12 网格着色"); }
    RHIStatus DrawMeshTasksIndirect(RHIBufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D12 间接网格着色"); }
    RHIStatus Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D12 计算调度"); }
    RHIStatus DispatchIndirect(RHIBufferHandle, std::uint64_t) override { return RHINativeDeviceBase::Unsupported("D3D12 间接计算调度"); }
    RHIStatus CopyBuffer(RHIBufferHandle, RHIBufferHandle, std::span<const RHIBufferCopyRegion>) override { return RHINativeDeviceBase::Unsupported("D3D12 缓冲区复制"); }
    RHIStatus CopyBufferToTexture(RHIBufferHandle, RHITextureHandle, std::span<const RHITextureBufferCopyRegion>) override { return RHINativeDeviceBase::Unsupported("D3D12 缓冲区到纹理复制"); }
    RHIStatus CopyTextureToBuffer(RHITextureHandle, RHIBufferHandle, std::span<const RHITextureBufferCopyRegion>) override { return RHINativeDeviceBase::Unsupported("D3D12 纹理到缓冲区复制"); }
    RHIStatus CopyTexture(RHITextureHandle, RHITextureHandle, std::span<const RHITextureCopyRegion>) override { return RHINativeDeviceBase::Unsupported("D3D12 纹理复制"); }
    RHIStatus ResolveTexture(RHITextureHandle, RHITextureHandle, std::span<const RHITextureCopyRegion>, RHIResolveMode) override { return RHINativeDeviceBase::Unsupported("D3D12 多重采样解析"); }
    RHIStatus ClearColorTexture(RHITextureHandle, const RHIClearColorValue&, std::span<const RHISubresourceRange>) override { return RHINativeDeviceBase::Unsupported("D3D12 纹理清除"); }
    RHIStatus ClearDepthStencilTexture(RHITextureHandle, const RHIClearDepthStencilValue&, std::span<const RHISubresourceRange>) override { return RHINativeDeviceBase::Unsupported("D3D12 深度清除"); }
    RHIStatus BeginQuery(RHIQueryPoolHandle, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D12 查询"); }
    RHIStatus EndQuery(RHIQueryPoolHandle, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D12 查询"); }
    RHIStatus WriteTimestamp(RHIQueryPoolHandle, std::uint32_t, RHIPipelineStage) override { return RHINativeDeviceBase::Unsupported("D3D12 时间戳"); }
    RHIStatus ResolveQueryData(RHIQueryPoolHandle, std::uint32_t, std::uint32_t, RHIBufferHandle, std::uint64_t) override { return RHINativeDeviceBase::Unsupported("D3D12 查询解析"); }
    RHIStatus BuildAccelerationStructure(const RHIAccelerationStructureBuildDesc&) override { return RHINativeDeviceBase::Unsupported("D3D12 加速结构"); }
    RHIStatus TraceRays(const RHIShaderTableDesc&, std::uint32_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("D3D12 光线追踪"); }
    RHIStatus ExecuteSecondary(std::span<IRHICommandList* const>) override { return RHINativeDeviceBase::Unsupported("D3D12 次级命令列表"); }

    /** 返回待提交的原生命令列表。 */
    ID3D12GraphicsCommandList* Native() const noexcept { return m_commandList.Get(); }

private:
    D3D12Device& m_device;
    RHICommandListDesc m_desc;
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    RHICommandListState m_state = RHICommandListState::Initial;
    bool m_rendering = false;
};

/** D3D12 原生设备，覆盖共享 PBR 前端所需的真实图形调用。 */
class D3D12Device final : public RHINativeDeviceBase
{
public:
    D3D12Device(RHIAdapterInfo adapter, ComPtr<ID3D12Device> device, ComPtr<ID3D12CommandQueue> queue)
        : RHINativeDeviceBase(std::move(adapter)), m_device(std::move(device)), m_queue(std::move(queue))
    {
        m_limits.MaxTextureDimension2D = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
        m_limits.MaxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        m_limits.MaxVertexBuffers = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        m_limits.MaxPushConstantBytes = 128;
        m_limits.MinConstantBufferOffsetAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    }
    RHIStatus CreateBuffer(const RHIBufferDesc& desc, RHIBufferHandle& outBuffer) override;
    void DestroyBuffer(RHIBufferHandle handle) override { std::scoped_lock lock(m_mutex); m_buffers.erase(handle.Value); }
    RHIStatus CreateTexture(const RHITextureDesc& desc, RHITextureHandle& outTexture) override;
    void DestroyTexture(RHITextureHandle handle) override { std::scoped_lock lock(m_mutex); m_textures.erase(handle.Value); }
    RHIStatus CreateTextureView(const RHITextureViewDesc& desc, RHITextureViewHandle& outView) override;
    void DestroyTextureView(RHITextureViewHandle handle) override { std::scoped_lock lock(m_mutex); m_views.erase(handle.Value); }
    RHIStatus MapBuffer(RHIBufferHandle handle, std::uint64_t offset, std::uint64_t size, void*& outData) override;
    RHIStatus FlushMappedBufferRange(RHIBufferHandle, std::uint64_t, std::uint64_t) override { return RHIStatus::Ok(); }
    RHIStatus InvalidateMappedBufferRange(RHIBufferHandle, std::uint64_t, std::uint64_t) override { return RHIStatus::Ok(); }
    RHIStatus UnmapBuffer(RHIBufferHandle handle) override;
    RHIStatus CreateShaderModule(const RHIShaderModuleDesc& desc, RHIShaderModuleHandle& outModule) override;
    void DestroyShaderModule(RHIShaderModuleHandle handle) override { std::scoped_lock lock(m_mutex); m_shaders.erase(handle.Value); }
    RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, RHIGraphicsPipelineHandle& outPipeline) override;
    void DestroyGraphicsPipeline(RHIGraphicsPipelineHandle handle) override { std::scoped_lock lock(m_mutex); m_pipelines.erase(handle.Value); }
    RHIStatus CreateCommandList(const RHICommandListDesc& desc, RHICommandListPtr& outList) override;
    RHIStatus CreateFence(const RHIFenceDesc& desc, RHIFenceHandle& outFence) override;
    void DestroyFence(RHIFenceHandle handle) override { std::scoped_lock lock(m_mutex); m_fences.erase(handle.Value); }
    RHIStatus WaitForFence(RHIFenceHandle handle, std::uint64_t timeoutNanoseconds) override;
    RHIStatus ResetFence(RHIFenceHandle handle) override;
    RHIStatus Submit(RHIQueueType queue, const RHIQueueSubmitDesc& desc) override;
    RHIStatus WaitIdle() override;

    /** 查找原生缓冲区。 */
    D3D12Buffer* FindBuffer(RHIBufferHandle handle) { auto it = m_buffers.find(handle.Value); return it == m_buffers.end() ? nullptr : &it->second; }
    /** 查找原生渲染目标视图。 */
    D3D12View* FindView(RHITextureViewHandle handle) { auto it = m_views.find(handle.Value); return it == m_views.end() ? nullptr : &it->second; }
    /** 查找原生管线。 */
    D3D12Pipeline* FindPipeline(RHIGraphicsPipelineHandle handle) { auto it = m_pipelines.find(handle.Value); return it == m_pipelines.end() ? nullptr : &it->second; }

private:
    std::uint64_t NextHandle() noexcept { return m_nextHandle++; }
    RHIStatus WaitNativeFence(ID3D12Fence& fence, std::uint64_t value, std::uint64_t timeoutNanoseconds);
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12CommandQueue> m_queue;
    ComPtr<ID3D12Fence> m_idleFence;
    std::uint64_t m_idleValue = 0;
    std::uint64_t m_nextHandle = 1;
    std::mutex m_mutex;
    std::unordered_map<std::uint64_t, D3D12Buffer> m_buffers;
    std::unordered_map<std::uint64_t, D3D12Texture> m_textures;
    std::unordered_map<std::uint64_t, D3D12View> m_views;
    std::unordered_map<std::uint64_t, D3D12Shader> m_shaders;
    std::unordered_map<std::uint64_t, D3D12Pipeline> m_pipelines;
    std::unordered_map<std::uint64_t, D3D12Fence> m_fences;
};

RHIStatus D3D12Device::CreateBuffer(const RHIBufferDesc& desc, RHIBufferHandle& outBuffer)
{
    if (desc.SizeInBytes == 0)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 缓冲区大小无效。");
    }

    // D3D12 将内存堆和资源描述一起传给 CreateCommittedResource。
    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = ToHeapType(desc.MemoryUsage);

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Width = desc.SizeInBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> native;
    const D3D12_RESOURCE_STATES initialState = heapProperties.Type == D3D12_HEAP_TYPE_UPLOAD
        ? D3D12_RESOURCE_STATE_GENERIC_READ
        : D3D12_RESOURCE_STATE_COMMON;
    if (FAILED(m_device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            initialState,
            nullptr,
            IID_PPV_ARGS(&native))))
    {
        return RHIStatus::Error(RHIResult::OutOfMemory, "无法创建 D3D12 缓冲区。");
    }

    // 原生资源由 ComPtr 管理，RHI 只向前端暴露稳定句柄。
    std::scoped_lock lock(m_mutex);
    outBuffer.Value = NextHandle();
    m_buffers.emplace(outBuffer.Value, D3D12Buffer{std::move(native), desc});
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::CreateTexture(const RHITextureDesc& desc, RHITextureHandle& outTexture)
{
    const bool isColorAttachment = (static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(RHITextureUsage::ColorAttachment)) != 0u;
    if (desc.Dimension != RHITextureDimension::Texture2D || !desc.Extent.IsValid() ||
        ToDxgiFormat(desc.Format) == DXGI_FORMAT_UNKNOWN || !isColorAttachment)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 PBR 路径需要有效二维颜色附件纹理。");
    }

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = desc.Extent.Width;
    resourceDesc.Height = desc.Extent.Height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = ToDxgiFormat(desc.Format);
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue{};
    clearValue.Format = resourceDesc.Format;

    ComPtr<ID3D12Resource> native;
    if (FAILED(m_device->CreateCommittedResource(
            &heapProperties,
            D3D12_HEAP_FLAG_NONE,
            &resourceDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            &clearValue,
            IID_PPV_ARGS(&native))))
    {
        return RHIStatus::Error(RHIResult::OutOfMemory, "无法创建 D3D12 颜色附件纹理。");
    }

    std::scoped_lock lock(m_mutex);
    outTexture.Value = NextHandle();
    m_textures.emplace(outTexture.Value, D3D12Texture{std::move(native), desc});
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::CreateTextureView(const RHITextureViewDesc& desc, RHITextureViewHandle& outView)
{
    std::scoped_lock lock(m_mutex);
    const auto texture = m_textures.find(desc.Texture.Value);
    if (texture == m_textures.end())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 纹理视图引用了未知纹理。");
    }

    // PBR 只需要一个 RTV，所以每个视图拥有一个只含一个描述符的 CPU 堆。
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heapDesc.NumDescriptors = 1;

    D3D12View view;
    if (FAILED(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&view.Heap))))
    {
        return RHIStatus::Error(RHIResult::OutOfMemory, "无法创建 D3D12 RTV 描述符堆。");
    }
    view.Handle = view.Heap->GetCPUDescriptorHandleForHeapStart();
    m_device->CreateRenderTargetView(texture->second.Resource.Get(), nullptr, view.Handle);

    outView.Value = NextHandle();
    m_views.emplace(outView.Value, std::move(view));
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::MapBuffer(RHIBufferHandle handle, std::uint64_t offset, std::uint64_t size, void*& outData)
{
    std::scoped_lock lock(m_mutex);
    D3D12Buffer* buffer = FindBuffer(handle);
    if (buffer == nullptr || buffer->Desc.MemoryUsage != RHIMemoryUsage::CpuToGpu ||
        offset + size > buffer->Desc.SizeInBytes)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 映射范围无效或缓冲区不可写。");
    }

    void* mapped = nullptr;
    if (FAILED(buffer->Resource->Map(0, nullptr, &mapped)))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法映射 D3D12 上传缓冲区。");
    }

    outData = static_cast<std::byte*>(mapped) + offset;
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::UnmapBuffer(RHIBufferHandle handle)
{
    std::scoped_lock lock(m_mutex);
    D3D12Buffer* buffer = FindBuffer(handle);
    if (buffer == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 取消映射引用了未知缓冲区。");
    }

    buffer->Resource->Unmap(0, nullptr);
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::CreateShaderModule(const RHIShaderModuleDesc& desc, RHIShaderModuleHandle& outModule)
{
    if (desc.Code.empty())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 着色器模块代码为空。");
    }

    // D3D12 在创建图形管线时才编译 HLSL；此处保存前端提交的模块描述。
    std::scoped_lock lock(m_mutex);
    outModule.Value = NextHandle();
    m_shaders.emplace(outModule.Value, D3D12Shader{desc});
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, RHIGraphicsPipelineHandle& outPipeline)
{
    std::scoped_lock lock(m_mutex);

    // 第 1 步：从通用管线描述中找出本路径所需的两个着色器阶段。
    const RHIShaderStageDesc* vertexStage = nullptr;
    const RHIShaderStageDesc* pixelStage = nullptr;
    for (const RHIShaderStageDesc& stage : desc.ShaderStages)
    {
        if (stage.Stage == RHIShaderStage::Vertex)
        {
            vertexStage = &stage;
        }
        else if (stage.Stage == RHIShaderStage::Pixel)
        {
            pixelStage = &stage;
        }
    }
    if (vertexStage == nullptr || pixelStage == nullptr ||
        desc.RenderTargets.ColorFormats.size() != 1 || desc.VertexBuffers.empty())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 PBR 图形管线描述不完整。");
    }

    const auto vertexModule = m_shaders.find(vertexStage->Module.Value);
    const auto pixelModule = m_shaders.find(pixelStage->Module.Value);
    if (vertexModule == m_shaders.end() || pixelModule == m_shaders.end())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 图形管线引用未知着色器。");
    }

    // 第 2 步：从模块描述复制入口点，并编译 HLSL 到 D3D12 所需字节码。
    RHIShaderModuleDesc vertexShaderDesc = vertexModule->second.Desc;
    vertexShaderDesc.EntryPoint = vertexStage->EntryPoint;
    RHIShaderModuleDesc pixelShaderDesc = pixelModule->second.Desc;
    pixelShaderDesc.EntryPoint = pixelStage->EntryPoint;

    ComPtr<ID3DBlob> vertexBlob;
    RHIStatus status = CompileHlsl(vertexShaderDesc, "vs_5_1", vertexBlob);
    if (!status.Succeeded())
    {
        return status;
    }

    ComPtr<ID3DBlob> pixelBlob;
    status = CompileHlsl(pixelShaderDesc, "ps_5_1", pixelBlob);
    if (!status.Succeeded())
    {
        return status;
    }

    // 第 3 步：根签名声明 b0 上的 32 个 32 位常量，即 128 字节 PBR 参数。
    D3D12_ROOT_PARAMETER rootParameter{};
    rootParameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParameter.Constants.ShaderRegister = 0;
    rootParameter.Constants.Num32BitValues = 32;
    rootParameter.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc{};
    rootSignatureDesc.NumParameters = 1;
    rootSignatureDesc.pParameters = &rootParameter;
    rootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> rootSignatureBlob;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(
            &rootSignatureDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &rootSignatureBlob,
            &errors)))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法序列化 D3D12 PBR 根签名。");
    }

    D3D12Pipeline pipeline;
    if (FAILED(m_device->CreateRootSignature(
            0,
            rootSignatureBlob->GetBufferPointer(),
            rootSignatureBlob->GetBufferSize(),
            IID_PPV_ARGS(&pipeline.RootSignature))))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D12 PBR 根签名。");
    }

    // 第 4 步：把 RHI 顶点属性展开为 D3D12 输入汇编器的元素描述。
    std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
    for (UINT slot = 0; slot < desc.VertexBuffers.size(); ++slot)
    {
        const RHIVertexBufferLayout& vertexBuffer = desc.VertexBuffers[slot];
        for (const RHIVertexAttribute& attribute : vertexBuffer.Attributes)
        {
            D3D12_INPUT_ELEMENT_DESC element{};
            element.SemanticName = SemanticName(attribute.Location);
            element.SemanticIndex = attribute.Location > 1 ? attribute.Location - 2 : 0;
            element.Format = ToDxgiVertexFormat(attribute.Format);
            element.InputSlot = slot;
            element.AlignedByteOffset = attribute.Offset;
            element.InputSlotClass = vertexBuffer.StepMode == RHIVertexStepMode::PerInstance
                ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            element.InstanceDataStepRate = 0;
            elements.push_back(element);
        }
    }

    // 第 5 步：将根签名、着色器、输入布局与光栅状态组合成 PSO。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
    state.pRootSignature = pipeline.RootSignature.Get();
    state.VS = {vertexBlob->GetBufferPointer(), vertexBlob->GetBufferSize()};
    state.PS = {pixelBlob->GetBufferPointer(), pixelBlob->GetBufferSize()};
    state.InputLayout = {elements.data(), static_cast<UINT>(elements.size())};
    state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state.SampleMask = UINT_MAX;
    state.NumRenderTargets = 1;
    state.RTVFormats[0] = ToDxgiFormat(desc.RenderTargets.ColorFormats[0]);
    state.SampleDesc.Count = 1;
    state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    state.RasterizerState.CullMode = desc.Rasterizer.CullMode == RHICullMode::None
        ? D3D12_CULL_MODE_NONE
        : desc.Rasterizer.CullMode == RHICullMode::Front ? D3D12_CULL_MODE_FRONT : D3D12_CULL_MODE_BACK;
    state.RasterizerState.DepthClipEnable = TRUE;
    state.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    if (FAILED(m_device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&pipeline.State))))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D12 PBR 图形管线。");
    }

    outPipeline.Value = NextHandle();
    m_pipelines.emplace(outPipeline.Value, std::move(pipeline));
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::CreateCommandList(const RHICommandListDesc& desc, RHICommandListPtr& outList)
{
    if (desc.QueueType != RHIQueueType::Graphics || desc.Level != RHICommandListLevel::Primary)
    {
        return RHIStatus::Error(RHIResult::Unsupported, "D3D12 PBR 路径只支持图形主命令列表。");
    }

    // 分配器存放命令字节码；命令列表负责向其中录制 GPU 命令。
    ComPtr<ID3D12CommandAllocator> allocator;
    if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D12 命令分配器。");
    }

    ComPtr<ID3D12GraphicsCommandList> commandList;
    if (FAILED(m_device->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            allocator.Get(),
            nullptr,
            IID_PPV_ARGS(&commandList))))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D12 命令列表。");
    }

    // D3D12 新建的命令列表处于打开状态，而 RHI 约定新列表从 Initial 状态开始。
    commandList->Close();
    outList = std::make_shared<D3D12CommandList>(*this, desc, std::move(allocator), std::move(commandList));
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::CreateFence(const RHIFenceDesc& desc, RHIFenceHandle& outFence)
{
    D3D12Fence native;
    const UINT64 initialValue = desc.InitiallySignaled ? 1 : 0;
    if (FAILED(m_device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&native.Fence))))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D12 围栏。");
    }

    native.Value = initialValue;
    std::scoped_lock lock(m_mutex);
    outFence.Value = NextHandle();
    m_fences.emplace(outFence.Value, std::move(native));
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::WaitNativeFence(ID3D12Fence& fence, const std::uint64_t value, const std::uint64_t timeoutNanoseconds)
{
    if (fence.GetCompletedValue() >= value)
    {
        return RHIStatus::Ok();
    }

    HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (eventHandle == nullptr)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D12 围栏等待事件。");
    }

    const HRESULT result = fence.SetEventOnCompletion(value, eventHandle);
    const DWORD timeout = timeoutNanoseconds == (std::numeric_limits<std::uint64_t>::max)()
        ? INFINITE
        : static_cast<DWORD>(std::min<std::uint64_t>(timeoutNanoseconds / 1000000u, MAXDWORD));
    const DWORD waitResult = SUCCEEDED(result) ? WaitForSingleObject(eventHandle, timeout) : WAIT_FAILED;
    CloseHandle(eventHandle);

    if (waitResult == WAIT_OBJECT_0)
    {
        return RHIStatus::Ok();
    }
    return RHIStatus::Error(
        waitResult == WAIT_TIMEOUT ? RHIResult::NotReady : RHIResult::Failure,
        "等待 D3D12 围栏失败或超时。");
}

RHIStatus D3D12Device::WaitForFence(RHIFenceHandle handle, std::uint64_t timeoutNanoseconds)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_fences.find(handle.Value);
    if (it == m_fences.end())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 等待引用了未知围栏。");
    }

    return WaitNativeFence(*it->second.Fence.Get(), it->second.Value, timeoutNanoseconds);
}

RHIStatus D3D12Device::ResetFence(RHIFenceHandle handle)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_fences.find(handle.Value);
    if (it == m_fences.end())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 重置引用了未知围栏。");
    }

    // RHI 的重置语义是让下次等待基于当前已完成值。
    it->second.Value = it->second.Fence->GetCompletedValue();
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::Submit(RHIQueueType queue, const RHIQueueSubmitDesc& desc)
{
    if (queue != RHIQueueType::Graphics)
    {
        return RHIStatus::Error(RHIResult::Unsupported, "D3D12 PBR 路径只支持图形队列。");
    }

    std::vector<ID3D12CommandList*> nativeCommandLists;
    nativeCommandLists.reserve(desc.CommandLists.size());
    for (const RHICommandListPtr& list : desc.CommandLists)
    {
        const auto d3dList = std::dynamic_pointer_cast<D3D12CommandList>(list);
        if (d3dList == nullptr || d3dList->GetState() != RHICommandListState::Executable)
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 提交包含无效命令列表。");
        }
        nativeCommandLists.push_back(d3dList->Native());
    }

    if (!nativeCommandLists.empty())
    {
        m_queue->ExecuteCommandLists(static_cast<UINT>(nativeCommandLists.size()), nativeCommandLists.data());
    }

    if (desc.SignalFence.IsValid())
    {
        std::scoped_lock lock(m_mutex);
        const auto it = m_fences.find(desc.SignalFence.Value);
        if (it == m_fences.end())
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 提交引用了未知围栏。");
        }

        ++it->second.Value;
        if (FAILED(m_queue->Signal(it->second.Fence.Get(), it->second.Value)))
        {
            return RHIStatus::Error(RHIResult::Failure, "无法提交 D3D12 围栏信号。");
        }
    }
    return RHIStatus::Ok();
}

RHIStatus D3D12Device::WaitIdle()
{
    std::scoped_lock lock(m_mutex);
    if (m_idleFence == nullptr &&
        FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_idleFence))))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D12 空闲围栏。");
    }

    ++m_idleValue;
    if (FAILED(m_queue->Signal(m_idleFence.Get(), m_idleValue)))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法发送 D3D12 空闲围栏。");
    }
    return WaitNativeFence(*m_idleFence.Get(), m_idleValue, (std::numeric_limits<std::uint64_t>::max)());
}

RHIStatus D3D12CommandList::Reset()
{
    if (m_state == RHICommandListState::Pending)
    {
        return RHIStatus::Error(RHIResult::NotReady, "D3D12 命令列表仍在执行。");
    }

    // 真正的原生命令列表重置发生在 Begin；这里先恢复 RHI 状态机。
    m_state = RHICommandListState::Initial;
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::Begin()
{
    if (m_state != RHICommandListState::Initial)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 命令列表必须处于初始状态才能开始录制。");
    }
    if (FAILED(m_allocator->Reset()))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法重置 D3D12 命令分配器。");
    }
    if (FAILED(m_commandList->Reset(m_allocator.Get(), nullptr)))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法开始 D3D12 命令列表。");
    }

    m_state = RHICommandListState::Recording;
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::End()
{
    if (m_state != RHICommandListState::Recording || m_rendering)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "结束 D3D12 命令列表前必须结束渲染区域。");
    }
    if (FAILED(m_commandList->Close()))
    {
        return RHIStatus::Error(RHIResult::Failure, "无法结束 D3D12 命令列表。");
    }

    m_state = RHICommandListState::Executable;
    return RHIStatus::Ok();
}
RHIStatus D3D12CommandList::BeginRendering(const RHIRenderingDesc& desc)
{
    if (m_state != RHICommandListState::Recording || m_rendering || desc.ColorAttachments.size() != 1)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 PBR 需要一个颜色附件。");
    }

    D3D12View* view = m_device.FindView(desc.ColorAttachments[0].View);
    if (view == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 渲染区域引用了无效视图。");
    }

    // RTV 描述符已经在 CreateTextureView 中创建，此处只绑定它。
    m_commandList->OMSetRenderTargets(1, &view->Handle, FALSE, nullptr);

    if (desc.ColorAttachments[0].LoadOp == RHILoadOp::Clear)
    {
        const auto& color = desc.ColorAttachments[0].ClearValue;
        const float values[] = {color.R, color.G, color.B, color.A};
        m_commandList->ClearRenderTargetView(view->Handle, values, 0, nullptr);
    }

    m_rendering = true;
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::EndRendering()
{
    if (!m_rendering)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 当前没有活跃渲染区域。");
    }

    m_rendering = false;
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::BindGraphicsPipeline(RHIGraphicsPipelineHandle handle)
{
    D3D12Pipeline* pipeline = m_device.FindPipeline(handle);
    if (m_state != RHICommandListState::Recording || pipeline == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 绑定了无效图形管线。");
    }

    // 根签名规定常量如何传入，PSO 固定着色器、输入布局和光栅状态。
    m_commandList->SetGraphicsRootSignature(pipeline->RootSignature.Get());
    m_commandList->SetPipelineState(pipeline->State.Get());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::PushConstants(RHIShaderStage, std::uint32_t offset, std::span<const std::byte> data)
{
    if ((offset % 4) != 0 || (data.size() % 4) != 0 || offset + data.size() > 128)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 根常量范围无效。");
    }

    m_commandList->SetGraphicsRoot32BitConstants(
        0,
        static_cast<UINT>(data.size() / 4),
        data.data(),
        offset / 4);
    return RHIStatus::Ok();
}
RHIStatus D3D12CommandList::BindVertexBuffers(std::uint32_t firstBinding, std::span<const RHIVertexBufferBinding> bindings)
{
    std::vector<D3D12_VERTEX_BUFFER_VIEW> views;
    views.reserve(bindings.size());

    for (const RHIVertexBufferBinding& binding : bindings)
    {
        D3D12Buffer* buffer = m_device.FindBuffer(binding.Buffer);
        if (buffer == nullptr || binding.Offset > buffer->Desc.SizeInBytes)
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 顶点缓冲区绑定无效。");
        }

        D3D12_VERTEX_BUFFER_VIEW view{};
        view.BufferLocation = buffer->Resource->GetGPUVirtualAddress() + binding.Offset;
        view.SizeInBytes = static_cast<UINT>(buffer->Desc.SizeInBytes - binding.Offset);
        // 当前教学 PBR 管线的顶点固定为 position + normal + uv，共 8 个 float。
        view.StrideInBytes = sizeof(float) * 8;
        views.push_back(view);
    }

    m_commandList->IASetVertexBuffers(firstBinding, static_cast<UINT>(views.size()), views.data());
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::BindIndexBuffer(const RHIIndexBufferBinding& binding)
{
    D3D12Buffer* buffer = m_device.FindBuffer(binding.Buffer);
    if (buffer == nullptr || binding.Offset > buffer->Desc.SizeInBytes)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 索引缓冲区绑定无效。");
    }

    D3D12_INDEX_BUFFER_VIEW view{};
    view.BufferLocation = buffer->Resource->GetGPUVirtualAddress() + binding.Offset;
    view.SizeInBytes = static_cast<UINT>(buffer->Desc.SizeInBytes - binding.Offset);
    view.Format = binding.Type == RHIIndexType::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    m_commandList->IASetIndexBuffer(&view);
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::SetViewports(std::uint32_t, std::span<const RHIViewport> viewports)
{
    std::vector<D3D12_VIEWPORT> nativeViewports;
    nativeViewports.reserve(viewports.size());
    for (const RHIViewport& viewport : viewports)
    {
        nativeViewports.push_back({
            viewport.X,
            viewport.Y,
            viewport.Width,
            viewport.Height,
            viewport.MinDepth,
            viewport.MaxDepth,
        });
    }

    m_commandList->RSSetViewports(static_cast<UINT>(nativeViewports.size()), nativeViewports.data());
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::SetScissors(std::uint32_t, std::span<const RHIScissor> scissors)
{
    std::vector<D3D12_RECT> nativeScissors;
    nativeScissors.reserve(scissors.size());
    for (const RHIScissor& scissor : scissors)
    {
        nativeScissors.push_back({
            scissor.X,
            scissor.Y,
            scissor.X + static_cast<LONG>(scissor.Width),
            scissor.Y + static_cast<LONG>(scissor.Height),
        });
    }

    m_commandList->RSSetScissorRects(static_cast<UINT>(nativeScissors.size()), nativeScissors.data());
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::Draw(
    std::uint32_t count,
    std::uint32_t instances,
    std::uint32_t first,
    std::uint32_t firstInstance)
{
    m_commandList->DrawInstanced(count, instances, first, firstInstance);
    return RHIStatus::Ok();
}

RHIStatus D3D12CommandList::DrawIndexed(
    std::uint32_t count,
    std::uint32_t instances,
    std::uint32_t first,
    std::int32_t offset,
    std::uint32_t firstInstance)
{
    m_commandList->DrawIndexedInstanced(count, instances, first, offset, firstInstance);
    return RHIStatus::Ok();
}

class D3D12Backend final : public IRHIBackend
{
public:
    [[nodiscard]] const RHIBackendInfo& GetInfo() const noexcept override { return m_info; }
    RHIStatus EnumerateAdapters(std::vector<RHIAdapterInfo>& outAdapters) override;
    RHIStatus CreateDevice(const RHIDeviceDesc& desc, RHIDevicePtr& outDevice) override;
private:
    RHIBackendInfo m_info{{"d3d12"}, "Direct3D 12", {1, 1, 0}};
};

RHIStatus D3D12Backend::EnumerateAdapters(std::vector<RHIAdapterInfo>& outAdapters)
{
    ComPtr<IDXGIFactory6> factory; if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return RHIStatus::Error(RHIResult::Failure, "无法创建 DXGI 工厂。");
    outAdapters.clear(); for (UINT index = 0;; ++index) { ComPtr<IDXGIAdapter1> adapter; if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) break; DXGI_ADAPTER_DESC1 native{}; adapter->GetDesc1(&native); outAdapters.push_back({ToUtf8(native.Description), std::to_string(index), native.DedicatedVideoMemory, (native.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u}); } return RHIStatus::Ok();
}

RHIStatus D3D12Backend::CreateDevice(const RHIDeviceDesc& desc, RHIDevicePtr& outDevice)
{
    ComPtr<IDXGIFactory6> factory; if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return RHIStatus::Error(RHIResult::Failure, "无法创建 DXGI 工厂。");
    UINT selected = 0; if (!desc.AdapterIdentifier.empty()) { try { selected = static_cast<UINT>(std::stoul(desc.AdapterIdentifier)); } catch (...) { return RHIStatus::Error(RHIResult::InvalidArgument, "D3D12 适配器标识必须是枚举索引。"); } }
    ComPtr<IDXGIAdapter1> adapter; if (FAILED(factory->EnumAdapters1(selected, &adapter))) return RHIStatus::Error(RHIResult::InvalidArgument, "请求的 D3D12 适配器不存在。");
    ComPtr<ID3D12Device> device; if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D12 设备。");
    D3D12_COMMAND_QUEUE_DESC queueDesc{}; queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; ComPtr<ID3D12CommandQueue> queue; if (FAILED(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)))) return RHIStatus::Error(RHIResult::Failure, "无法创建 D3D12 图形队列。");
    DXGI_ADAPTER_DESC1 native{}; adapter->GetDesc1(&native); outDevice = std::make_shared<D3D12Device>(RHIAdapterInfo{ToUtf8(native.Description), std::to_string(selected), native.DedicatedVideoMemory, (native.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0u}, std::move(device), std::move(queue)); return RHIStatus::Ok();
}

class D3D12Factory final : public IRHIBackendFactory
{
public:
    [[nodiscard]] const RHIBackendInfo& GetInfo() const noexcept override { return m_info; }
    RHIStatus CreateBackend(RHIBackendPtr& outBackend) override { outBackend = std::make_shared<D3D12Backend>(); return RHIStatus::Ok(); }
private:
    RHIBackendInfo m_info{{"d3d12"}, "Direct3D 12", {1, 1, 0}};
};

} // namespace

RHIStatus RegisterD3D12Backend(RHIBackendRegistry& registry)
{
    return registry.RegisterFactory(std::make_shared<D3D12Factory>());
}

} // namespace RHI
