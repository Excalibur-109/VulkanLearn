#include "RHI/Backends/RHINativeBackends.hpp"
#include "../Common/RHINativeDeviceBase.hpp"

#include <vulkan/vulkan.h>

#include <chrono>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace RHI
{
namespace
{

/** 将 RHI 格式转换为 Vulkan 格式。 */
VkFormat ToVkFormat(const RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::RGBA8_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::RGBA8_UNorm_sRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case RHIFormat::BGRA8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case RHIFormat::BGRA8_UNorm_sRGB: return VK_FORMAT_B8G8R8A8_SRGB;
    case RHIFormat::RGBA16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

/** 将 RHI 顶点格式转换为 Vulkan 格式。 */
VkFormat ToVkVertexFormat(const RHIVertexFormat format)
{
    switch (format)
    {
    case RHIVertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
    case RHIVertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
    case RHIVertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    default: return VK_FORMAT_UNDEFINED;
    }
}

/** 按内存属性掩码选择满足全部请求标志的物理内存类型。 */
std::uint32_t FindMemoryType(const VkPhysicalDeviceMemoryProperties& properties, const std::uint32_t mask, const VkMemoryPropertyFlags flags)
{
    for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
        if ((mask & (1u << index)) != 0u && (properties.memoryTypes[index].propertyFlags & flags) == flags) return index;
    return std::numeric_limits<std::uint32_t>::max();
}

struct VulkanBuffer final { VkBuffer Buffer = VK_NULL_HANDLE; VkDeviceMemory Memory = VK_NULL_HANDLE; RHIBufferDesc Desc; };
struct VulkanTexture final { VkImage Image = VK_NULL_HANDLE; VkDeviceMemory Memory = VK_NULL_HANDLE; RHITextureDesc Desc; VkImageLayout Layout = VK_IMAGE_LAYOUT_UNDEFINED; };
struct VulkanView final { VkImageView View = VK_NULL_HANDLE; RHITextureHandle Texture{}; };
struct VulkanShader final { VkShaderModule Module = VK_NULL_HANDLE; RHIShaderModuleDesc Desc; };
struct VulkanPipeline final { VkPipelineLayout Layout = VK_NULL_HANDLE; VkPipeline Pipeline = VK_NULL_HANDLE; };

class VulkanDevice;

/** Vulkan 主命令列表。 */
class VulkanCommandList final : public IRHICommandList
{
public:
    /** 使用设备分配的 Vulkan 命令缓冲区构造命令列表。 */
    VulkanCommandList(VulkanDevice& device, RHICommandListDesc desc, VkCommandBuffer commandBuffer)
        : m_device(device), m_desc(std::move(desc)), m_commandBuffer(commandBuffer) {}
    /** 释放仍然持有的命令缓冲区。 */
    ~VulkanCommandList() override;
    [[nodiscard]] RHIQueueType GetQueueType() const noexcept override { return m_desc.QueueType; }
    [[nodiscard]] RHICommandListState GetState() const noexcept override { return m_state; }
    [[nodiscard]] const RHICommandListDesc& GetDesc() const noexcept override { return m_desc; }
    RHIStatus Reset() override;
    RHIStatus Begin() override;
    RHIStatus End() override;
    RHIStatus PushDebugLabel(std::string_view, const RHIColor&) override { return RHIStatus::Ok(); }
    RHIStatus PopDebugLabel() override { return RHIStatus::Ok(); }
    RHIStatus ResourceBarriers(std::span<const RHIBarrier>) override { return RHIStatus::Ok(); }
    RHIStatus BeginRendering(const RHIRenderingDesc& desc) override;
    RHIStatus EndRendering() override;
    RHIStatus BindGraphicsPipeline(RHIGraphicsPipelineHandle pipeline) override;
    RHIStatus BindComputePipeline(RHIComputePipelineHandle) override { return RHINativeDeviceBase::Unsupported("Vulkan 计算管线"); }
    RHIStatus BindRayTracingPipeline(RHIRayTracingPipelineHandle) override { return RHINativeDeviceBase::Unsupported("Vulkan 光线追踪管线"); }
    RHIStatus BindSet(std::uint32_t, RHIBindSetHandle, std::span<const std::uint32_t>) override { return RHINativeDeviceBase::Unsupported("Vulkan 绑定集"); }
    RHIStatus PushConstants(RHIShaderStage, std::uint32_t offset, std::span<const std::byte> data) override;
    RHIStatus BindVertexBuffers(std::uint32_t firstBinding, std::span<const RHIVertexBufferBinding> bindings) override;
    RHIStatus BindIndexBuffer(const RHIIndexBufferBinding& binding) override;
    RHIStatus SetViewports(std::uint32_t, std::span<const RHIViewport> viewports) override;
    RHIStatus SetScissors(std::uint32_t, std::span<const RHIScissor> scissors) override;
    RHIStatus SetBlendConstant(const RHIColor&) override { return RHIStatus::Ok(); }
    RHIStatus SetDepthBias(float, float, float) override { return RHIStatus::Ok(); }
    RHIStatus SetStencilReference(std::uint32_t) override { return RHIStatus::Ok(); }
    RHIStatus SetStencilCompareMask(std::uint32_t) override { return RHIStatus::Ok(); }
    RHIStatus SetStencilWriteMask(std::uint32_t) override { return RHIStatus::Ok(); }
    RHIStatus SetDepthBounds(float, float) override { return RHINativeDeviceBase::Unsupported("Vulkan 深度范围"); }
    RHIStatus SetLineWidth(float) override { return RHINativeDeviceBase::Unsupported("Vulkan 线宽"); }
    RHIStatus Draw(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::uint32_t firstInstance) override;
    RHIStatus DrawIndexed(std::uint32_t count, std::uint32_t instances, std::uint32_t first, std::int32_t offset, std::uint32_t firstInstance) override;
    RHIStatus DrawIndirect(RHIBufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 间接绘制"); }
    RHIStatus DrawIndexedIndirect(RHIBufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 间接索引绘制"); }
    RHIStatus DrawMeshTasks(std::uint32_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 网格着色"); }
    RHIStatus DrawMeshTasksIndirect(RHIBufferHandle, std::uint64_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 间接网格着色"); }
    RHIStatus Dispatch(std::uint32_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 计算调度"); }
    RHIStatus DispatchIndirect(RHIBufferHandle, std::uint64_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 间接计算调度"); }
    RHIStatus CopyBuffer(RHIBufferHandle, RHIBufferHandle, std::span<const RHIBufferCopyRegion>) override { return RHINativeDeviceBase::Unsupported("Vulkan 缓冲区复制"); }
    RHIStatus CopyBufferToTexture(RHIBufferHandle, RHITextureHandle, std::span<const RHITextureBufferCopyRegion>) override { return RHINativeDeviceBase::Unsupported("Vulkan 缓冲区到纹理复制"); }
    RHIStatus CopyTextureToBuffer(RHITextureHandle, RHIBufferHandle, std::span<const RHITextureBufferCopyRegion>) override { return RHINativeDeviceBase::Unsupported("Vulkan 纹理到缓冲区复制"); }
    RHIStatus CopyTexture(RHITextureHandle, RHITextureHandle, std::span<const RHITextureCopyRegion>) override { return RHINativeDeviceBase::Unsupported("Vulkan 纹理复制"); }
    RHIStatus ResolveTexture(RHITextureHandle, RHITextureHandle, std::span<const RHITextureCopyRegion>, RHIResolveMode) override { return RHINativeDeviceBase::Unsupported("Vulkan 多重采样解析"); }
    RHIStatus ClearColorTexture(RHITextureHandle, const RHIClearColorValue&, std::span<const RHISubresourceRange>) override { return RHINativeDeviceBase::Unsupported("Vulkan 纹理清除"); }
    RHIStatus ClearDepthStencilTexture(RHITextureHandle, const RHIClearDepthStencilValue&, std::span<const RHISubresourceRange>) override { return RHINativeDeviceBase::Unsupported("Vulkan 深度清除"); }
    RHIStatus BeginQuery(RHIQueryPoolHandle, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 查询"); }
    RHIStatus EndQuery(RHIQueryPoolHandle, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 查询"); }
    RHIStatus WriteTimestamp(RHIQueryPoolHandle, std::uint32_t, RHIPipelineStage) override { return RHINativeDeviceBase::Unsupported("Vulkan 时间戳"); }
    RHIStatus ResolveQueryData(RHIQueryPoolHandle, std::uint32_t, std::uint32_t, RHIBufferHandle, std::uint64_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 查询解析"); }
    RHIStatus BuildAccelerationStructure(const RHIAccelerationStructureBuildDesc&) override { return RHINativeDeviceBase::Unsupported("Vulkan 加速结构"); }
    RHIStatus TraceRays(const RHIShaderTableDesc&, std::uint32_t, std::uint32_t, std::uint32_t) override { return RHINativeDeviceBase::Unsupported("Vulkan 光线追踪"); }
    RHIStatus ExecuteSecondary(std::span<IRHICommandList* const>) override { return RHINativeDeviceBase::Unsupported("Vulkan 次级命令列表"); }
    /** 返回可提交的原生命令缓冲区。 */
    VkCommandBuffer Native() const noexcept { return m_commandBuffer; }
private:
    VulkanDevice& m_device;
    RHICommandListDesc m_desc;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    VkPipelineLayout m_boundLayout = VK_NULL_HANDLE;
    RHICommandListState m_state = RHICommandListState::Initial;
    bool m_rendering = false;
};

/** Vulkan 1.3 原生设备。 */
class VulkanDevice final : public RHINativeDeviceBase
{
public:
    VulkanDevice(RHIAdapterInfo adapter, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue, std::uint32_t family)
        : RHINativeDeviceBase(std::move(adapter)), m_physicalDevice(physicalDevice), m_device(device), m_queue(queue), m_queueFamily(family)
    {
        vkGetPhysicalDeviceMemoryProperties(m_physicalDevice, &m_memoryProperties);
        VkPhysicalDeviceProperties properties{}; vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
        m_limits.MaxTextureDimension2D = properties.limits.maxImageDimension2D; m_limits.MaxColorAttachments = properties.limits.maxColorAttachments; m_limits.MaxVertexBuffers = properties.limits.maxVertexInputBindings; m_limits.MaxPushConstantBytes = properties.limits.maxPushConstantsSize; m_limits.MinConstantBufferOffsetAlignment = properties.limits.minUniformBufferOffsetAlignment;
        VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolInfo.queueFamilyIndex = m_queueFamily; poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool);
    }
    ~VulkanDevice() override;
    RHIStatus CreateBuffer(const RHIBufferDesc& desc, RHIBufferHandle& outBuffer) override;
    void DestroyBuffer(RHIBufferHandle handle) override;
    RHIStatus CreateTexture(const RHITextureDesc& desc, RHITextureHandle& outTexture) override;
    void DestroyTexture(RHITextureHandle handle) override;
    RHIStatus CreateTextureView(const RHITextureViewDesc& desc, RHITextureViewHandle& outView) override;
    void DestroyTextureView(RHITextureViewHandle handle) override;
    RHIStatus MapBuffer(RHIBufferHandle handle, std::uint64_t offset, std::uint64_t size, void*& outData) override;
    RHIStatus FlushMappedBufferRange(RHIBufferHandle, std::uint64_t, std::uint64_t) override { return RHIStatus::Ok(); }
    RHIStatus InvalidateMappedBufferRange(RHIBufferHandle, std::uint64_t, std::uint64_t) override { return RHIStatus::Ok(); }
    RHIStatus UnmapBuffer(RHIBufferHandle handle) override;
    RHIStatus CreateShaderModule(const RHIShaderModuleDesc& desc, RHIShaderModuleHandle& outModule) override;
    void DestroyShaderModule(RHIShaderModuleHandle handle) override;
    RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, RHIGraphicsPipelineHandle& outPipeline) override;
    void DestroyGraphicsPipeline(RHIGraphicsPipelineHandle handle) override;
    RHIStatus CreateCommandList(const RHICommandListDesc& desc, RHICommandListPtr& outList) override;
    RHIStatus CreateFence(const RHIFenceDesc& desc, RHIFenceHandle& outFence) override;
    void DestroyFence(RHIFenceHandle handle) override;
    RHIStatus WaitForFence(RHIFenceHandle handle, std::uint64_t timeoutNanoseconds) override;
    RHIStatus ResetFence(RHIFenceHandle handle) override;
    RHIStatus Submit(RHIQueueType queue, const RHIQueueSubmitDesc& desc) override;
    RHIStatus WaitIdle() override { return vkDeviceWaitIdle(m_device) == VK_SUCCESS ? RHIStatus::Ok() : RHIStatus::Error(RHIResult::DeviceLost, "Vulkan 设备等待空闲失败。"); }
    VulkanBuffer* FindBuffer(RHIBufferHandle handle) { auto it = m_buffers.find(handle.Value); return it == m_buffers.end() ? nullptr : &it->second; }
    VulkanTexture* FindTexture(RHITextureHandle handle) { auto it = m_textures.find(handle.Value); return it == m_textures.end() ? nullptr : &it->second; }
    VulkanView* FindView(RHITextureViewHandle handle) { auto it = m_views.find(handle.Value); return it == m_views.end() ? nullptr : &it->second; }
    VulkanPipeline* FindPipeline(RHIGraphicsPipelineHandle handle) { auto it = m_pipelines.find(handle.Value); return it == m_pipelines.end() ? nullptr : &it->second; }
    void FreeCommandBuffer(VkCommandBuffer commandBuffer) { if (commandBuffer != VK_NULL_HANDLE && m_commandPool != VK_NULL_HANDLE) vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer); }
    RHIStatus TransitionToColorAttachment(VkCommandBuffer commandBuffer, VulkanTexture& texture);
private:
    std::uint64_t NextHandle() noexcept { return m_nextHandle++; }
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE; VkDevice m_device = VK_NULL_HANDLE; VkQueue m_queue = VK_NULL_HANDLE; std::uint32_t m_queueFamily = 0; VkCommandPool m_commandPool = VK_NULL_HANDLE; VkPhysicalDeviceMemoryProperties m_memoryProperties{}; std::uint64_t m_nextHandle = 1; std::mutex m_mutex;
    std::unordered_map<std::uint64_t, VulkanBuffer> m_buffers; std::unordered_map<std::uint64_t, VulkanTexture> m_textures; std::unordered_map<std::uint64_t, VulkanView> m_views; std::unordered_map<std::uint64_t, VulkanShader> m_shaders; std::unordered_map<std::uint64_t, VulkanPipeline> m_pipelines; std::unordered_map<std::uint64_t, VkFence> m_fences;
};

VulkanDevice::~VulkanDevice()
{
    (void)WaitIdle();
    for (auto& [_, pipeline] : m_pipelines) { vkDestroyPipeline(m_device, pipeline.Pipeline, nullptr); vkDestroyPipelineLayout(m_device, pipeline.Layout, nullptr); }
    for (auto& [_, shader] : m_shaders) vkDestroyShaderModule(m_device, shader.Module, nullptr);
    for (auto& [_, view] : m_views) vkDestroyImageView(m_device, view.View, nullptr);
    for (auto& [_, texture] : m_textures) { vkDestroyImage(m_device, texture.Image, nullptr); vkFreeMemory(m_device, texture.Memory, nullptr); }
    for (auto& [_, buffer] : m_buffers) { vkDestroyBuffer(m_device, buffer.Buffer, nullptr); vkFreeMemory(m_device, buffer.Memory, nullptr); }
    for (auto& [_, fence] : m_fences) vkDestroyFence(m_device, fence, nullptr);
    if (m_commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(m_device, m_commandPool, nullptr);
    if (m_device != VK_NULL_HANDLE) vkDestroyDevice(m_device, nullptr);
}

RHIStatus VulkanDevice::CreateBuffer(const RHIBufferDesc& desc, RHIBufferHandle& outBuffer)
{
    if (desc.SizeInBytes == 0)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 缓冲区大小无效。");
    }

    // 1. 创建不带内存的 VkBuffer。Vulkan 将对象创建和内存分配拆为两个步骤。
    VkBufferCreateInfo createInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    createInfo.size = desc.SizeInBytes;
    createInfo.usage = 0;
    if ((static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(RHIBufferUsage::Vertex)) != 0u)
    {
        createInfo.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if ((static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(RHIBufferUsage::Index)) != 0u)
    {
        createInfo.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VulkanBuffer buffer;
    buffer.Desc = desc;
    if (vkCreateBuffer(m_device, &createInfo, nullptr, &buffer.Buffer) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::OutOfMemory, "无法创建 Vulkan 缓冲区。");
    }

    // 2. 查询缓冲区的对齐和内存类型要求。
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(m_device, buffer.Buffer, &requirements);

    const VkMemoryPropertyFlags requiredFlags = desc.MemoryUsage == RHIMemoryUsage::CpuToGpu
        ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const std::uint32_t memoryType = FindMemoryType(m_memoryProperties, requirements.memoryTypeBits, requiredFlags);
    if (memoryType == (std::numeric_limits<std::uint32_t>::max)())
    {
        vkDestroyBuffer(m_device, buffer.Buffer, nullptr);
        return RHIStatus::Error(RHIResult::OutOfMemory, "找不到 Vulkan 缓冲区内存类型。");
    }

    // 3. 分配所选类型的内存，并将其绑定给缓冲区。
    VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(m_device, &allocationInfo, nullptr, &buffer.Memory) != VK_SUCCESS)
    {
        vkDestroyBuffer(m_device, buffer.Buffer, nullptr);
        return RHIStatus::Error(RHIResult::OutOfMemory, "无法分配 Vulkan 缓冲区内存。");
    }
    if (vkBindBufferMemory(m_device, buffer.Buffer, buffer.Memory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(m_device, buffer.Memory, nullptr);
        vkDestroyBuffer(m_device, buffer.Buffer, nullptr);
        return RHIStatus::Error(RHIResult::Failure, "无法绑定 Vulkan 缓冲区内存。");
    }

    // 4. 将原生对象登记为 RHI 强类型句柄。
    std::scoped_lock lock(m_mutex);
    outBuffer.Value = NextHandle();
    m_buffers.emplace(outBuffer.Value, buffer);
    return RHIStatus::Ok();
}

void VulkanDevice::DestroyBuffer(RHIBufferHandle handle)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_buffers.find(handle.Value);
    if (it == m_buffers.end())
    {
        return;
    }

    vkDestroyBuffer(m_device, it->second.Buffer, nullptr);
    vkFreeMemory(m_device, it->second.Memory, nullptr);
    m_buffers.erase(it);
}

RHIStatus VulkanDevice::CreateTexture(const RHITextureDesc& desc, RHITextureHandle& outTexture)
{
    const bool isColorAttachment = (static_cast<std::uint32_t>(desc.Usage) & static_cast<std::uint32_t>(RHITextureUsage::ColorAttachment)) != 0u;
    if (desc.Dimension != RHITextureDimension::Texture2D || !desc.Extent.IsValid() ||
        ToVkFormat(desc.Format) == VK_FORMAT_UNDEFINED || !isColorAttachment)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan PBR 路径需要有效二维颜色附件。");
    }

    // 1. 创建未绑定内存的二维颜色图像。
    VulkanTexture texture;
    texture.Desc = desc;
    VkImageCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    createInfo.imageType = VK_IMAGE_TYPE_2D;
    createInfo.format = ToVkFormat(desc.Format);
    createInfo.extent = {desc.Extent.Width, desc.Extent.Height, 1};
    createInfo.mipLevels = 1;
    createInfo.arrayLayers = 1;
    createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    createInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(m_device, &createInfo, nullptr, &texture.Image) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::OutOfMemory, "无法创建 Vulkan 颜色附件。");
    }

    // 2. 为图像选择设备本地内存。
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(m_device, texture.Image, &requirements);
    const std::uint32_t memoryType = FindMemoryType(
        m_memoryProperties,
        requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (memoryType == (std::numeric_limits<std::uint32_t>::max)())
    {
        vkDestroyImage(m_device, texture.Image, nullptr);
        return RHIStatus::Error(RHIResult::OutOfMemory, "找不到 Vulkan 图像内存类型。");
    }

    // 3. 分配并绑定图像内存。
    VkMemoryAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocationInfo.allocationSize = requirements.size;
    allocationInfo.memoryTypeIndex = memoryType;
    if (vkAllocateMemory(m_device, &allocationInfo, nullptr, &texture.Memory) != VK_SUCCESS)
    {
        vkDestroyImage(m_device, texture.Image, nullptr);
        return RHIStatus::Error(RHIResult::OutOfMemory, "无法分配 Vulkan 图像内存。");
    }
    if (vkBindImageMemory(m_device, texture.Image, texture.Memory, 0) != VK_SUCCESS)
    {
        vkFreeMemory(m_device, texture.Memory, nullptr);
        vkDestroyImage(m_device, texture.Image, nullptr);
        return RHIStatus::Error(RHIResult::Failure, "无法绑定 Vulkan 图像内存。");
    }

    std::scoped_lock lock(m_mutex);
    outTexture.Value = NextHandle();
    m_textures.emplace(outTexture.Value, texture);
    return RHIStatus::Ok();
}

void VulkanDevice::DestroyTexture(RHITextureHandle handle)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_textures.find(handle.Value);
    if (it == m_textures.end())
    {
        return;
    }

    vkDestroyImage(m_device, it->second.Image, nullptr);
    vkFreeMemory(m_device, it->second.Memory, nullptr);
    m_textures.erase(it);
}

RHIStatus VulkanDevice::CreateTextureView(const RHITextureViewDesc& desc, RHITextureViewHandle& outView)
{
    std::scoped_lock lock(m_mutex);
    VulkanTexture* texture = FindTexture(desc.Texture);
    if (texture == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 纹理视图引用未知纹理。");
    }

    VulkanView view;
    view.Texture = desc.Texture;
    VkImageViewCreateInfo createInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    createInfo.image = texture->Image;
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = ToVkFormat(texture->Desc.Format);
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.layerCount = 1;
    if (vkCreateImageView(m_device, &createInfo, nullptr, &view.View) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 Vulkan 纹理视图。");
    }

    outView.Value = NextHandle();
    m_views.emplace(outView.Value, view);
    return RHIStatus::Ok();
}

void VulkanDevice::DestroyTextureView(RHITextureViewHandle handle)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_views.find(handle.Value);
    if (it == m_views.end())
    {
        return;
    }

    vkDestroyImageView(m_device, it->second.View, nullptr);
    m_views.erase(it);
}

RHIStatus VulkanDevice::MapBuffer(RHIBufferHandle handle, std::uint64_t offset, std::uint64_t size, void*& outData)
{
    std::scoped_lock lock(m_mutex);
    VulkanBuffer* buffer = FindBuffer(handle);
    if (buffer == nullptr || buffer->Desc.MemoryUsage != RHIMemoryUsage::CpuToGpu ||
        offset + size > buffer->Desc.SizeInBytes)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 映射范围无效。");
    }

    void* mapped = nullptr;
    if (vkMapMemory(m_device, buffer->Memory, offset, size, 0, &mapped) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法映射 Vulkan 缓冲区。");
    }
    outData = mapped;
    return RHIStatus::Ok();
}

RHIStatus VulkanDevice::UnmapBuffer(RHIBufferHandle handle)
{
    std::scoped_lock lock(m_mutex);
    VulkanBuffer* buffer = FindBuffer(handle);
    if (buffer == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 取消映射引用未知缓冲区。");
    }

    vkUnmapMemory(m_device, buffer->Memory);
    return RHIStatus::Ok();
}

RHIStatus VulkanDevice::CreateShaderModule(const RHIShaderModuleDesc& desc, RHIShaderModuleHandle& outModule)
{
    if (desc.CodeFormat != RHIShaderCodeFormat::IntermediateBinary || desc.Code.empty() ||
        (desc.Code.size() % sizeof(std::uint32_t)) != 0)
    {
        return RHIStatus::Error(RHIResult::Unsupported, "Vulkan PBR 路径只接受 SPIR-V 二进制着色器。");
    }

    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = desc.Code.size();
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(desc.Code.data());

    VulkanShader shader;
    shader.Desc = desc;
    if (vkCreateShaderModule(m_device, &createInfo, nullptr, &shader.Module) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 Vulkan 着色器模块。");
    }

    std::scoped_lock lock(m_mutex);
    outModule.Value = NextHandle();
    m_shaders.emplace(outModule.Value, std::move(shader));
    return RHIStatus::Ok();
}

void VulkanDevice::DestroyShaderModule(RHIShaderModuleHandle handle)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_shaders.find(handle.Value);
    if (it == m_shaders.end())
    {
        return;
    }

    vkDestroyShaderModule(m_device, it->second.Module, nullptr);
    m_shaders.erase(it);
}

RHIStatus VulkanDevice::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc, RHIGraphicsPipelineHandle& outPipeline)
{
    std::scoped_lock lock(m_mutex);

    // 第 1 步：查找顶点与片段阶段，以及它们对应的 SPIR-V 模块。
    const RHIShaderStageDesc* vertexStage = nullptr;
    const RHIShaderStageDesc* fragmentStage = nullptr;
    for (const RHIShaderStageDesc& stage : desc.ShaderStages)
    {
        if (stage.Stage == RHIShaderStage::Vertex)
        {
            vertexStage = &stage;
        }
        else if (stage.Stage == RHIShaderStage::Pixel)
        {
            fragmentStage = &stage;
        }
    }
    if (vertexStage == nullptr || fragmentStage == nullptr ||
        desc.RenderTargets.ColorFormats.size() != 1 || desc.VertexBuffers.empty())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan PBR 图形管线描述不完整。");
    }

    const auto vertexModule = m_shaders.find(vertexStage->Module.Value);
    const auto fragmentModule = m_shaders.find(fragmentStage->Module.Value);
    if (vertexModule == m_shaders.end() || fragmentModule == m_shaders.end())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 图形管线引用了未知着色器。");
    }

    // 第 2 步：管线布局只包含所有图形阶段共享的 128 字节 PBR 推送常量。
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.size = 128;

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;

    VulkanPipeline pipeline;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &pipeline.Layout) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 Vulkan PBR 管线布局。");
    }

    // 第 3 步：把 RHI 着色器阶段和顶点输入布局翻译为 Vulkan 描述。
    VkPipelineShaderStageCreateInfo nativeVertexStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    nativeVertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    nativeVertexStage.module = vertexModule->second.Module;
    nativeVertexStage.pName = vertexStage->EntryPoint.c_str();

    VkPipelineShaderStageCreateInfo nativeFragmentStage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    nativeFragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    nativeFragmentStage.module = fragmentModule->second.Module;
    nativeFragmentStage.pName = fragmentStage->EntryPoint.c_str();
    const VkPipelineShaderStageCreateInfo shaderStages[] = {nativeVertexStage, nativeFragmentStage};

    std::vector<VkVertexInputBindingDescription> bindings;
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (std::uint32_t bindingIndex = 0; bindingIndex < desc.VertexBuffers.size(); ++bindingIndex)
    {
        const RHIVertexBufferLayout& vertexBuffer = desc.VertexBuffers[bindingIndex];
        bindings.push_back({
            bindingIndex,
            vertexBuffer.Stride,
            vertexBuffer.StepMode == RHIVertexStepMode::PerInstance
                ? VK_VERTEX_INPUT_RATE_INSTANCE
                : VK_VERTEX_INPUT_RATE_VERTEX,
        });

        for (const RHIVertexAttribute& attribute : vertexBuffer.Attributes)
        {
            attributes.push_back({
                attribute.Location,
                bindingIndex,
                ToVkVertexFormat(attribute.Format),
                attribute.Offset,
            });
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = static_cast<std::uint32_t>(bindings.size());
    vertexInput.pVertexBindingDescriptions = bindings.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    // 第 4 步：描述固定功能状态。视口和裁剪框保留为动态状态，由每帧命令设置。
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = desc.Rasterizer.CullMode == RHICullMode::None
        ? VK_CULL_MODE_NONE
        : desc.Rasterizer.CullMode == RHICullMode::Front ? VK_CULL_MODE_FRONT_BIT : VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = desc.Rasterizer.FrontFace == RHIFrontFace::CounterClockwise
        ? VK_FRONT_FACE_COUNTER_CLOCKWISE
        : VK_FRONT_FACE_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blendAttachment;

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(std::size(dynamicStates));
    dynamicState.pDynamicStates = dynamicStates;

    // 第 5 步：使用动态渲染时，颜色格式通过 VkPipelineRenderingCreateInfo 声明。
    const VkFormat colorFormat = ToVkFormat(desc.RenderTargets.ColorFormats[0]);
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;

    VkGraphicsPipelineCreateInfo createInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    createInfo.pNext = &renderingInfo;
    createInfo.stageCount = static_cast<std::uint32_t>(std::size(shaderStages));
    createInfo.pStages = shaderStages;
    createInfo.pVertexInputState = &vertexInput;
    createInfo.pInputAssemblyState = &inputAssembly;
    createInfo.pViewportState = &viewportState;
    createInfo.pRasterizationState = &rasterizer;
    createInfo.pMultisampleState = &multisample;
    createInfo.pColorBlendState = &colorBlend;
    createInfo.pDynamicState = &dynamicState;
    createInfo.layout = pipeline.Layout;

    if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &createInfo, nullptr, &pipeline.Pipeline) != VK_SUCCESS)
    {
        vkDestroyPipelineLayout(m_device, pipeline.Layout, nullptr);
        return RHIStatus::Error(RHIResult::Failure, "无法创建 Vulkan PBR 图形管线。");
    }

    outPipeline.Value = NextHandle();
    m_pipelines.emplace(outPipeline.Value, pipeline);
    return RHIStatus::Ok();
}

void VulkanDevice::DestroyGraphicsPipeline(RHIGraphicsPipelineHandle handle)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_pipelines.find(handle.Value);
    if (it == m_pipelines.end())
    {
        return;
    }

    vkDestroyPipeline(m_device, it->second.Pipeline, nullptr);
    vkDestroyPipelineLayout(m_device, it->second.Layout, nullptr);
    m_pipelines.erase(it);
}

RHIStatus VulkanDevice::CreateCommandList(const RHICommandListDesc& desc, RHICommandListPtr& outList)
{
    if (desc.QueueType != RHIQueueType::Graphics || desc.Level != RHICommandListLevel::Primary)
    {
        return RHIStatus::Error(RHIResult::Unsupported, "Vulkan PBR 路径只支持图形主命令列表。");
    }

    // 命令池归设备所有；每个 RHI 命令列表从中取得一个可独立录制的命令缓冲区。
    VkCommandBufferAllocateInfo allocationInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocationInfo.commandPool = m_commandPool;
    allocationInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocationInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(m_device, &allocationInfo, &commandBuffer) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::OutOfMemory, "无法分配 Vulkan 命令缓冲区。");
    }

    outList = std::make_shared<VulkanCommandList>(*this, desc, commandBuffer);
    return RHIStatus::Ok();
}

RHIStatus VulkanDevice::CreateFence(const RHIFenceDesc& desc, RHIFenceHandle& outFence)
{
    VkFenceCreateInfo createInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (desc.InitiallySignaled)
    {
        createInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    }

    VkFence fence = VK_NULL_HANDLE;
    if (vkCreateFence(m_device, &createInfo, nullptr, &fence) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法创建 Vulkan 围栏。");
    }

    std::scoped_lock lock(m_mutex);
    outFence.Value = NextHandle();
    m_fences.emplace(outFence.Value, fence);
    return RHIStatus::Ok();
}

void VulkanDevice::DestroyFence(RHIFenceHandle handle)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_fences.find(handle.Value);
    if (it == m_fences.end())
    {
        return;
    }

    vkDestroyFence(m_device, it->second, nullptr);
    m_fences.erase(it);
}

RHIStatus VulkanDevice::WaitForFence(RHIFenceHandle handle, std::uint64_t timeoutNanoseconds)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_fences.find(handle.Value);
    if (it == m_fences.end())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 等待引用未知围栏。");
    }

    const VkResult result = vkWaitForFences(m_device, 1, &it->second, VK_TRUE, timeoutNanoseconds);
    if (result == VK_SUCCESS)
    {
        return RHIStatus::Ok();
    }
    return RHIStatus::Error(
        result == VK_TIMEOUT ? RHIResult::NotReady : RHIResult::Failure,
        "等待 Vulkan 围栏失败或超时。");
}

RHIStatus VulkanDevice::ResetFence(RHIFenceHandle handle)
{
    std::scoped_lock lock(m_mutex);
    const auto it = m_fences.find(handle.Value);
    if (it == m_fences.end())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 重置引用未知围栏。");
    }
    if (vkResetFences(m_device, 1, &it->second) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法重置 Vulkan 围栏。");
    }
    return RHIStatus::Ok();
}

RHIStatus VulkanDevice::Submit(RHIQueueType queue, const RHIQueueSubmitDesc& desc)
{
    if (queue != RHIQueueType::Graphics)
    {
        return RHIStatus::Error(RHIResult::Unsupported, "Vulkan PBR 路径只支持图形队列。");
    }

    // 提交前先把 RHI 命令列表转换为 Vulkan 命令缓冲区，并验证其已经结束录制。
    std::vector<VkCommandBuffer> commandBuffers;
    commandBuffers.reserve(desc.CommandLists.size());
    for (const RHICommandListPtr& list : desc.CommandLists)
    {
        const auto vulkanList = std::dynamic_pointer_cast<VulkanCommandList>(list);
        if (vulkanList == nullptr || vulkanList->GetState() != RHICommandListState::Executable)
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 提交包含无效命令列表。");
        }
        commandBuffers.push_back(vulkanList->Native());
    }

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = static_cast<std::uint32_t>(commandBuffers.size());
    submitInfo.pCommandBuffers = commandBuffers.data();

    VkFence completionFence = VK_NULL_HANDLE;
    if (desc.SignalFence.IsValid())
    {
        std::scoped_lock lock(m_mutex);
        const auto it = m_fences.find(desc.SignalFence.Value);
        if (it == m_fences.end())
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 提交引用未知围栏。");
        }

        completionFence = it->second;
        if (vkResetFences(m_device, 1, &completionFence) != VK_SUCCESS)
        {
            return RHIStatus::Error(RHIResult::Failure, "无法重置 Vulkan 提交围栏。");
        }
    }

    if (vkQueueSubmit(m_queue, 1, &submitInfo, completionFence) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法提交 Vulkan 图形队列。");
    }
    return RHIStatus::Ok();
}

RHIStatus VulkanDevice::TransitionToColorAttachment(VkCommandBuffer commandBuffer, VulkanTexture& texture)
{
    if (texture.Layout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL)
    {
        return RHIStatus::Ok();
    }

    // 动态渲染要求颜色附件处于 COLOR_ATTACHMENT_OPTIMAL 布局。
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = texture.Layout;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.image = texture.Image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &barrier);
    texture.Layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    return RHIStatus::Ok();
}

VulkanCommandList::~VulkanCommandList()
{
    m_device.FreeCommandBuffer(m_commandBuffer);
}

RHIStatus VulkanCommandList::Reset()
{
    if (m_state == RHICommandListState::Pending)
    {
        return RHIStatus::Error(RHIResult::NotReady, "Vulkan 命令列表仍在执行。");
    }
    if (vkResetCommandBuffer(m_commandBuffer, 0) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法重置 Vulkan 命令缓冲区。");
    }

    m_boundLayout = VK_NULL_HANDLE;
    m_state = RHICommandListState::Initial;
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::Begin()
{
    if (m_state != RHICommandListState::Initial)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 命令列表必须先重置再开始。");
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(m_commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法开始 Vulkan 命令缓冲区。");
    }

    m_state = RHICommandListState::Recording;
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::End()
{
    if (m_state != RHICommandListState::Recording || m_rendering)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "结束 Vulkan 命令缓冲区前必须结束渲染区域。");
    }
    if (vkEndCommandBuffer(m_commandBuffer) != VK_SUCCESS)
    {
        return RHIStatus::Error(RHIResult::Failure, "无法结束 Vulkan 命令缓冲区。");
    }

    m_state = RHICommandListState::Executable;
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::BeginRendering(const RHIRenderingDesc& desc)
{
    if (m_state != RHICommandListState::Recording || m_rendering || desc.ColorAttachments.size() != 1)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan PBR 需要一个颜色附件。");
    }

    const RHIRenderingColorAttachment& colorAttachment = desc.ColorAttachments[0];
    VulkanView* view = m_device.FindView(colorAttachment.View);
    if (view == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 渲染区域引用无效视图。");
    }

    VulkanTexture* texture = m_device.FindTexture(view->Texture);
    if (texture == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 视图引用无效纹理。");
    }

    // 动态渲染不会隐式转换布局，所以在开始渲染前显式转为颜色附件布局。
    RHIStatus status = m_device.TransitionToColorAttachment(m_commandBuffer, *texture);
    if (!status.Succeeded())
    {
        return status;
    }

    VkClearValue clearValue{};
    clearValue.color.float32[0] = colorAttachment.ClearValue.R;
    clearValue.color.float32[1] = colorAttachment.ClearValue.G;
    clearValue.color.float32[2] = colorAttachment.ClearValue.B;
    clearValue.color.float32[3] = colorAttachment.ClearValue.A;

    VkRenderingAttachmentInfo nativeAttachment{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    nativeAttachment.imageView = view->View;
    nativeAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    nativeAttachment.loadOp = colorAttachment.LoadOp == RHILoadOp::Clear
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    nativeAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    nativeAttachment.clearValue = clearValue;

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea.offset = {desc.RenderArea.X, desc.RenderArea.Y};
    renderingInfo.renderArea.extent = {desc.RenderArea.Width, desc.RenderArea.Height};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &nativeAttachment;
    vkCmdBeginRendering(m_commandBuffer, &renderingInfo);

    m_rendering = true;
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::EndRendering()
{
    if (!m_rendering)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 当前没有活跃渲染区域。");
    }

    vkCmdEndRendering(m_commandBuffer);
    m_rendering = false;
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::BindGraphicsPipeline(RHIGraphicsPipelineHandle handle)
{
    VulkanPipeline* pipeline = m_device.FindPipeline(handle);
    if (m_state != RHICommandListState::Recording || pipeline == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 绑定无效图形管线。");
    }

    vkCmdBindPipeline(m_commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->Pipeline);
    // 后续 PushConstants 必须使用与当前管线匹配的布局。
    m_boundLayout = pipeline->Layout;
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::PushConstants(RHIShaderStage, std::uint32_t offset, std::span<const std::byte> data)
{
    if (m_boundLayout == VK_NULL_HANDLE || offset + data.size() > 128)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 推送常量范围无效。");
    }

    vkCmdPushConstants(
        m_commandBuffer,
        m_boundLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        offset,
        static_cast<std::uint32_t>(data.size()),
        data.data());
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::BindVertexBuffers(
    std::uint32_t firstBinding,
    std::span<const RHIVertexBufferBinding> bindings)
{
    std::vector<VkBuffer> nativeBuffers;
    std::vector<VkDeviceSize> nativeOffsets;
    nativeBuffers.reserve(bindings.size());
    nativeOffsets.reserve(bindings.size());

    for (const RHIVertexBufferBinding& binding : bindings)
    {
        VulkanBuffer* buffer = m_device.FindBuffer(binding.Buffer);
        if (buffer == nullptr || binding.Offset > buffer->Desc.SizeInBytes)
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 顶点缓冲区绑定无效。");
        }
        nativeBuffers.push_back(buffer->Buffer);
        nativeOffsets.push_back(binding.Offset);
    }

    vkCmdBindVertexBuffers(
        m_commandBuffer,
        firstBinding,
        static_cast<std::uint32_t>(nativeBuffers.size()),
        nativeBuffers.data(),
        nativeOffsets.data());
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::BindIndexBuffer(const RHIIndexBufferBinding& binding)
{
    VulkanBuffer* buffer = m_device.FindBuffer(binding.Buffer);
    if (buffer == nullptr || binding.Offset > buffer->Desc.SizeInBytes)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 索引缓冲区绑定无效。");
    }

    const VkIndexType indexType = binding.Type == RHIIndexType::UInt16
        ? VK_INDEX_TYPE_UINT16
        : VK_INDEX_TYPE_UINT32;
    vkCmdBindIndexBuffer(m_commandBuffer, buffer->Buffer, binding.Offset, indexType);
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::SetViewports(std::uint32_t first, std::span<const RHIViewport> viewports)
{
    std::vector<VkViewport> nativeViewports;
    nativeViewports.reserve(viewports.size());
    for (const RHIViewport& viewport : viewports)
    {
        nativeViewports.push_back({
            viewport.X,
            viewport.Y,
            viewport.Width,
            viewport.Height,
            viewport.MinDepth,
            viewport.MaxDepth,
        });
    }

    vkCmdSetViewport(m_commandBuffer, first, static_cast<std::uint32_t>(nativeViewports.size()), nativeViewports.data());
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::SetScissors(std::uint32_t first, std::span<const RHIScissor> scissors)
{
    std::vector<VkRect2D> nativeScissors;
    nativeScissors.reserve(scissors.size());
    for (const RHIScissor& scissor : scissors)
    {
        nativeScissors.push_back({
            {scissor.X, scissor.Y},
            {scissor.Width, scissor.Height},
        });
    }

    vkCmdSetScissor(m_commandBuffer, first, static_cast<std::uint32_t>(nativeScissors.size()), nativeScissors.data());
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::Draw(
    std::uint32_t count,
    std::uint32_t instances,
    std::uint32_t first,
    std::uint32_t firstInstance)
{
    vkCmdDraw(m_commandBuffer, count, instances, first, firstInstance);
    return RHIStatus::Ok();
}

RHIStatus VulkanCommandList::DrawIndexed(
    std::uint32_t count,
    std::uint32_t instances,
    std::uint32_t first,
    std::int32_t offset,
    std::uint32_t firstInstance)
{
    vkCmdDrawIndexed(m_commandBuffer, count, instances, first, offset, firstInstance);
    return RHIStatus::Ok();
}

class VulkanBackend final : public IRHIBackend
{
public:
    ~VulkanBackend() override { if (m_instance != VK_NULL_HANDLE) vkDestroyInstance(m_instance, nullptr); }
    [[nodiscard]] const RHIBackendInfo& GetInfo() const noexcept override { return m_info; }
    RHIStatus EnumerateAdapters(std::vector<RHIAdapterInfo>& outAdapters) override;
    RHIStatus CreateDevice(const RHIDeviceDesc& desc, RHIDevicePtr& outDevice) override;
private:
    RHIStatus EnsureInstance();
    RHIBackendInfo m_info{{"vulkan"}, "Vulkan", {1, 1, 0}}; VkInstance m_instance = VK_NULL_HANDLE;
};

RHIStatus VulkanBackend::EnsureInstance() { if (m_instance != VK_NULL_HANDLE) return RHIStatus::Ok(); VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO}; application.pApplicationName = "RHI"; application.apiVersion = VK_API_VERSION_1_3; VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo = &application; return vkCreateInstance(&info, nullptr, &m_instance) == VK_SUCCESS ? RHIStatus::Ok() : RHIStatus::Error(RHIResult::Failure, "无法创建 Vulkan 实例。"); }
RHIStatus VulkanBackend::EnumerateAdapters(std::vector<RHIAdapterInfo>& outAdapters) { RHIStatus status = EnsureInstance(); if (!status.Succeeded()) return status; std::uint32_t count = 0; if (vkEnumeratePhysicalDevices(m_instance, &count, nullptr) != VK_SUCCESS) return RHIStatus::Error(RHIResult::Failure, "无法枚举 Vulkan 物理设备。"); std::vector<VkPhysicalDevice> devices(count); if (count && vkEnumeratePhysicalDevices(m_instance, &count, devices.data()) != VK_SUCCESS) return RHIStatus::Error(RHIResult::Failure, "无法读取 Vulkan 物理设备。"); outAdapters.clear(); for (std::uint32_t index = 0; index < count; ++index) { VkPhysicalDeviceProperties properties{}; VkPhysicalDeviceMemoryProperties memory{}; vkGetPhysicalDeviceProperties(devices[index], &properties); vkGetPhysicalDeviceMemoryProperties(devices[index], &memory); std::uint64_t local = 0; for (std::uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap) if (memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) local += memory.memoryHeaps[heap].size; outAdapters.push_back({properties.deviceName, std::to_string(index), local, properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU}); } return RHIStatus::Ok(); }
RHIStatus VulkanBackend::CreateDevice(const RHIDeviceDesc& desc, RHIDevicePtr& outDevice) { RHIStatus status = EnsureInstance(); if (!status.Succeeded()) return status; std::vector<RHIAdapterInfo> adapters; if (!(status = EnumerateAdapters(adapters)).Succeeded()) return status; std::uint32_t selected = 0; if (!desc.AdapterIdentifier.empty()) { try { selected = static_cast<std::uint32_t>(std::stoul(desc.AdapterIdentifier)); } catch (...) { return RHIStatus::Error(RHIResult::InvalidArgument, "Vulkan 适配器标识必须是枚举索引。"); } } if (selected >= adapters.size()) return RHIStatus::Error(RHIResult::InvalidArgument, "请求的 Vulkan 适配器不存在。"); std::uint32_t count = 0; vkEnumeratePhysicalDevices(m_instance, &count, nullptr); std::vector<VkPhysicalDevice> devices(count); vkEnumeratePhysicalDevices(m_instance, &count, devices.data()); VkPhysicalDevice physical = devices[selected]; std::uint32_t familyCount = 0; vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr); std::vector<VkQueueFamilyProperties> families(familyCount); vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data()); std::uint32_t family = std::numeric_limits<std::uint32_t>::max(); for (std::uint32_t index = 0; index < familyCount; ++index) if (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = index; break; } if (family == (std::numeric_limits<std::uint32_t>::max)()) return RHIStatus::Error(RHIResult::Unsupported, "Vulkan 适配器没有图形队列。"); float priority = 1.0f; VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority; VkPhysicalDeviceDynamicRenderingFeatures dynamic{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES}; dynamic.dynamicRendering = VK_TRUE; VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.pNext = &dynamic; deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo; VkDevice device = VK_NULL_HANDLE; if (vkCreateDevice(physical, &deviceInfo, nullptr, &device) != VK_SUCCESS) return RHIStatus::Error(RHIResult::Failure, "无法创建启用动态渲染的 Vulkan 设备。"); VkQueue queue = VK_NULL_HANDLE; vkGetDeviceQueue(device, family, 0, &queue); outDevice = std::make_shared<VulkanDevice>(adapters[selected], physical, device, queue, family); return RHIStatus::Ok(); }

class VulkanFactory final : public IRHIBackendFactory
{
public:
    [[nodiscard]] const RHIBackendInfo& GetInfo() const noexcept override { return m_info; }
    RHIStatus CreateBackend(RHIBackendPtr& outBackend) override { outBackend = std::make_shared<VulkanBackend>(); return RHIStatus::Ok(); }
private:
    RHIBackendInfo m_info{{"vulkan"}, "Vulkan", {1, 1, 0}};
};

} // namespace

RHIStatus RegisterVulkanBackend(RHIBackendRegistry& registry) { return registry.RegisterFactory(std::make_shared<VulkanFactory>()); }

} // namespace RHI
