#pragma once

#include "RHIDeviceDesc.hpp"

#include <string>
#include <vector>

namespace rhi {

/// 所有图形 API 后端必须实现的统一 RHI 设备接口。
///
/// RHIDevice 只负责 GPU 资源、管线、交换链和命令提交。Scene、相机、灯光、
/// RenderCollector、RenderQueue 与 RenderGraph 都属于更高层。
class RHIDevice {
public:
    virtual ~RHIDevice() = default;

    RHIDevice(const RHIDevice&) = delete;
    RHIDevice& operator=(const RHIDevice&) = delete;

    [[nodiscard]] virtual RHIGraphicsAPI api() const noexcept = 0;
    [[nodiscard]] virtual const char* backendName() const noexcept = 0;
    [[nodiscard]] virtual bool initialize(const RHIDeviceCreateDesc& desc, std::string* errorMessage = nullptr) = 0;
    virtual void shutdown() noexcept = 0;
    [[nodiscard]] virtual bool isInitialized() const noexcept = 0;
    [[nodiscard]] virtual const RHICapabilities& capabilities() const noexcept = 0;

    [[nodiscard]] virtual RHIBuffer createBuffer(const RHIBufferDesc& desc) = 0;
    [[nodiscard]] virtual RHITexture createTexture(const RHITextureDesc& desc) = 0;
    [[nodiscard]] virtual RHITextureView createTextureView(const RHITextureViewDesc& desc) = 0;
    [[nodiscard]] virtual RHISampler createSampler(const RHISamplerDesc& desc) = 0;
    [[nodiscard]] virtual RHIShader createShaderModule(const RHIShaderDesc& desc) = 0;
    [[nodiscard]] virtual RHIBindGroupLayout createBindGroupLayout(const RHIBindGroupLayoutDesc& desc) = 0;
    [[nodiscard]] virtual RHIBindGroup createBindGroup(const RHIBindGroupDesc& desc) = 0;

    [[nodiscard]] virtual RHIPipelineLayout createPipelineLayout(const RHIPipelineLayoutDesc& desc) = 0;
    [[nodiscard]] virtual RHIPipelineCache createPipelineCache(const RHIPipelineCacheDesc& desc) = 0;
    [[nodiscard]] virtual RHIPipeline createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) = 0;
    [[nodiscard]] virtual RHIPipeline createComputePipeline(const RHIComputePipelineDesc& desc) = 0;

    [[nodiscard]] virtual RHIQueryPool createQueryPool(const RHIQueryPoolDesc& desc) = 0;
    [[nodiscard]] virtual RHISemaphore createSemaphore(const RHISemaphoreDesc& desc) = 0;
    [[nodiscard]] virtual RHIFence createFence(const RHIFenceDesc& desc) = 0;

    [[nodiscard]] virtual RHISwapchain createSwapchain(const RHISwapchainDesc& desc) = 0;
    [[nodiscard]] virtual std::vector<RHITexture> getSwapchainImages(RHISwapchain handle) const = 0;
    [[nodiscard]] virtual std::vector<RHITextureView> getSwapchainImageViews(RHISwapchain handle) const = 0;
    [[nodiscard]] virtual RHIFormat getSwapchainFormat(RHISwapchain handle) const = 0;
    [[nodiscard]] virtual RHIExtent2D getSwapchainExtent(RHISwapchain handle) const = 0;
    [[nodiscard]] virtual bool acquireNextImage(
        RHISwapchain swapchain,
        RHISemaphore signalSemaphore,
        RHIFence signalFence,
        RHIUInt32* imageIndex,
        std::string* errorMessage = nullptr) = 0;

    [[nodiscard]] virtual bool submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage = nullptr) = 0;
    [[nodiscard]] virtual bool present(const RHIPresentDesc& desc, std::string* errorMessage = nullptr) = 0;
    [[nodiscard]] virtual bool submitFrame(const RHIFramePacket& packet, std::string* errorMessage = nullptr) = 0;
    virtual void waitIdle() const noexcept = 0;

    virtual void destroy(RHIBuffer handle) noexcept = 0;
    virtual void destroy(RHITexture handle) noexcept = 0;
    virtual void destroy(RHITextureView handle) noexcept = 0;
    virtual void destroy(RHISampler handle) noexcept = 0;
    virtual void destroy(RHIShader handle) noexcept = 0;
    virtual void destroy(RHIBindGroupLayout handle) noexcept = 0;
    virtual void destroy(RHIBindGroup handle) noexcept = 0;
    virtual void destroy(RHIPipelineLayout handle) noexcept = 0;
    virtual void destroy(RHIPipelineCache handle) noexcept = 0;
    virtual void destroy(RHIPipeline handle) noexcept = 0;
    virtual void destroy(RHIQueryPool handle) noexcept = 0;
    virtual void destroy(RHISemaphore handle) noexcept = 0;
    virtual void destroy(RHIFence handle) noexcept = 0;
    virtual void destroy(RHISwapchain handle) noexcept = 0;

protected:
    RHIDevice() = default;
};

} // namespace rhi