#pragma once

#include "RHI/RHICommands.hpp"

#include <memory>

namespace RHI
{

/** Defines optional device capabilities exposed by a backend. */
struct RHIDeviceFeatures final
{
    /** True when asynchronous compute queues are usable. */
    bool AsyncCompute = false;
    /** True when asynchronous copy queues are usable. */
    bool AsyncCopy = false;
    /** True when timestamp queries are usable. */
    bool TimestampQueries = false;
    /** True when occlusion queries are usable. */
    bool OcclusionQueries = false;
    /** True when pipeline-statistics queries are usable. */
    bool PipelineStatisticsQueries = false;
    /** True when bindless descriptor arrays are usable. */
    bool Bindless = false;
    /** True when descriptor updates after binding are usable. */
    bool UpdateAfterBind = false;
    /** True when variable-rate shading is usable. */
    bool VariableRateShading = false;
    /** True when multiview rendering is usable. */
    bool Multiview = false;
    /** True when ray tracing and acceleration structures are usable. */
    bool RayTracing = false;
    /** True when mesh shading is usable. */
    bool MeshShading = false;
    /** True when conservative rasterization is usable. */
    bool ConservativeRasterization = false;
};

/** Defines numeric device limits relevant to portable rendering code. */
struct RHIDeviceLimits final
{
    /** Maximum two-dimensional texture extent. */
    std::uint32_t MaxTextureDimension2D = 0;
    /** Maximum three-dimensional texture extent. */
    std::uint32_t MaxTextureDimension3D = 0;
    /** Maximum texture array layers. */
    std::uint32_t MaxTextureArrayLayers = 0;
    /** Maximum number of color attachments in one rendering region. */
    std::uint32_t MaxColorAttachments = 0;
    /** Maximum vertex-buffer bindings in one graphics pipeline. */
    std::uint32_t MaxVertexBuffers = 0;
    /** Maximum byte size of all inline constants in one pipeline layout. */
    std::uint32_t MaxPushConstantBytes = 0;
    /** Required alignment of constant-buffer offsets. */
    std::uint32_t MinConstantBufferOffsetAlignment = 1;
    /** Required alignment of storage-buffer offsets. */
    std::uint32_t MinStorageBufferOffsetAlignment = 1;
    /** Maximum descriptor bindings in one bind group. */
    std::uint32_t MaxBindingsPerBindSet = 0;
    /** Maximum dispatch group count in each dimension. */
    std::array<std::uint32_t, 3> MaxComputeWorkGroupCount{};
    /** Maximum compute invocations in one work group. */
    std::uint32_t MaxComputeWorkGroupInvocations = 0;
    /** Timestamp period in nanoseconds, or zero if timestamps are unsupported. */
    double TimestampPeriodNanoseconds = 0.0;
};

/** Identifies a physical adapter selected by an RHI backend. */
struct RHIAdapterInfo final
{
    /** Human-readable adapter name. */
    std::string Name;
    /** Backend-provider-assigned adapter identifier. */
    std::string Identifier;
    /** Dedicated local memory available to the adapter in bytes. */
    std::uint64_t DedicatedMemoryBytes = 0;
    /** True when the adapter is software-emulated. */
    bool IsSoftware = false;
};

/** Describes device selection and diagnostics preferences. */
struct RHIDeviceDesc final
{
    /** Debug-only name used in diagnostics and captures. */
    std::string DebugName;
    /** Optional adapter identifier requested by the application. */
    std::string AdapterIdentifier;
    /** Enables backend validation where the implementation offers it. */
    bool EnableValidation = false;
    /** Enables backend diagnostic markers and object names. */
    bool EnableDebugNames = true;
    /** Required optional device capabilities. */
    RHIDeviceFeatures RequiredFeatures{};
};

/** Describes a queue submission. */
struct RHIQueueSubmitDesc final
{
    /** Command lists submitted in execution order. */
    std::vector<RHICommandListPtr> CommandLists;
    /** Semaphores to wait before execution. */
    std::vector<RHISemaphoreWait> WaitSemaphores;
    /** Semaphores to signal after execution. */
    std::vector<RHISemaphoreSignal> SignalSemaphores;
    /** Optional fence signaled when all submitted work completes. */
    RHIFenceHandle SignalFence{};
};

/** Describes a presentation request for one acquired swapchain image. */
struct RHIPresentDesc final
{
    /** Swapchain receiving the presentation request. */
    RHISwapchainHandle Swapchain{};
    /** Image index returned by acquisition. */
    std::uint32_t ImageIndex = 0;
    /** Semaphore that presentation waits on. */
    RHISemaphoreHandle WaitSemaphore{};
};

/** Defines a full abstract rendering device independent of an underlying graphics API. */
class IRHIDevice
{
public:
    /** Releases the device implementation after all owned objects are destroyed. */
    virtual ~IRHIDevice() = default;

    /** Returns the selected adapter information. */
    [[nodiscard]] virtual const RHIAdapterInfo& GetAdapterInfo() const noexcept = 0;

    /** Returns optional capabilities exposed by the device. */
    [[nodiscard]] virtual const RHIDeviceFeatures& GetFeatures() const noexcept = 0;

    /** Returns numeric limits exposed by the device. */
    [[nodiscard]] virtual const RHIDeviceLimits& GetLimits() const noexcept = 0;

    /** Creates a buffer allocation. */
    virtual RHIStatus CreateBuffer(const RHIBufferDesc& desc, RHIBufferHandle& outBuffer) = 0;

    /** Destroys a buffer allocation after all uses have completed. */
    virtual void DestroyBuffer(RHIBufferHandle buffer) = 0;

    /** Creates a texture allocation. */
    virtual RHIStatus CreateTexture(const RHITextureDesc& desc, RHITextureHandle& outTexture) = 0;

    /** Destroys a texture allocation after all uses have completed. */
    virtual void DestroyTexture(RHITextureHandle texture) = 0;

    /** Creates a view over a texture allocation. */
    virtual RHIStatus CreateTextureView(const RHITextureViewDesc& desc, RHITextureViewHandle& outView) = 0;

    /** Destroys a texture view after all uses have completed. */
    virtual void DestroyTextureView(RHITextureViewHandle view) = 0;

    /** Creates a sampler state object. */
    virtual RHIStatus CreateSampler(const RHISamplerDesc& desc, RHISamplerHandle& outSampler) = 0;

    /** Destroys a sampler state object after all uses have completed. */
    virtual void DestroySampler(RHISamplerHandle sampler) = 0;

    /** Maps a CPU-visible buffer range and returns a writable/readable pointer. */
    virtual RHIStatus MapBuffer(RHIBufferHandle buffer, std::uint64_t offset, std::uint64_t size, void*& outData) = 0;

    /** Flushes CPU writes to a mapped buffer range when required. */
    virtual RHIStatus FlushMappedBufferRange(RHIBufferHandle buffer, std::uint64_t offset, std::uint64_t size) = 0;

    /** Invalidates CPU caches for a mapped buffer range when required. */
    virtual RHIStatus InvalidateMappedBufferRange(RHIBufferHandle buffer, std::uint64_t offset, std::uint64_t size) = 0;

    /** Unmaps a previously mapped buffer. */
    virtual RHIStatus UnmapBuffer(RHIBufferHandle buffer) = 0;

    /** Creates a compiled shader module. */
    virtual RHIStatus CreateShaderModule(const RHIShaderModuleDesc& desc, RHIShaderModuleHandle& outShaderModule) = 0;

    /** Destroys a shader module after all dependent pipelines are destroyed. */
    virtual void DestroyShaderModule(RHIShaderModuleHandle shaderModule) = 0;

    /** Creates a bind-group layout. */
    virtual RHIStatus CreateBindLayout(const RHIBindLayoutDesc& desc, RHIBindLayoutHandle& outLayout) = 0;

    /** Destroys a bind-group layout after all dependent objects are destroyed. */
    virtual void DestroyBindLayout(RHIBindLayoutHandle layout) = 0;

    /** Creates a bind-group instance. */
    virtual RHIStatus CreateBindSet(const RHIBindSetDesc& desc, RHIBindSetHandle& outBindSet) = 0;

    /** Updates resource bindings in an existing bind group. */
    virtual RHIStatus UpdateBindSet(RHIBindSetHandle bindSet, std::span<const RHIBindSetWrite> writes) = 0;

    /** Destroys a bind-group instance after all uses have completed. */
    virtual void DestroyBindSet(RHIBindSetHandle bindSet) = 0;

    /** Creates a graphics pipeline. */
    virtual RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, RHIGraphicsPipelineHandle& outPipeline) = 0;

    /** Destroys a graphics pipeline after all uses have completed. */
    virtual void DestroyGraphicsPipeline(RHIGraphicsPipelineHandle pipeline) = 0;

    /** Creates a compute pipeline. */
    virtual RHIStatus CreateComputePipeline(const RHIComputePipelineDesc& desc, RHIComputePipelineHandle& outPipeline) = 0;

    /** Destroys a compute pipeline after all uses have completed. */
    virtual void DestroyComputePipeline(RHIComputePipelineHandle pipeline) = 0;

    /** Creates a ray-tracing pipeline when the feature is supported. */
    virtual RHIStatus CreateRayTracingPipeline(const RHIRayTracingPipelineDesc& desc, RHIRayTracingPipelineHandle& outPipeline) = 0;

    /** Destroys a ray-tracing pipeline after all uses have completed. */
    virtual void DestroyRayTracingPipeline(RHIRayTracingPipelineHandle pipeline) = 0;

    /** Creates an acceleration-structure allocation when the feature is supported. */
    virtual RHIStatus CreateAccelerationStructure(const RHIAccelerationStructureDesc& desc, RHIAccelerationStructureHandle& outStructure) = 0;

    /** Destroys an acceleration structure after all uses have completed. */
    virtual void DestroyAccelerationStructure(RHIAccelerationStructureHandle structure) = 0;

    /** Creates a command list. */
    virtual RHIStatus CreateCommandList(const RHICommandListDesc& desc, RHICommandListPtr& outCommandList) = 0;

    /** Creates a synchronization fence. */
    virtual RHIStatus CreateFence(const RHIFenceDesc& desc, RHIFenceHandle& outFence) = 0;

    /** Destroys a synchronization fence after all uses have completed. */
    virtual void DestroyFence(RHIFenceHandle fence) = 0;

    /** Waits until a fence reaches the signaled state or the timeout expires. */
    virtual RHIStatus WaitForFence(RHIFenceHandle fence, std::uint64_t timeoutNanoseconds) = 0;

    /** Resets a fence to the unsignaled state. */
    virtual RHIStatus ResetFence(RHIFenceHandle fence) = 0;

    /** Creates a synchronization semaphore. */
    virtual RHIStatus CreateSemaphore(const RHISemaphoreDesc& desc, RHISemaphoreHandle& outSemaphore) = 0;

    /** Destroys a synchronization semaphore after all uses have completed. */
    virtual void DestroySemaphore(RHISemaphoreHandle semaphore) = 0;

    /** Creates a timestamp, occlusion, or pipeline-statistics query pool. */
    virtual RHIStatus CreateQueryPool(const RHIQueryPoolDesc& desc, RHIQueryPoolHandle& outPool) = 0;

    /** Destroys a query pool after all uses have completed. */
    virtual void DestroyQueryPool(RHIQueryPoolHandle pool) = 0;

    /** Creates an application presentation surface. */
    virtual RHIStatus CreatePresentationSurface(const RHIPresentationSurfaceDesc& desc, RHISurfaceHandle& outSurface) = 0;

    /** Destroys a presentation surface after all swapchains using it are destroyed. */
    virtual void DestroyPresentationSurface(RHISurfaceHandle surface) = 0;

    /** Creates a presentation swapchain for a surface. */
    virtual RHIStatus CreateSwapchain(const RHISwapchainDesc& desc, RHISwapchainHandle& outSwapchain) = 0;

    /** Recreates swapchain images after resizing or a surface change. */
    virtual RHIStatus ResizeSwapchain(RHISwapchainHandle swapchain, RHIExtent2D extent) = 0;

    /** Destroys a swapchain after all presentation work completes. */
    virtual void DestroySwapchain(RHISwapchainHandle swapchain) = 0;

    /** Acquires the next renderable swapchain image. */
    virtual RHIStatus AcquireNextImage(RHISwapchainHandle swapchain, std::uint64_t timeoutNanoseconds, RHIAcquiredImage& outImage) = 0;

    /** Submits recorded command lists to a queue. */
    virtual RHIStatus Submit(RHIQueueType queue, const RHIQueueSubmitDesc& desc) = 0;

    /** Presents a completed swapchain image. */
    virtual RHIStatus Present(const RHIPresentDesc& desc) = 0;

    /** Blocks until all queues on this device are idle. */
    virtual RHIStatus WaitIdle() = 0;
};

/** Owns an RHI device implementation. */
using RHIDevicePtr = std::shared_ptr<IRHIDevice>;

} // namespace RHI
