#pragma once

#include "RHIDeviceDesc.hpp"

#include <memory>
#include <string>
#include <vector>

namespace rhi {

/// 唯一的跨 API RHI 设备门面。
///
/// Vulkan、D3D11、D3D12 后端都是普通类，不继承 RHIDevice。RHIDevice 在实现文件中
/// 持有当前选中的后端，并把公共方法直接分发给它，从而消除三套重复的 Device 包装类。
class RHIDevice {
public:
    explicit RHIDevice(RHIGraphicsAPI requestedApi = RHIGraphicsAPI::Unknown);
    ~RHIDevice();

    RHIDevice(const RHIDevice&) = delete;
    RHIDevice& operator=(const RHIDevice&) = delete;
    RHIDevice(RHIDevice&&) noexcept;
    RHIDevice& operator=(RHIDevice&&) noexcept;

    [[nodiscard]] RHIGraphicsAPI Api() const noexcept;
    [[nodiscard]] const char* BackendName() const noexcept;
    [[nodiscard]] bool Initialize(const RHIDeviceCreateDesc& desc, std::string* errorMessage = nullptr);
    void Shutdown() noexcept;
    [[nodiscard]] bool IsInitialized() const noexcept;
    [[nodiscard]] const RHICapabilities& Capabilities() const noexcept;

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
    void WaitForCPUSignal(RHICPUWaitGPUSignal handle) const noexcept;

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
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rhi









