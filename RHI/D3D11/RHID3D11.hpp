#pragma once

#include "../RHIDefinitions.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <memory>
#include <string>
#include <vector>

namespace rhi {

struct RHID3D11SurfaceDesc {
    HWND hwnd = nullptr;
};

struct RHID3D11Desc {
    RHIBackendDesc backend{};
    RHID3D11SurfaceDesc surface{};
    D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_UNKNOWN;
    D3D_FEATURE_LEVEL minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    bool allowWarpFallback = true;
};

struct RHID3D11NativeHandles {
    IDXGIFactory1* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* immediateContext = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    HWND hwnd = nullptr;
};

class RHID3D11 {
public:
    RHID3D11();
    ~RHID3D11();

    RHID3D11(const RHID3D11&) = delete;
    RHID3D11& operator=(const RHID3D11&) = delete;
    RHID3D11(RHID3D11&&) noexcept;
    RHID3D11& operator=(RHID3D11&&) noexcept;

    [[nodiscard]] bool initialize(const RHID3D11Desc& desc, std::string* errorMessage = nullptr);
    void shutdown() noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] const RHICapabilities& capabilities() const noexcept;
    [[nodiscard]] const RHID3D11NativeHandles& nativeHandles() const noexcept;

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
        u32* imageIndex,
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


