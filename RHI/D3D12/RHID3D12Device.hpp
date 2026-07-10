#pragma once

#include "../Core/RHIDevice.hpp"

#include <memory>

namespace rhi {

/// Direct3D 12 后端的统一 RHI 设备。
/// 公开头文件不包含原生 API 类型，具体驱动对象隐藏在 Impl 中。
class RHID3D12Device final : public RHIDevice {
public:
    RHID3D12Device();
    ~RHID3D12Device() override;

    RHID3D12Device(const RHID3D12Device&) = delete;
    RHID3D12Device& operator=(const RHID3D12Device&) = delete;

    [[nodiscard]] RHIGraphicsAPI api() const noexcept override;
    [[nodiscard]] const char* backendName() const noexcept override;
    [[nodiscard]] bool initialize(const RHIDeviceCreateDesc& desc, std::string* errorMessage = nullptr) override;
    void shutdown() noexcept override;
    [[nodiscard]] bool isInitialized() const noexcept override;
    [[nodiscard]] const RHICapabilities& capabilities() const noexcept override;

    [[nodiscard]] RHIBuffer createBuffer(const RHIBufferDesc& desc) override;
    [[nodiscard]] RHITexture createTexture(const RHITextureDesc& desc) override;
    [[nodiscard]] RHITextureView createTextureView(const RHITextureViewDesc& desc) override;
    [[nodiscard]] RHISampler createSampler(const RHISamplerDesc& desc) override;
    [[nodiscard]] RHIShader createShaderModule(const RHIShaderDesc& desc) override;
    [[nodiscard]] RHIBindGroupLayout createBindGroupLayout(const RHIBindGroupLayoutDesc& desc) override;
    [[nodiscard]] RHIBindGroup createBindGroup(const RHIBindGroupDesc& desc) override;

    [[nodiscard]] RHIPipelineLayout createPipelineLayout(const RHIPipelineLayoutDesc& desc) override;
    [[nodiscard]] RHIPipelineCache createPipelineCache(const RHIPipelineCacheDesc& desc) override;
    [[nodiscard]] RHIPipeline createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;
    [[nodiscard]] RHIPipeline createComputePipeline(const RHIComputePipelineDesc& desc) override;

    [[nodiscard]] RHIQueryPool createQueryPool(const RHIQueryPoolDesc& desc) override;
    [[nodiscard]] RHISemaphore createSemaphore(const RHISemaphoreDesc& desc) override;
    [[nodiscard]] RHIFence createFence(const RHIFenceDesc& desc) override;
    [[nodiscard]] RHISwapchain createSwapchain(const RHISwapchainDesc& desc) override;
    [[nodiscard]] std::vector<RHITexture> getSwapchainImages(RHISwapchain handle) const override;
    [[nodiscard]] std::vector<RHITextureView> getSwapchainImageViews(RHISwapchain handle) const override;
    [[nodiscard]] RHIFormat getSwapchainFormat(RHISwapchain handle) const override;
    [[nodiscard]] RHIExtent2D getSwapchainExtent(RHISwapchain handle) const override;
    [[nodiscard]] bool acquireNextImage(RHISwapchain swapchain, RHISemaphore signalSemaphore, RHIFence signalFence, RHIUInt32* imageIndex, std::string* errorMessage = nullptr) override;

    [[nodiscard]] bool submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage = nullptr) override;
    [[nodiscard]] bool present(const RHIPresentDesc& desc, std::string* errorMessage = nullptr) override;
    [[nodiscard]] bool submitFrame(const RHIFramePacket& packet, std::string* errorMessage = nullptr) override;
    void waitIdle() const noexcept override;

    void destroy(RHIBuffer handle) noexcept override;
    void destroy(RHITexture handle) noexcept override;
    void destroy(RHITextureView handle) noexcept override;
    void destroy(RHISampler handle) noexcept override;
    void destroy(RHIShader handle) noexcept override;
    void destroy(RHIBindGroupLayout handle) noexcept override;
    void destroy(RHIBindGroup handle) noexcept override;
    void destroy(RHIPipelineLayout handle) noexcept override;
    void destroy(RHIPipelineCache handle) noexcept override;
    void destroy(RHIPipeline handle) noexcept override;
    void destroy(RHIQueryPool handle) noexcept override;
    void destroy(RHISemaphore handle) noexcept override;
    void destroy(RHIFence handle) noexcept override;
    void destroy(RHISwapchain handle) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rhi