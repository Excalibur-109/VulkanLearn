#pragma once

#include "../RHIDefinitions.hpp"

#include "../RenderGraph/RHIRenderGraph.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <memory>
#include <string>
#include <vector>

namespace rhi {

struct RHID3D12SurfaceDesc {
    HWND hwnd = nullptr;
};

struct RHID3D12Desc {
    RHIBackendDesc backend{};
    RHID3D12SurfaceDesc surface{};
    D3D_FEATURE_LEVEL minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    bool allowWarpFallback = true;
};

struct RHID3D12NativeHandles {
    IDXGIFactory6* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* graphicsQueue = nullptr;
    ID3D12CommandAllocator* commandAllocator = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 fenceValue = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    HWND hwnd = nullptr;
};

class RHID3D12 {
public:
    RHID3D12();
    ~RHID3D12();

    RHID3D12(const RHID3D12&) = delete;
    RHID3D12& operator=(const RHID3D12&) = delete;
    RHID3D12(RHID3D12&&) noexcept;
    RHID3D12& operator=(RHID3D12&&) noexcept;

    [[nodiscard]] bool Initialize(const RHID3D12Desc& desc, std::string* errorMessage = nullptr);
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const RHICapabilities& Capabilities() const noexcept;
    [[nodiscard]] const RHID3D12NativeHandles& NativeHandles() const noexcept;

    [[nodiscard]] RHIBuffer CreateBuffer(const RHIBufferDesc& desc);
    [[nodiscard]] RHITexture CreateTexture(const RHITextureDesc& desc);
    [[nodiscard]] RHITextureView CreateTextureView(const RHITextureViewDesc& desc);
    [[nodiscard]] RHISampler CreateSampler(const RHISamplerDesc& desc);
    [[nodiscard]] RHIShader CreateShaderModule(const RHIShaderDesc& desc);
    [[nodiscard]] RHIBindSetLayout CreateBindSetLayout(const RHIBindSetLayoutDesc& desc);
    [[nodiscard]] RHIBindSet CreateBindSet(const RHIBindSetDesc& desc);
    [[nodiscard]] RHIPipelineLayout CreatePipelineLayout(const RHIPipelineLayoutDesc& desc);
    [[nodiscard]] RHIPipelineCache CreatePipelineCache(const RHIPipelineCacheDesc& desc);
    [[nodiscard]] RHIPipeline CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc);
    [[nodiscard]] RHIPipeline CreateComputePipeline(const RHIComputePipelineDesc& desc);
    [[nodiscard]] RHIQueryPool CreateQueryPool(const RHIQueryPoolDesc& desc);
    [[nodiscard]] RHIGPUWaitGPUSignal CreateGPUWaitGPUSignal(const RHIGPUWaitGPUSignalDesc& desc);
    [[nodiscard]] RHICPUWaitGPUSignal CreateCPUWaitGPUSignal(const RHICPUWaitGPUSignalDesc& desc);
    [[nodiscard]] RHISwapchain CreateSwapchain(const RHISwapchainDesc& desc);

    [[nodiscard]] std::vector<RHITexture> GetSwapchainImages(RHISwapchain handle) const;
    [[nodiscard]] std::vector<RHITextureView> GetSwapchainImageViews(RHISwapchain handle) const;
    [[nodiscard]] RHIFormat GetSwapchainFormat(RHISwapchain handle) const;
    [[nodiscard]] RHIExtent2D GetSwapchainExtent(RHISwapchain handle) const;

    [[nodiscard]] bool AcquireNextImage(
        RHISwapchain swapchain,
        RHIGPUWaitGPUSignal gpuWaitGPUSignal,
        RHICPUWaitGPUSignal cpuWaitGPUSignal,
        u32* imageIndex,
        std::string* errorMessage = nullptr);

    [[nodiscard]] bool Submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage = nullptr);
    [[nodiscard]] bool Present(const RHIPresentDesc& desc, std::string* errorMessage = nullptr);
    [[nodiscard]] bool SubmitFrame(const RHIFramePacket& packet, std::string* errorMessage = nullptr);
    [[nodiscard]] bool SubmitFrame(
        const RHIFramePacket& packet,
        const RHIRenderGraphExecutionPlan& graphPlan,
        std::string* errorMessage = nullptr);

    void WaitIdle() const noexcept;

    void Destroy(RHIBuffer handle) noexcept;
    void Destroy(RHITexture handle) noexcept;
    void Destroy(RHITextureView handle) noexcept;
    void Destroy(RHISampler handle) noexcept;
    void Destroy(RHIShader handle) noexcept;
    void Destroy(RHIBindSetLayout handle) noexcept;
    void Destroy(RHIBindSet handle) noexcept;
    void Destroy(RHIPipelineLayout handle) noexcept;
    void Destroy(RHIPipelineCache handle) noexcept;
    void Destroy(RHIPipeline handle) noexcept;
    void Destroy(RHIQueryPool handle) noexcept;
    void Destroy(RHIGPUWaitGPUSignal handle) noexcept;
    void Destroy(RHICPUWaitGPUSignal handle) noexcept;
    void Destroy(RHISwapchain handle) noexcept;

private:
    [[nodiscard]] bool RecordAndSubmitFrame(
        const RHIFramePacket& packet,
        const RHIRenderGraphExecutionPlan& graphPlan,
        std::string* errorMessage);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rhi










