#pragma once

#include "RHIVulkanPrivate.inl"

namespace rhi {

RHIQueryPool RHIVulkan::CreateQueryPool(const RHIQueryPoolDesc& desc) {
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

// RHI 同时暴露 Binary/Timeline GPU signal：
// - 普通 queue 之间优先用 Timeline，一个对象配合递增 value 表达多次依赖；
// - acquire/present 属于 WSI 边界，仍必须使用 Binary；
// - CPUWaitGPUSignal 映射为 VkFence，供显式低层提交使用。
RHIGPUWaitGPUSignal RHIVulkan::CreateGPUWaitGPUSignal(const RHIGPUWaitGPUSignalDesc& desc) {
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

RHICPUWaitGPUSignal RHIVulkan::CreateCPUWaitGPUSignal(const RHICPUWaitGPUSignalDesc& desc) {
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
RHISwapchain RHIVulkan::CreateSwapchain(const RHISwapchainDesc& desc) {
    if (impl_->native.surface == VK_NULL_HANDLE) {
        throw std::runtime_error("CreateSwapchain requires a valid VkSurfaceKHR during initialization");
    }

    const VulkanSwapchainSupport support = querySwapchainSupport(impl_->native.physicalDevice, impl_->native.surface);
    if (!support.isUsable()) {
        throw std::runtime_error("The Vulkan surface does not provide a usable swapchain format and Present mode");
    }

    // VkSwapchainCreateInfoKHR::imageUsage 不能随意填写，必须是 surface Capabilities
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
        // 刚从 swapchain 取得的 image 在第一次使用前没有可依赖的旧布局。
        // 首次 barrier 必须从 UNDEFINED 转换；完成一次 present 后状态跟踪才会变为 Present。
        texture.currentState = RHIResourceState::Undefined;
        texture.currentStages = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        texture.currentAccess = 0;
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
        resource.imageViews.push_back(CreateTextureView(viewDesc));
    }

    const RHISwapchain handle = makeRenderHandle<RHISwapchain>(impl_->swapchains, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<u64>(impl_->swapchains.back().swapchain), desc.debugName);
    return handle;
}

std::vector<RHITexture> RHIVulkan::GetSwapchainImages(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->images;
    }
    return {};
}

std::vector<RHITextureView> RHIVulkan::GetSwapchainImageViews(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->imageViews;
    }
    return {};
}

RHIFormat RHIVulkan::GetSwapchainFormat(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return fromVkFormat(swapchain->format);
    }
    return RHIFormat::Undefined;
}

RHIExtent2D RHIVulkan::GetSwapchainExtent(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return {swapchain->extent.width, swapchain->extent.height};
    }
    return {};
}

bool RHIVulkan::AcquireNextImage(
    RHISwapchain swapchainHandle,
    RHIGPUWaitGPUSignal gpuWaitGPUSignal,
    RHICPUWaitGPUSignal cpuWaitGPUSignal,
    u32* imageIndex,
    std::string* errorMessage) {
    // acquire 只是向 swapchain 取下一张可写图像的索引，并可选 signal semaphore/fence。
    // 注意：传给 vkAcquireNextImageKHR 的 semaphore 必须是 Binary，不能传 Timeline。
    // 真正把图像从 Present 转到 ColorAttachment 的 layout transition 在 frame 录制阶段完成。
    const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, swapchainHandle);
    const Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, gpuWaitGPUSignal);
    const Impl::CPUWaitGPUSignalResource* fence = getRenderResource(impl_->cpuWaitGPUSignals, cpuWaitGPUSignal);
    if (swapchain == nullptr || swapchain->swapchain == VK_NULL_HANDLE || imageIndex == nullptr) {
        if (errorMessage != nullptr) *errorMessage = "AcquireNextImage 参数无效";
        return false;
    }
    if (semaphore != nullptr && semaphore->desc.type != RHIGPUWaitGPUSignalType::Binary) {
        if (errorMessage != nullptr) {
            *errorMessage = "AcquireNextImage requires a Binary GPU signal; WSI cannot signal a Timeline semaphore";
        }
        return false;
    }

    // Acquire 会 signal image-available binary signal。复用该 signal 前，必须先确认
    // 使用同一帧槽位的上一次 GPU 提交已经完成，因此帧等待必须发生在 acquire 之前。
    try {
        impl_->prepareNextFrameContext();
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
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

// 低层 Submit：把外部已经准备好的同步关系提交到指定 queue。
// 这个函数不录制命令，只把 wait/signal semaphore、timeline value 和 fence 翻译成 VkSubmitInfo。
bool RHIVulkan::Submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) {
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

    VkSubmitInfo SubmitInfo{};
    SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    SubmitInfo.pNext = usesTimelineSemaphore ? &timelineInfo : nullptr;
    SubmitInfo.waitSemaphoreCount = static_cast<u32>(waitSignals.size());
    SubmitInfo.pWaitSemaphores = waitSignals.data();
    SubmitInfo.pWaitDstStageMask = waitStages.data();
    SubmitInfo.signalSemaphoreCount = static_cast<u32>(signalSemaphores.size());
    SubmitInfo.pSignalSemaphores = signalSemaphores.data();

    const Impl::CPUWaitGPUSignalResource* fence = getRenderResource(impl_->cpuWaitGPUSignals, desc.cpuWaitGPUSignal);
    VkQueue queue = impl_->queueForType(desc.queue);
    if (queue == VK_NULL_HANDLE) {
        if (errorMessage != nullptr) *errorMessage = "RHIQueueSubmitDesc 请求的队列类型当前设备不支持";
        return false;
    }

    const VkResult result = vkQueueSubmit(
        queue,
        1,
        &SubmitInfo,
        fence != nullptr ? fence->fence : VK_NULL_HANDLE);

    if (result != VK_SUCCESS) {
        if (errorMessage != nullptr) *errorMessage = "vkQueueSubmit 失败";
        return false;
    }
    // 低层 Submit 不使用 FrameContext 内部 fence，资源管理器无法仅凭 frame serial
    // 判断它何时完成。后续 Destroy 会选择 device-idle 的安全退化路径。
    impl_->hasUntrackedSubmissions = true;
    impl_->deviceKnownIdle = false;
    return true;
}

// Present 把 acquire 得到并渲染完成的 swapchain image 交还给窗口系统。
// Vulkan 要求 Present 等待 Binary render-finished semaphore，确保图像写入完成后才显示。
bool RHIVulkan::Present(const RHIPresentDesc& desc, std::string* errorMessage) {
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
        if (semaphore->desc.type != RHIGPUWaitGPUSignalType::Binary) {
            if (errorMessage != nullptr) {
                *errorMessage = "Present requires Binary GPU wait signals on the Vulkan WSI path";
            }
            return false;
        }
        waitSignals.push_back(semaphore->semaphore);
    }

    VkSwapchainKHR vkSwapchain = swapchain->swapchain;
    VkPresentInfoKHR PresentInfo{};
    PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    PresentInfo.waitSemaphoreCount = static_cast<u32>(waitSignals.size());
    PresentInfo.pWaitSemaphores = waitSignals.data();
    PresentInfo.swapchainCount = 1;
    PresentInfo.pSwapchains = &vkSwapchain;
    PresentInfo.pImageIndices = &desc.imageIndex;

    const VkResult result = vkQueuePresentKHR(impl_->native.presentQueue, &PresentInfo);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        if (errorMessage != nullptr) *errorMessage = "vkQueuePresentKHR 失败";
        return false;
    }
    impl_->deviceKnownIdle = false;
    return true;
}

// RecordAndSubmitFrame 是把 RHIFramePacket 真正落成 Vulkan 命令的地方。
// CommandBuffer/Fence 由 FrameContext 按 framesInFlight 轮转复用；CPU 只等待即将复用的
// 帧槽位，而不是每次提交后立刻等待整帧完成。

} // namespace rhi










