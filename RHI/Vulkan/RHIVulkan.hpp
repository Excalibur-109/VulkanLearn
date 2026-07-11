#pragma once

#include "../RHIDefinitions.hpp"

#include <vulkan/vulkan.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rhi {

/// Vulkan surface 由平台层创建，例如 GLFW、Win32、SDL 或 Android。
/// 渲染后端只接收已经创建好的 VkSurfaceKHR，避免把窗口系统代码混进通用后端。
struct RHIVulkanSurfaceDesc {
    VkSurfaceKHR surface = VK_NULL_HANDLE; ///< 外部传入的 Vulkan surface。
    std::function<VkSurfaceKHR(VkInstance)> createSurface; ///< 可选 surface 工厂；用于 GLFW 这类必须拿到 VkInstance 后才能创建 surface 的窗口库。
    bool ownsSurface = false; ///< true 表示 RHIVulkan::shutdown 时会销毁 surface。
};

/// Vulkan 后端初始化描述，组合通用 RHIBackendDesc 和 Vulkan 专用扩展需求。
struct RHIVulkanDesc {
    RHIBackendDesc backend{}; ///< 通用后端初始化参数。
    RHIVulkanSurfaceDesc surface{}; ///< 可选 surface；没有 surface 时仍可做离屏渲染和计算。
    std::vector<const char*> requiredInstanceExtensions; ///< 平台必须启用的 instance extensions。
    std::vector<const char*> optionalInstanceExtensions; ///< 可选 instance extensions，支持则启用。
    std::vector<const char*> requiredDeviceExtensions; ///< 必须启用的 device extensions。
    std::vector<const char*> optionalDeviceExtensions; ///< 可选 device extensions，支持则启用。
    std::vector<RHIQueueDesc> queues; ///< 期望队列；为空时默认请求 graphics/compute/transfer/present。
};

/// 暴露给上层调试或和现有 Vulkan 教程代码互操作的底层 Vulkan 句柄。
struct RHIVulkanNativeHandles {
    VkInstance instance = VK_NULL_HANDLE; ///< Vulkan instance。
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; ///< 选中的物理设备。
    VkDevice device = VK_NULL_HANDLE; ///< Vulkan logical device。
    VkSurfaceKHR surface = VK_NULL_HANDLE; ///< 当前 surface，可能为空。
    VkQueue graphicsQueue = VK_NULL_HANDLE; ///< graphics queue。
    VkQueue computeQueue = VK_NULL_HANDLE; ///< compute queue。
    VkQueue transferQueue = VK_NULL_HANDLE; ///< transfer queue。
    VkQueue presentQueue = VK_NULL_HANDLE; ///< present queue。
    u32 graphicsQueueFamily = RHI_INVALID_INDEX; ///< graphics queue family index。
    u32 computeQueueFamily = RHI_INVALID_INDEX; ///< compute queue family index。
    u32 transferQueueFamily = RHI_INVALID_INDEX; ///< transfer queue family index。
    u32 presentQueueFamily = RHI_INVALID_INDEX; ///< present queue family index。
};

/// 基于 RHIDefinitions.hpp 的 Vulkan 后端。
///
/// 这个类负责把通用渲染描述转换成 Vulkan 对象：
/// - RHIBufferDesc / RHITextureDesc / RHISamplerDesc -> VkBuffer / VkImage / VkSampler
/// - RHIBindGroupLayoutDesc / RHIBindGroupDesc -> VkDescriptorSetLayout / VkDescriptorSet
/// - RHIGraphicsPipelineDesc / RHIComputePipelineDesc -> VkPipeline
/// - RHISwapchainDesc / RHIPresentDesc -> VkSwapchainKHR / vkQueuePresentKHR
class RHIVulkan {
public:
    RHIVulkan();
    ~RHIVulkan();

    RHIVulkan(const RHIVulkan&) = delete;
    RHIVulkan& operator=(const RHIVulkan&) = delete;
    RHIVulkan(RHIVulkan&&) noexcept;
    RHIVulkan& operator=(RHIVulkan&&) noexcept;

    /// 初始化 Vulkan instance、physical device、logical device 和基础资源池。
    [[nodiscard]] bool initialize(const RHIVulkanDesc& desc, std::string* errorMessage = nullptr);

    /// 销毁所有 Vulkan 对象；调用后所有渲染句柄都失效。
    void shutdown() noexcept;

    /// 后端是否已经成功初始化。
    [[nodiscard]] bool isInitialized() const noexcept;

    /// 当前设备能力，初始化成功后有效。
    [[nodiscard]] const RHICapabilities& capabilities() const noexcept;

    /// 当前 Vulkan 原生句柄，便于调试或和教程代码过渡集成。
    [[nodiscard]] const RHIVulkanNativeHandles& nativeHandles() const noexcept;

    /// 创建 GPU buffer。
    [[nodiscard]] RHIBuffer createBuffer(const RHIBufferDesc& desc);

    /// 创建 GPU texture/image。
    [[nodiscard]] RHITexture createTexture(const RHITextureDesc& desc);

    /// 创建 texture view。
    [[nodiscard]] RHITextureView createTextureView(const RHITextureViewDesc& desc);

    /// 创建 sampler。
    [[nodiscard]] RHISampler createSampler(const RHISamplerDesc& desc);

    /// 从 SPIR-V bytecode 或 filePath 创建 shader module。
    [[nodiscard]] RHIShader createShaderModule(const RHIShaderDesc& desc);

    /// 创建 descriptor set layout。
    [[nodiscard]] RHIBindGroupLayout createBindGroupLayout(const RHIBindGroupLayoutDesc& desc);

    /// 创建并更新 descriptor set。
    [[nodiscard]] RHIBindGroup createBindGroup(const RHIBindGroupDesc& desc);

    /// 创建 pipeline layout。
    [[nodiscard]] RHIPipelineLayout createPipelineLayout(const RHIPipelineLayoutDesc& desc);

    /// 创建 pipeline cache。
    [[nodiscard]] RHIPipelineCache createPipelineCache(const RHIPipelineCacheDesc& desc);

    /// 创建 graphics pipeline；当前实现使用 Vulkan dynamic rendering。
    [[nodiscard]] RHIPipeline createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc);

    /// 创建 compute pipeline。
    [[nodiscard]] RHIPipeline createComputePipeline(const RHIComputePipelineDesc& desc);

    /// 创建 GPU query pool。
    [[nodiscard]] RHIQueryPool createQueryPool(const RHIQueryPoolDesc& desc);

    /// 创建 semaphore。
    [[nodiscard]] RHISemaphore createSemaphore(const RHISemaphoreDesc& desc);

    /// 创建 fence。
    [[nodiscard]] RHIFence createFence(const RHIFenceDesc& desc);

    /// 创建 swapchain；需要 initialize 时传入有效 surface。
    [[nodiscard]] RHISwapchain createSwapchain(const RHISwapchainDesc& desc);

    /// 获取 swapchain image 对应的引擎 texture handles。
    [[nodiscard]] std::vector<RHITexture> getSwapchainImages(RHISwapchain handle) const;

    /// 获取 swapchain image view handles。
    [[nodiscard]] std::vector<RHITextureView> getSwapchainImageViews(RHISwapchain handle) const;

    /// 获取 swapchain 实际选择的后备缓冲格式；无效句柄返回 RHIFormat::Undefined。
    [[nodiscard]] RHIFormat getSwapchainFormat(RHISwapchain handle) const;

    /// 获取 swapchain 实际 extent；无效句柄返回 {0, 0}。
    [[nodiscard]] RHIExtent2D getSwapchainExtent(RHISwapchain handle) const;

    /// 获取下一张 swapchain image。
    [[nodiscard]] bool acquireNextImage(
        RHISwapchain swapchain,
        RHISemaphore signalSemaphore,
        RHIFence signalFence,
        u32* imageIndex,
        std::string* errorMessage = nullptr);

    /// 按 RHIQueueSubmitDesc 提交队列；当前提交已录好的内部/外部命令缓冲为空时可用于同步测试。
    [[nodiscard]] bool submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage = nullptr);

    /// 执行 present。
    [[nodiscard]] bool present(const RHIPresentDesc& desc, std::string* errorMessage = nullptr);

    /// 提交一帧的同步和 present 描述。RenderGraph 自动录制会在后续扩展。
    [[nodiscard]] bool submitFrame(const RHIFramePacket& packet, std::string* errorMessage = nullptr);

    /// 等待 device idle，通常只在 resize、退出或资源大清理时使用。
    void waitIdle() const noexcept;

    /// 按句柄销毁对应 Vulkan 资源；无效句柄或已经销毁的资源会被忽略。
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
    /// 根据 RHIFramePacket 录制并提交一帧命令；当前覆盖示例所需的上传、动态渲染和 indexed draw。
    [[nodiscard]] bool recordAndSubmitFrame(const RHIFramePacket& packet, std::string* errorMessage);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rhi


