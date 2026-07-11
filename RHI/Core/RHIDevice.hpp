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

    [[nodiscard]] RHIGraphicsAPI api() const noexcept;
    [[nodiscard]] const char* backendName() const noexcept;
    [[nodiscard]] bool initialize(const RHIDeviceCreateDesc& desc, std::string* errorMessage = nullptr);
    void shutdown() noexcept;
    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] const RHICapabilities& capabilities() const noexcept;

    [[nodiscard]] RHIBuffer createBuffer(const RHIBufferDesc& desc);
    [[nodiscard]] RHITexture createTexture(const RHITextureDesc& desc);
    [[nodiscard]] RHITextureView createTextureView(const RHITextureViewDesc& desc);
    [[nodiscard]] RHISampler createSampler(const RHISamplerDesc& desc);
    [[nodiscard]] RHIShader createShaderModule(const RHIShaderDesc& desc);
    [[nodiscard]] RHIBindSetLayout createBindSetLayout(const RHIBindSetLayoutDesc& desc);
    [[nodiscard]] RHIBindSet createBindSet(const RHIBindSetDesc& desc);

    [[nodiscard]] RHIPipelineLayout createPipelineLayout(const RHIPipelineLayoutDesc& desc);
    [[nodiscard]] RHIPipelineCache createPipelineCache(const RHIPipelineCacheDesc& desc);
    [[nodiscard]] RHIPipeline createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc);
    [[nodiscard]] RHIPipeline createComputePipeline(const RHIComputePipelineDesc& desc);

    [[nodiscard]] RHIQueryPool createQueryPool(const RHIQueryPoolDesc& desc);
    [[nodiscard]] RHIGPUWaitGPUSignal createGPUWaitGPUSignal(const RHIGPUWaitGPUSignalDesc& desc);
    [[nodiscard]] RHICPUWaitGPUSignal createCPUWaitGPUSignal(const RHICPUWaitGPUSignalDesc& desc);

    [[nodiscard]] RHISwapchain createSwapchain(const RHISwapchainDesc& desc);
    [[nodiscard]] std::vector<RHITexture> getSwapchainImages(RHISwapchain handle) const;
    [[nodiscard]] std::vector<RHITextureView> getSwapchainImageViews(RHISwapchain handle) const;
    [[nodiscard]] RHIFormat getSwapchainFormat(RHISwapchain handle) const;
    [[nodiscard]] RHIExtent2D getSwapchainExtent(RHISwapchain handle) const;
    [[nodiscard]] bool acquireNextImage(
        RHISwapchain swapchain,
        RHIGPUWaitGPUSignal gpuWaitGPUSignal,
        RHICPUWaitGPUSignal cpuWaitGPUSignal,
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
    void destroy(RHIBindSetLayout handle) noexcept;
    void destroy(RHIBindSet handle) noexcept;
    void destroy(RHIPipelineLayout handle) noexcept;
    void destroy(RHIPipelineCache handle) noexcept;
    void destroy(RHIPipeline handle) noexcept;
    void destroy(RHIQueryPool handle) noexcept;
    void destroy(RHIGPUWaitGPUSignal handle) noexcept;
    void destroy(RHICPUWaitGPUSignal handle) noexcept;
    void destroy(RHISwapchain handle) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rhi



