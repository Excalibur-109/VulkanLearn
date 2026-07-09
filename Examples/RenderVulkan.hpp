#pragma once

#include "RenderDefinitions.hpp"

#include <vulkan/vulkan.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

/// Vulkan surface 由平台层创建，例如 GLFW、Win32、SDL 或 Android。
/// 渲染后端只接收已经创建好的 VkSurfaceKHR，避免把窗口系统代码混进通用后端。
struct VulkanSurfaceDesc {
    VkSurfaceKHR surface = VK_NULL_HANDLE; ///< 外部传入的 Vulkan surface。
    std::function<VkSurfaceKHR(VkInstance)> createSurface; ///< 可选 surface 工厂；用于 GLFW 这类必须拿到 VkInstance 后才能创建 surface 的窗口库。
    bool ownsSurface = false; ///< true 表示 VulkanRenderer::shutdown 时会销毁 surface。
};

/// Vulkan 后端初始化描述，组合通用 RenderBackendDesc 和 Vulkan 专用扩展需求。
struct VulkanRendererDesc {
    RenderBackendDesc backend{}; ///< 通用后端初始化参数。
    VulkanSurfaceDesc surface{}; ///< 可选 surface；没有 surface 时仍可做离屏渲染和计算。
    std::vector<const char*> requiredInstanceExtensions; ///< 平台必须启用的 instance extensions。
    std::vector<const char*> optionalInstanceExtensions; ///< 可选 instance extensions，支持则启用。
    std::vector<const char*> requiredDeviceExtensions; ///< 必须启用的 device extensions。
    std::vector<const char*> optionalDeviceExtensions; ///< 可选 device extensions，支持则启用。
    std::vector<QueueDesc> queues; ///< 期望队列；为空时默认请求 graphics/compute/transfer/present。
};

/// 暴露给上层调试或和现有 Vulkan 教程代码互操作的底层 Vulkan 句柄。
struct VulkanNativeHandles {
    VkInstance instance = VK_NULL_HANDLE; ///< Vulkan instance。
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE; ///< 选中的物理设备。
    VkDevice device = VK_NULL_HANDLE; ///< Vulkan logical device。
    VkSurfaceKHR surface = VK_NULL_HANDLE; ///< 当前 surface，可能为空。
    VkQueue graphicsQueue = VK_NULL_HANDLE; ///< graphics queue。
    VkQueue computeQueue = VK_NULL_HANDLE; ///< compute queue。
    VkQueue transferQueue = VK_NULL_HANDLE; ///< transfer queue。
    VkQueue presentQueue = VK_NULL_HANDLE; ///< present queue。
    u32 graphicsQueueFamily = INVALID_INDEX; ///< graphics queue family index。
    u32 computeQueueFamily = INVALID_INDEX; ///< compute queue family index。
    u32 transferQueueFamily = INVALID_INDEX; ///< transfer queue family index。
    u32 presentQueueFamily = INVALID_INDEX; ///< present queue family index。
};

/// 基于 RenderDefinitions.hpp 的 Vulkan 后端。
///
/// 这个类负责把通用渲染描述转换成 Vulkan 对象：
/// - BufferDesc / TextureDesc / SamplerDesc -> VkBuffer / VkImage / VkSampler
/// - BindGroupLayoutDesc / BindGroupDesc -> VkDescriptorSetLayout / VkDescriptorSet
/// - GraphicsPipelineDesc / ComputePipelineDesc -> VkPipeline
/// - SwapchainDesc / PresentDesc -> VkSwapchainKHR / vkQueuePresentKHR
class VulkanRenderer {
public:
    VulkanRenderer();
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;
    VulkanRenderer(VulkanRenderer&&) noexcept;
    VulkanRenderer& operator=(VulkanRenderer&&) noexcept;

    /// 初始化 Vulkan instance、physical device、logical device 和基础资源池。
    [[nodiscard]] bool initialize(const VulkanRendererDesc& desc, std::string* errorMessage = nullptr);

    /// 销毁所有 Vulkan 对象；调用后所有渲染句柄都失效。
    void shutdown() noexcept;

    /// 后端是否已经成功初始化。
    [[nodiscard]] bool isInitialized() const noexcept;

    /// 当前设备能力，初始化成功后有效。
    [[nodiscard]] const RenderCapabilities& capabilities() const noexcept;

    /// 当前 Vulkan 原生句柄，便于调试或和教程代码过渡集成。
    [[nodiscard]] const VulkanNativeHandles& nativeHandles() const noexcept;

    /// 创建 GPU buffer。
    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc);

    /// 创建 GPU texture/image。
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc);

    /// 创建 texture view。
    [[nodiscard]] TextureViewHandle createTextureView(const TextureViewDesc& desc);

    /// 创建 sampler。
    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& desc);

    /// 从 SPIR-V bytecode 或 filePath 创建 shader module。
    [[nodiscard]] ShaderHandle createShaderModule(const ShaderDesc& desc);

    /// 创建 descriptor set layout。
    [[nodiscard]] BindGroupLayoutHandle createBindGroupLayout(const BindGroupLayoutDesc& desc);

    /// 创建并更新 descriptor set。
    [[nodiscard]] BindGroupHandle createBindGroup(const BindGroupDesc& desc);

    /// 创建 pipeline layout。
    [[nodiscard]] PipelineLayoutHandle createPipelineLayout(const PipelineLayoutDesc& desc);

    /// 创建 pipeline cache。
    [[nodiscard]] PipelineCacheHandle createPipelineCache(const PipelineCacheDesc& desc);

    /// 创建 graphics pipeline；当前实现使用 Vulkan dynamic rendering。
    [[nodiscard]] PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc);

    /// 创建 compute pipeline。
    [[nodiscard]] PipelineHandle createComputePipeline(const ComputePipelineDesc& desc);

    /// 创建 GPU query pool。
    [[nodiscard]] QueryPoolHandle createQueryPool(const QueryPoolDesc& desc);

    /// 创建 semaphore。
    [[nodiscard]] SemaphoreHandle createSemaphore(const SemaphoreDesc& desc);

    /// 创建 fence。
    [[nodiscard]] FenceHandle createFence(const FenceDesc& desc);

    /// 创建 swapchain；需要 initialize 时传入有效 surface。
    [[nodiscard]] SwapchainHandle createSwapchain(const SwapchainDesc& desc);

    /// 获取 swapchain image 对应的引擎 texture handles。
    [[nodiscard]] std::vector<TextureHandle> getSwapchainImages(SwapchainHandle handle) const;

    /// 获取 swapchain image view handles。
    [[nodiscard]] std::vector<TextureViewHandle> getSwapchainImageViews(SwapchainHandle handle) const;

    /// 获取 swapchain 实际选择的后备缓冲格式；无效句柄返回 Format::Undefined。
    [[nodiscard]] Format getSwapchainFormat(SwapchainHandle handle) const;

    /// 获取 swapchain 实际 extent；无效句柄返回 {0, 0}。
    [[nodiscard]] Extent2D getSwapchainExtent(SwapchainHandle handle) const;

    /// 获取下一张 swapchain image。
    [[nodiscard]] bool acquireNextImage(
        SwapchainHandle swapchain,
        SemaphoreHandle signalSemaphore,
        FenceHandle signalFence,
        u32* imageIndex,
        std::string* errorMessage = nullptr);

    /// 按 QueueSubmitDesc 提交队列；当前提交已录好的内部/外部命令缓冲为空时可用于同步测试。
    [[nodiscard]] bool submit(const QueueSubmitDesc& desc, std::string* errorMessage = nullptr);

    /// 执行 present。
    [[nodiscard]] bool present(const PresentDesc& desc, std::string* errorMessage = nullptr);

    /// 提交一帧的同步和 present 描述。RenderGraph 自动录制会在后续扩展。
    [[nodiscard]] bool submitFrame(const FramePacket& packet, std::string* errorMessage = nullptr);

    /// 等待 device idle，通常只在 resize、退出或资源大清理时使用。
    void waitIdle() const noexcept;

    /// 按句柄销毁对应 Vulkan 资源；无效句柄或已经销毁的资源会被忽略。
    void destroy(BufferHandle handle) noexcept;
    void destroy(TextureHandle handle) noexcept;
    void destroy(TextureViewHandle handle) noexcept;
    void destroy(SamplerHandle handle) noexcept;
    void destroy(ShaderHandle handle) noexcept;
    void destroy(BindGroupLayoutHandle handle) noexcept;
    void destroy(BindGroupHandle handle) noexcept;
    void destroy(PipelineLayoutHandle handle) noexcept;
    void destroy(PipelineCacheHandle handle) noexcept;
    void destroy(PipelineHandle handle) noexcept;
    void destroy(QueryPoolHandle handle) noexcept;
    void destroy(SemaphoreHandle handle) noexcept;
    void destroy(FenceHandle handle) noexcept;
    void destroy(SwapchainHandle handle) noexcept;

private:
    /// 根据 FramePacket 录制并提交一帧命令；当前覆盖示例所需的上传、动态渲染和 indexed draw。
    [[nodiscard]] bool recordAndSubmitFrame(const FramePacket& packet, std::string* errorMessage);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
