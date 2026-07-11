#pragma once

#include "../RHIDefinitions.hpp"

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

struct RHID3D12BackendDesc {
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

class RHID3D12Backend {
public:
    RHID3D12Backend();
    ~RHID3D12Backend();

    RHID3D12Backend(const RHID3D12Backend&) = delete;
    RHID3D12Backend& operator=(const RHID3D12Backend&) = delete;
    RHID3D12Backend(RHID3D12Backend&&) noexcept;
    RHID3D12Backend& operator=(RHID3D12Backend&&) noexcept;

    [[nodiscard]] bool initialize(const RHID3D12BackendDesc& desc, std::string* errorMessage = nullptr);
    void shutdown() noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] const RHICapabilities& capabilities() const noexcept;
    [[nodiscard]] const RHID3D12NativeHandles& nativeHandles() const noexcept;

    [[nodiscard]] RHIBuffer createBuffer(const RHIBufferDesc& desc);
    [[nodiscard]] RHITexture createTexture(const RHITextureDesc& desc);
    [[nodiscard]] RHITextureView createTextureView(const RHITextureViewDesc& desc);
    [[nodiscard]] RHISampler createSampler(const RHISamplerDesc& desc);
    [[nodiscard]] RHIShader createShaderModule(const RHIShaderDesc& desc);
    [[nodiscard]] RHIBindGroupLayout createBindGroupLayout(const RHIBindGroupLayoutDesc& desc);
    [[nodiscard]] RHIBindGroup createBindGroup(const RHIBindGroupDesc& desc);
    [[nodiscard]] RHIPipelineLayout createPipelineLayout(const RHIPipelineLayoutDesc& desc);
    [[nodiscard]] RHIPipelineCache createPipelineCache(const RHIPipelineCacheDesc& desc);
    [[nodiscard]] RHIPipeline createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc);
    [[nodiscard]] RHIPipeline createComputePipeline(const RHIComputePipelineDesc& desc);
    [[nodiscard]] RHIQueryPool createQueryPool(const RHIQueryPoolDesc& desc);
    [[nodiscard]] RHISemaphore createSemaphore(const RHISemaphoreDesc& desc);
    [[nodiscard]] RHIFence createFence(const RHIFenceDesc& desc);
    [[nodiscard]] RHISwapchain createSwapchain(const RHISwapchainDesc& desc);

    [[nodiscard]] std::vector<RHITexture> getSwapchainImages(RHISwapchain handle) const;
    [[nodiscard]] std::vector<RHITextureView> getSwapchainImageViews(RHISwapchain handle) const;
    [[nodiscard]] RHIFormat getSwapchainFormat(RHISwapchain handle) const;
    [[nodiscard]] RHIExtent2D getSwapchainExtent(RHISwapchain handle) const;

    [[nodiscard]] bool acquireNextImage(
        RHISwapchain swapchain,
        RHISemaphore signalSemaphore,
        RHIFence signalFence,
        RHIUInt32* imageIndex,
        std::string* errorMessage = nullptr);

    [[nodiscard]] bool submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage = nullptr);
    [[nodiscard]] bool present(const RHIPresentDesc& desc, std::string* errorMessage = nullptr);
    [[nodiscard]] bool submitFrame(const RHIFramePacket& packet, std::string* errorMessage = nullptr);

    void waitIdle() const noexcept;

    void destroy(RHIBuffer handle) noexcept;
    void destroy(RHITexture handle) noexcept;
    void destroy(RHITextureView handle) noexcept;
    void destroy(RHISampler handle) noexcept;
    void destroy(RHIShader handle) noexcept;
    void destroy(RHIBindGroupLayout handle) noexcept;
    void destroy(RHIBindGroup handle) noexcept;
    void destroy(RHIPipelineLayout handle) noexcept;
    void destroy(RHIPipelineCache handle) noexcept;
    void destroy(RHIPipeline handle) noexcept;
    void destroy(RHIQueryPool handle) noexcept;
    void destroy(RHISemaphore handle) noexcept;
    void destroy(RHIFence handle) noexcept;
    void destroy(RHISwapchain handle) noexcept;

private:
    [[nodiscard]] bool recordAndSubmitFrame(const RHIFramePacket& packet, std::string* errorMessage);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rhi
