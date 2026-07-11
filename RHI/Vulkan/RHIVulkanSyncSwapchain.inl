#pragma once

#include "RHIVulkanPrivate.inl"

namespace rhi {

RHIQueryPool RHIVulkan::createQueryPool(const RHIQueryPoolDesc& desc) {
    Impl::QueryPoolResource resource{};
    resource.desc = desc;

    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryCount = desc.queryCount;
    switch (desc.type) {
    case RHIQueryType::Timestamp:
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        break;
    case RHIQueryType::Occlusion:
        queryInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
        break;
    case RHIQueryType::PipelineStatistics:
        queryInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        queryInfo.pipelineStatistics = 0;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::InputAssemblyVertices))           queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::InputAssemblyPrimitives))         queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::VertexShaderInvocations))         queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::GeometryShaderInvocations))       queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::GeometryShaderPrimitives))        queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::ClippingInvocations))             queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::ClippingPrimitives))              queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::FragmentShaderInvocations))       queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::TessControlShaderPatches))        queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::TessEvaluationShaderInvocations)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;
        if (RHIHasAny(desc.statistics, RHIPipelineStatisticFlags::ComputeShaderInvocations))        queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
        break;
    }

    if (vkCreateQueryPool(impl_->native.device, &queryInfo, nullptr, &resource.pool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateQueryPool failed");
    }

    const RHIQueryPool handle = makeRenderHandle<RHIQueryPool>(impl_->queryPools, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<u64>(impl_->queryPools.back().pool), desc.debugName);
    return handle;
}

// Semaphore/Fence 都是同步对象：semaphore 主要在 queue 之间或 acquire/present 之间传递依赖，
// fence 用于 CPU 等 GPU 完成。timeline semaphore 需要 Vulkan 1.2 feature 支持。
RHIGPUWaitGPUSignal RHIVulkan::createGPUWaitGPUSignal(const RHIGPUWaitGPUSignalDesc& desc) {
    if (desc.type == RHIGPUWaitGPUSignalType::Timeline && !impl_->supportsTimelineSemaphore) {
        throw std::runtime_error("The current Vulkan device does not support timeline gpuWaitGPUSignals");
    }

    Impl::GPUWaitGPUSignalResource resource{};
    resource.desc = desc;

    VkSemaphoreTypeCreateInfo typeInfo{};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = desc.type == RHIGPUWaitGPUSignalType::Timeline ? VK_SEMAPHORE_TYPE_TIMELINE : VK_SEMAPHORE_TYPE_BINARY;
    typeInfo.initialValue = desc.initialValue;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &typeInfo;

    if (vkCreateSemaphore(impl_->native.device, &semaphoreInfo, nullptr, &resource.semaphore) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSemaphore failed");
    }

    const RHIGPUWaitGPUSignal handle = makeRenderHandle<RHIGPUWaitGPUSignal>(impl_->gpuWaitGPUSignals, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<u64>(impl_->gpuWaitGPUSignals.back().semaphore), desc.debugName);
    return handle;
}

RHICPUWaitGPUSignal RHIVulkan::createCPUWaitGPUSignal(const RHICPUWaitGPUSignalDesc& desc) {
    Impl::CPUWaitGPUSignalResource resource{};
    resource.desc = desc;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = desc.signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

    if (vkCreateFence(impl_->native.device, &fenceInfo, nullptr, &resource.fence) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateFence failed");
    }

    const RHICPUWaitGPUSignal handle = makeRenderHandle<RHICPUWaitGPUSignal>(impl_->cpuWaitGPUSignals, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_FENCE, reinterpret_cast<u64>(impl_->cpuWaitGPUSignals.back().fence), desc.debugName);
    return handle;
}

// Swapchain 是窗口系统提供的可呈现图像队列。Vulkan swapchain image 由 VkSwapchainKHR 拥有，
// 不能像普通 texture 那样释放内存，所以这里把它们包装成 TextureResource，并把 ownsImage
// 设为 false；这样上层仍然能用 RHITexture/RHITextureView 统一描述后备缓冲。
RHISwapchain RHIVulkan::createSwapchain(const RHISwapchainDesc& desc) {
    if (impl_->native.surface == VK_NULL_HANDLE) {
        throw std::runtime_error("createSwapchain requires a valid VkSurfaceKHR during initialization");
    }

    const VulkanSwapchainSupport support = querySwapchainSupport(impl_->native.physicalDevice, impl_->native.surface);
    if (!support.isUsable()) {
        throw std::runtime_error("The Vulkan surface does not provide a usable swapchain format and present mode");
    }

    // VkSwapchainCreateInfoKHR::imageUsage 不能随意填写，必须是 surface capabilities
    // 明确支持的位。COLOR_ATTACHMENT 是直接渲染的硬需求；TRANSFER_DST 仅在支持时启用。
    if ((support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("The Vulkan surface does not support swapchain color-attachment usage");
    }
    VkImageUsageFlags swapchainImageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if ((support.capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0) {
        swapchainImageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }

    const VkSurfaceFormatKHR            selectedFormat       = chooseSwapchainFormat(support, desc);
    const RHIFormat                        selectedEngineFormat = fromVkFormat(selectedFormat.format);
    const RHIFormat                        swapchainFormat      = selectedEngineFormat == RHIFormat::Undefined ? desc.preferredFormat : selectedEngineFormat;
    const VkPresentModeKHR              selectedPresentMode  = chooseSwapchainPresentMode(support, desc.presentMode);
    const VkExtent2D                    extent               = chooseSwapchainExtent(support, desc.extent);
    const u32                           imageCount           = chooseSwapchainImageCount(support, desc.imageCount);
    const VkSurfaceTransformFlagBitsKHR selectedTransform    = chooseSwapchainTransform(support, desc.preTransform);
    const VkCompositeAlphaFlagBitsKHR   selectedAlpha        = chooseSwapchainCompositeAlpha(support, desc.compositeAlpha);

    std::array<u32, 2> queueFamilies = {impl_->queueFamilies.graphics, impl_->queueFamilies.present};

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = impl_->native.surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = selectedFormat.format;
    swapchainInfo.imageColorSpace = selectedFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = swapchainImageUsage;
    if (impl_->queueFamilies.graphics != impl_->queueFamilies.present) {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = static_cast<u32>(queueFamilies.size());
        swapchainInfo.pQueueFamilyIndices = queueFamilies.data();
    } else {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    swapchainInfo.preTransform = selectedTransform;
    swapchainInfo.compositeAlpha = selectedAlpha;
    swapchainInfo.presentMode = selectedPresentMode;
    swapchainInfo.clipped = VK_TRUE;

    Impl::SwapchainResource resource{};
    resource.desc = desc;
    resource.format = selectedFormat.format;
    resource.extent = extent;
    if (vkCreateSwapchainKHR(impl_->native.device, &swapchainInfo, nullptr, &resource.swapchain) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSwapchainKHR failed");
    }

    u32 swapchainImageCount = 0;
    if (vkGetSwapchainImagesKHR(impl_->native.device, resource.swapchain, &swapchainImageCount, nullptr) != VK_SUCCESS ||
        swapchainImageCount == 0) {
        vkDestroySwapchainKHR(impl_->native.device, resource.swapchain, nullptr);
        throw std::runtime_error("vkGetSwapchainImagesKHR(count) failed or returned zero images");
    }
    std::vector<VkImage> images(swapchainImageCount);
    if (vkGetSwapchainImagesKHR(impl_->native.device, resource.swapchain, &swapchainImageCount, images.data()) != VK_SUCCESS) {
        vkDestroySwapchainKHR(impl_->native.device, resource.swapchain, nullptr);
        throw std::runtime_error("vkGetSwapchainImagesKHR(images) failed");
    }

    for (u32 index = 0; index < swapchainImageCount; ++index) {
        Impl::TextureResource texture{};
        texture.ownsImage = false;
        texture.image = images[index];
        texture.currentState = RHIResourceState::Undefined;
        texture.desc.debugName = desc.debugName + ".Image" + std::to_string(index);
        texture.desc.dimension = RHITextureDimension::Texture2D;
        texture.desc.extent = {extent.width, extent.height, 1};
        texture.desc.arrayLayers = 1;
        texture.desc.mipLevels = 1;
        texture.desc.format = swapchainFormat;
        texture.desc.samples = RHISampleCount::Count1;
        texture.desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Present;
        texture.desc.initialState = RHIResourceState::Present;

        RHITexture textureHandle = makeRenderHandle<RHITexture>(impl_->textures, std::move(texture));
        resource.images.push_back(textureHandle);

        RHITextureViewDesc viewDesc{};
        viewDesc.debugName = desc.debugName + ".ImageView" + std::to_string(index);
        viewDesc.texture = textureHandle;
        viewDesc.dimension = RHITextureViewDimension::View2D;
        viewDesc.format = swapchainFormat;
        viewDesc.aspect = RHITextureAspect::Color;
        resource.imageViews.push_back(createTextureView(viewDesc));
    }

    const RHISwapchain handle = makeRenderHandle<RHISwapchain>(impl_->swapchains, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<u64>(impl_->swapchains.back().swapchain), desc.debugName);
    return handle;
}

std::vector<RHITexture> RHIVulkan::getSwapchainImages(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->images;
    }
    return {};
}

std::vector<RHITextureView> RHIVulkan::getSwapchainImageViews(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->imageViews;
    }
    return {};
}

RHIFormat RHIVulkan::getSwapchainFormat(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return fromVkFormat(swapchain->format);
    }
    return RHIFormat::Undefined;
}

RHIExtent2D RHIVulkan::getSwapchainExtent(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return {swapchain->extent.width, swapchain->extent.height};
    }
    return {};
}

bool RHIVulkan::acquireNextImage(
    RHISwapchain swapchainHandle,
    RHIGPUWaitGPUSignal gpuWaitGPUSignal,
    RHICPUWaitGPUSignal cpuWaitGPUSignal,
    u32* imageIndex,
    std::string* errorMessage) {
    // acquire 只是向 swapchain 取下一张可写图像的索引，并可选 signal semaphore/fence。
    // 真正把图像从 Present 转到 ColorAttachment 的 layout transition 在 frame 录制阶段完成。
    const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, swapchainHandle);
    const Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, gpuWaitGPUSignal);
    const Impl::CPUWaitGPUSignalResource* fence = getRenderResource(impl_->cpuWaitGPUSignals, cpuWaitGPUSignal);
    if (swapchain == nullptr || swapchain->swapchain == VK_NULL_HANDLE || imageIndex == nullptr) {
        if (errorMessage != nullptr) *errorMessage = "acquireNextImage 参数无效";
        return false;
    }

    const VkResult result = vkAcquireNextImageKHR(
        impl_->native.device,
        swapchain->swapchain,
        std::numeric_limits<u64>::max(),
        semaphore != nullptr ? semaphore->semaphore : VK_NULL_HANDLE,
        fence != nullptr ? fence->fence : VK_NULL_HANDLE,
        imageIndex);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        if (errorMessage != nullptr) *errorMessage = "vkAcquireNextImageKHR 失败";
        return false;
    }
    return true;
}

// 低层 submit：把外部已经准备好的同步关系提交到指定 queue。
// 这个函数不录制命令，只把 wait/signal semaphore、timeline value 和 fence 翻译成 VkSubmitInfo。
bool RHIVulkan::submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) {
    std::vector<VkSemaphore> waitSignals;
    std::vector<VkPipelineStageFlags> waitStages;
    std::vector<VkSemaphore> signalSemaphores;
    std::vector<u64> waitValues;
    std::vector<u64> signalValues;
    bool usesTimelineSemaphore = false;

    for (const RHIQueueWaitDesc& wait : desc.waits) {
        const Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, wait.signal);
        if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
            if (errorMessage != nullptr) *errorMessage = "RHIQueueSubmitDesc 包含无效 wait semaphore";
            return false;
        }
        usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == RHIGPUWaitGPUSignalType::Timeline;
        waitSignals.push_back(semaphore->semaphore);
        waitStages.push_back(toVkPipelineStages(wait.stages));
        waitValues.push_back(wait.value);
    }

    for (const RHIQueueSignalDesc& signal : desc.signals) {
        const Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, signal.signal);
        if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
            if (errorMessage != nullptr) *errorMessage = "RHIQueueSubmitDesc 包含无效 signal semaphore";
            return false;
        }
        usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == RHIGPUWaitGPUSignalType::Timeline;
        signalSemaphores.push_back(semaphore->semaphore);
        signalValues.push_back(signal.value);
    }

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.waitSemaphoreValueCount = static_cast<u32>(waitValues.size());
    timelineInfo.pWaitSemaphoreValues = waitValues.data();
    timelineInfo.signalSemaphoreValueCount = static_cast<u32>(signalValues.size());
    timelineInfo.pSignalSemaphoreValues = signalValues.data();

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = usesTimelineSemaphore ? &timelineInfo : nullptr;
    submitInfo.waitSemaphoreCount = static_cast<u32>(waitSignals.size());
    submitInfo.pWaitSemaphores = waitSignals.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.signalSemaphoreCount = static_cast<u32>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    const Impl::CPUWaitGPUSignalResource* fence = getRenderResource(impl_->cpuWaitGPUSignals, desc.cpuWaitGPUSignal);
    VkQueue queue = impl_->queueForType(desc.queue);
    if (queue == VK_NULL_HANDLE) {
        if (errorMessage != nullptr) *errorMessage = "RHIQueueSubmitDesc 请求的队列类型当前设备不支持";
        return false;
    }

    const VkResult result = vkQueueSubmit(
        queue,
        1,
        &submitInfo,
        fence != nullptr ? fence->fence : VK_NULL_HANDLE);

    if (result != VK_SUCCESS) {
        if (errorMessage != nullptr) *errorMessage = "vkQueueSubmit 失败";
        return false;
    }
    return true;
}

// present 把 acquire 得到并渲染完成的 swapchain image 交还给窗口系统。
// Vulkan 要求 present 等待 render-finished semaphore，确保图像写入完成后才显示。
bool RHIVulkan::present(const RHIPresentDesc& desc, std::string* errorMessage) {
    const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, desc.swapchain);
    if (swapchain == nullptr || swapchain->swapchain == VK_NULL_HANDLE) {
        if (errorMessage != nullptr) *errorMessage = "RHIPresentDesc::swapchain 无效";
        return false;
    }

    std::vector<VkSemaphore> waitSignals;
    waitSignals.reserve(desc.waitSignals.size());
    for (RHIGPUWaitGPUSignal handle : desc.waitSignals) {
        const Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, handle);
        if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
            if (errorMessage != nullptr) *errorMessage = "RHIPresentDesc 包含无效 wait semaphore";
            return false;
        }
        waitSignals.push_back(semaphore->semaphore);
    }

    VkSwapchainKHR vkSwapchain = swapchain->swapchain;
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = static_cast<u32>(waitSignals.size());
    presentInfo.pWaitSemaphores = waitSignals.data();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vkSwapchain;
    presentInfo.pImageIndices = &desc.imageIndex;

    const VkResult result = vkQueuePresentKHR(impl_->native.presentQueue, &presentInfo);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        if (errorMessage != nullptr) *errorMessage = "vkQueuePresentKHR 失败";
        return false;
    }
    return true;
}

// recordAndSubmitFrame 是把 RHIFramePacket 真正落成 Vulkan 命令的地方。
// 当前实现为学习和示例优先：每帧分配一个一次性 command buffer，处理上传、资源状态转换、
// dynamic rendering begin/end、pipeline/descriptor/buffer 绑定和 draw，然后提交并等待 fence。

} // namespace rhi





