#pragma once

#include "RHIVulkanPrivate.inl"

namespace rhi {

void RHIVulkan::Destroy(RHIBuffer handle) noexcept {
    Impl::BufferResource* resource = getRenderResource(impl_->buffers, handle);
    if (resource == nullptr) return;
    if (resource->mapped != nullptr) {
        vkUnmapMemory(impl_->native.device, resource->memory);
        resource->mapped = nullptr;
    }
    const VkDevice device = impl_->native.device;
    const VkBuffer buffer = resource->buffer;
    const VkDeviceMemory memory = resource->memory;
    resource->buffer = VK_NULL_HANDLE;
    resource->memory = VK_NULL_HANDLE;
    if (buffer == VK_NULL_HANDLE && memory == VK_NULL_HANDLE) return;
    impl_->deferRelease([device, buffer, memory]() noexcept {
        if (buffer != VK_NULL_HANDLE) vkDestroyBuffer(device, buffer, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    });
}

void RHIVulkan::Destroy(RHITexture handle) noexcept {
    Impl::TextureResource* resource = getRenderResource(impl_->textures, handle);
    if (resource == nullptr) return;
    const VkDevice device = impl_->native.device;
    const VkImage image = resource->image;
    const VkDeviceMemory memory = resource->memory;
    const bool ownsImage = resource->ownsImage;
    resource->image = VK_NULL_HANDLE;
    resource->memory = VK_NULL_HANDLE;
    if (image == VK_NULL_HANDLE && memory == VK_NULL_HANDLE) return;
    impl_->deferRelease([device, image, memory, ownsImage]() noexcept {
        if (image != VK_NULL_HANDLE && ownsImage) vkDestroyImage(device, image, nullptr);
        if (memory != VK_NULL_HANDLE) vkFreeMemory(device, memory, nullptr);
    });
}

void RHIVulkan::Destroy(RHITextureView handle) noexcept {
    Impl::TextureViewResource* resource = getRenderResource(impl_->textureViews, handle);
    if (resource == nullptr || resource->view == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkImageView view = resource->view;
    resource->view = VK_NULL_HANDLE;
    impl_->deferRelease([device, view]() noexcept { vkDestroyImageView(device, view, nullptr); });
}

void RHIVulkan::Destroy(RHISampler handle) noexcept {
    Impl::SamplerResource* resource = getRenderResource(impl_->samplers, handle);
    if (resource == nullptr || resource->sampler == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkSampler sampler = resource->sampler;
    resource->sampler = VK_NULL_HANDLE;
    impl_->deferRelease([device, sampler]() noexcept { vkDestroySampler(device, sampler, nullptr); });
}

void RHIVulkan::Destroy(RHIShader handle) noexcept {
    Impl::ShaderResource* resource = getRenderResource(impl_->shaders, handle);
    if (resource == nullptr || resource->module == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkShaderModule module = resource->module;
    resource->module = VK_NULL_HANDLE;
    impl_->deferRelease([device, module]() noexcept { vkDestroyShaderModule(device, module, nullptr); });
}

void RHIVulkan::Destroy(RHIBindSetLayout handle) noexcept {
    Impl::BindSetLayoutResource* resource = getRenderResource(impl_->bindSetLayouts, handle);
    if (resource == nullptr || resource->layout == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkDescriptorSetLayout layout = resource->layout;
    resource->layout = VK_NULL_HANDLE;
    impl_->deferRelease([device, layout]() noexcept {
        vkDestroyDescriptorSetLayout(device, layout, nullptr);
    });
}

void RHIVulkan::Destroy(RHIBindSet handle) noexcept {
    Impl::BindSetResource* resource = getRenderResource(impl_->bindSets, handle);
    if (resource == nullptr || resource->set == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkDescriptorPool descriptorPool = impl_->descriptorPool;
    const VkDescriptorSet set = resource->set;
    resource->set = VK_NULL_HANDLE;
    impl_->deferRelease([device, descriptorPool, set]() noexcept {
        vkFreeDescriptorSets(device, descriptorPool, 1, &set);
    });
}

void RHIVulkan::Destroy(RHIPipelineLayout handle) noexcept {
    Impl::PipelineLayoutResource* resource = getRenderResource(impl_->pipelineLayouts, handle);
    if (resource == nullptr || resource->layout == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkPipelineLayout layout = resource->layout;
    resource->layout = VK_NULL_HANDLE;
    impl_->deferRelease([device, layout]() noexcept { vkDestroyPipelineLayout(device, layout, nullptr); });
}

void RHIVulkan::Destroy(RHIPipelineCache handle) noexcept {
    Impl::PipelineCacheResource* resource = getRenderResource(impl_->pipelineCaches, handle);
    if (resource == nullptr || resource->cache == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkPipelineCache cache = resource->cache;
    resource->cache = VK_NULL_HANDLE;
    impl_->deferRelease([device, cache]() noexcept { vkDestroyPipelineCache(device, cache, nullptr); });
}

void RHIVulkan::Destroy(RHIPipeline handle) noexcept {
    Impl::PipelineResource* resource = getRenderResource(impl_->pipelines, handle);
    if (resource == nullptr || resource->pipeline == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkPipeline pipeline = resource->pipeline;
    resource->pipeline = VK_NULL_HANDLE;
    impl_->deferRelease([device, pipeline]() noexcept { vkDestroyPipeline(device, pipeline, nullptr); });
}

void RHIVulkan::Destroy(RHIQueryPool handle) noexcept {
    Impl::QueryPoolResource* resource = getRenderResource(impl_->queryPools, handle);
    if (resource == nullptr || resource->pool == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkQueryPool pool = resource->pool;
    resource->pool = VK_NULL_HANDLE;
    impl_->deferRelease([device, pool]() noexcept { vkDestroyQueryPool(device, pool, nullptr); });
}

void RHIVulkan::Destroy(RHIGPUWaitGPUSignal handle) noexcept {
    Impl::GPUWaitGPUSignalResource* resource = getRenderResource(impl_->gpuWaitGPUSignals, handle);
    if (resource == nullptr || resource->semaphore == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkSemaphore semaphore = resource->semaphore;
    resource->semaphore = VK_NULL_HANDLE;
    impl_->deferRelease(
        [device, semaphore]() noexcept { vkDestroySemaphore(device, semaphore, nullptr); },
        true);
}

void RHIVulkan::Destroy(RHICPUWaitGPUSignal handle) noexcept {
    Impl::CPUWaitGPUSignalResource* resource = getRenderResource(impl_->cpuWaitGPUSignals, handle);
    if (resource == nullptr || resource->fence == VK_NULL_HANDLE) return;
    const VkDevice device = impl_->native.device;
    const VkFence fence = resource->fence;
    resource->fence = VK_NULL_HANDLE;
    impl_->deferRelease(
        [device, fence]() noexcept { vkDestroyFence(device, fence, nullptr); },
        true);
}

void RHIVulkan::Destroy(RHISwapchain handle) noexcept {
    Impl::SwapchainResource* resource = getRenderResource(impl_->swapchains, handle);
    if (resource == nullptr) return;

    for (RHITextureView view : resource->imageViews) {
        Destroy(view);
    }
    resource->imageViews.clear();

    for (RHITexture image : resource->images) {
        Destroy(image);
    }
    resource->images.clear();

    if (resource->swapchain != VK_NULL_HANDLE) {
        const VkDevice device = impl_->native.device;
        const VkSwapchainKHR swapchain = resource->swapchain;
        resource->swapchain = VK_NULL_HANDLE;
        impl_->deferRelease(
            [device, swapchain]() noexcept {
                vkDestroySwapchainKHR(device, swapchain, nullptr);
            },
            true);
    }
}

} // namespace rhi









