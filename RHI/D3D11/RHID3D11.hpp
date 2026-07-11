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

    [[nodiscard]] bool Initialize(const RHID3D11Desc& desc, std::string* errorMessage = nullptr);
    void Shutdown() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const RHICapabilities& Capabilities() const noexcept;
    [[nodiscard]] const RHID3D11NativeHandles& NativeHandles() const noexcept;

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
    [[nodiscard]] bool RecordAndSubmitFrame(const RHIFramePacket& packet, std::string* errorMessage);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rhi










