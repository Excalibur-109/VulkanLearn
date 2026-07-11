void RHID3D11Backend::destroy(RHIBuffer handle) noexcept {
    if (Impl::BufferResource* resource = getRenderResource(impl_->buffers, handle)) {
        resource->buffer.Reset();
    }
}

void RHID3D11Backend::destroy(RHITexture handle) noexcept {
    if (Impl::TextureResource* resource = getRenderResource(impl_->textures, handle)) {
        resource->resource.Reset();
    }
}

void RHID3D11Backend::destroy(RHITextureView handle) noexcept {
    if (Impl::TextureViewResource* resource = getRenderResource(impl_->textureViews, handle)) {
        resource->srv.Reset();
        resource->rtv.Reset();
        resource->dsv.Reset();
        resource->uav.Reset();
    }
}

void RHID3D11Backend::destroy(RHISampler handle) noexcept {
    if (Impl::SamplerResource* resource = getRenderResource(impl_->samplers, handle)) {
        resource->sampler.Reset();
    }
}

void RHID3D11Backend::destroy(RHIShader handle) noexcept {
    if (Impl::ShaderResource* resource = getRenderResource(impl_->shaders, handle)) {
        resource->vertexShader.Reset();
        resource->hullShader.Reset();
        resource->domainShader.Reset();
        resource->geometryShader.Reset();
        resource->pixelShader.Reset();
        resource->computeShader.Reset();
        resource->bytecode.clear();
    }
}

void RHID3D11Backend::destroy(RHIBindGroupLayout handle) noexcept {
    if (Impl::BindGroupLayoutResource* resource = getRenderResource(impl_->bindGroupLayouts, handle)) {
        resource->desc = {};
    }
}

void RHID3D11Backend::destroy(RHIBindGroup handle) noexcept {
    if (Impl::BindGroupResource* resource = getRenderResource(impl_->bindGroups, handle)) {
        resource->bindings.clear();
        resource->desc = {};
    }
}

void RHID3D11Backend::destroy(RHIPipelineLayout handle) noexcept {
    if (Impl::PipelineLayoutResource* resource = getRenderResource(impl_->pipelineLayouts, handle)) {
        resource->desc = {};
    }
}

void RHID3D11Backend::destroy(RHIPipelineCache handle) noexcept {
    if (Impl::PipelineCacheResource* resource = getRenderResource(impl_->pipelineCaches, handle)) {
        resource->desc = {};
    }
}

void RHID3D11Backend::destroy(RHIPipeline handle) noexcept {
    if (Impl::PipelineResource* resource = getRenderResource(impl_->pipelines, handle)) {
        resource->inputLayout.Reset();
        resource->vertexShader.Reset();
        resource->hullShader.Reset();
        resource->domainShader.Reset();
        resource->geometryShader.Reset();
        resource->pixelShader.Reset();
        resource->computeShader.Reset();
        resource->rasterizerState.Reset();
        resource->depthStencilState.Reset();
        resource->blendState.Reset();
    }
}

void RHID3D11Backend::destroy(RHIQueryPool handle) noexcept {
    if (Impl::QueryPoolResource* resource = getRenderResource(impl_->queryPools, handle)) {
        resource->queries.clear();
        resource->desc = {};
    }
}

void RHID3D11Backend::destroy(RHISemaphore handle) noexcept {
    if (Impl::SemaphoreResource* resource = getRenderResource(impl_->semaphores, handle)) {
        resource->desc = {};
        resource->value = 0;
        resource->signaled = false;
    }
}

void RHID3D11Backend::destroy(RHIFence handle) noexcept {
    if (Impl::FenceResource* resource = getRenderResource(impl_->fences, handle)) {
        resource->eventQuery.Reset();
        resource->desc = {};
        resource->signaled = false;
    }
}

void RHID3D11Backend::destroy(RHISwapchain handle) noexcept {
    if (Impl::SwapchainResource* resource = getRenderResource(impl_->swapchains, handle)) {
        for (RHITextureView view : resource->imageViews) {
            destroy(view);
        }
        for (RHITexture image : resource->images) {
            destroy(image);
        }
        resource->imageViews.clear();
        resource->images.clear();
        resource->swapchain.Reset();
        resource->desc = {};
        resource->format = RHIFormat::Undefined;
        resource->extent = {};
    }
}
// D3D11 destroy 片段集中释放统一句柄对应的 COM 对象。
// 大多数资源只需要 ComPtr::Reset；真正要注意的是依赖方向：
// view 引用 texture，pipeline 引用 shader/state，swapchain 引用 back buffer。
// shutdown 会按反向依赖顺序调用这些 destroy，避免 context 或 view 还持有底层资源。
