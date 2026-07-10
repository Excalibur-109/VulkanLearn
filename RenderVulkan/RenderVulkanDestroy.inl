void VulkanRenderer::destroy(BufferHandle handle) noexcept {
    Impl::BufferResource* resource = getRenderResource(impl_->buffers, handle);
    if (resource == nullptr) return;
    if (resource->mapped != nullptr) {
        vkUnmapMemory(impl_->native.device, resource->memory);
        resource->mapped = nullptr;
    }
    if (resource->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(impl_->native.device, resource->buffer, nullptr);
        resource->buffer = VK_NULL_HANDLE;
    }
    if (resource->memory != VK_NULL_HANDLE) {
        vkFreeMemory(impl_->native.device, resource->memory, nullptr);
        resource->memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(TextureHandle handle) noexcept {
    Impl::TextureResource* resource = getRenderResource(impl_->textures, handle);
    if (resource == nullptr) return;
    if (resource->image != VK_NULL_HANDLE && resource->ownsImage) {
        vkDestroyImage(impl_->native.device, resource->image, nullptr);
    }
    resource->image = VK_NULL_HANDLE;
    if (resource->memory != VK_NULL_HANDLE) {
        vkFreeMemory(impl_->native.device, resource->memory, nullptr);
        resource->memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(TextureViewHandle handle) noexcept {
    Impl::TextureViewResource* resource = getRenderResource(impl_->textureViews, handle);
    if (resource != nullptr && resource->view != VK_NULL_HANDLE) {
        vkDestroyImageView(impl_->native.device, resource->view, nullptr);
        resource->view = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(SamplerHandle handle) noexcept {
    Impl::SamplerResource* resource = getRenderResource(impl_->samplers, handle);
    if (resource != nullptr && resource->sampler != VK_NULL_HANDLE) {
        vkDestroySampler(impl_->native.device, resource->sampler, nullptr);
        resource->sampler = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(ShaderHandle handle) noexcept {
    Impl::ShaderResource* resource = getRenderResource(impl_->shaders, handle);
    if (resource != nullptr && resource->module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(impl_->native.device, resource->module, nullptr);
        resource->module = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(BindGroupLayoutHandle handle) noexcept {
    Impl::BindGroupLayoutResource* resource = getRenderResource(impl_->bindGroupLayouts, handle);
    if (resource != nullptr && resource->layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(impl_->native.device, resource->layout, nullptr);
        resource->layout = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(BindGroupHandle handle) noexcept {
    Impl::BindGroupResource* resource = getRenderResource(impl_->bindGroups, handle);
    if (resource != nullptr && resource->set != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(impl_->native.device, impl_->descriptorPool, 1, &resource->set);
        resource->set = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(PipelineLayoutHandle handle) noexcept {
    Impl::PipelineLayoutResource* resource = getRenderResource(impl_->pipelineLayouts, handle);
    if (resource != nullptr && resource->layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(impl_->native.device, resource->layout, nullptr);
        resource->layout = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(PipelineCacheHandle handle) noexcept {
    Impl::PipelineCacheResource* resource = getRenderResource(impl_->pipelineCaches, handle);
    if (resource != nullptr && resource->cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(impl_->native.device, resource->cache, nullptr);
        resource->cache = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(PipelineHandle handle) noexcept {
    Impl::PipelineResource* resource = getRenderResource(impl_->pipelines, handle);
    if (resource != nullptr && resource->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(impl_->native.device, resource->pipeline, nullptr);
        resource->pipeline = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(QueryPoolHandle handle) noexcept {
    Impl::QueryPoolResource* resource = getRenderResource(impl_->queryPools, handle);
    if (resource != nullptr && resource->pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(impl_->native.device, resource->pool, nullptr);
        resource->pool = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(SemaphoreHandle handle) noexcept {
    Impl::SemaphoreResource* resource = getRenderResource(impl_->semaphores, handle);
    if (resource != nullptr && resource->semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(impl_->native.device, resource->semaphore, nullptr);
        resource->semaphore = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(FenceHandle handle) noexcept {
    Impl::FenceResource* resource = getRenderResource(impl_->fences, handle);
    if (resource != nullptr && resource->fence != VK_NULL_HANDLE) {
        vkDestroyFence(impl_->native.device, resource->fence, nullptr);
        resource->fence = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(SwapchainHandle handle) noexcept {
    Impl::SwapchainResource* resource = getRenderResource(impl_->swapchains, handle);
    if (resource == nullptr) return;

    for (TextureViewHandle view : resource->imageViews) {
        destroy(view);
    }
    resource->imageViews.clear();

    for (TextureHandle image : resource->images) {
        destroy(image);
    }
    resource->images.clear();

    if (resource->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(impl_->native.device, resource->swapchain, nullptr);
        resource->swapchain = VK_NULL_HANDLE;
    }
}
