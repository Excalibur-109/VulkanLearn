#pragma once

#include "RHID3D11Private.inl"

namespace rhi {

void RHID3D11::Destroy(RHIBuffer handle) noexcept {
    if (Impl::BufferResource* resource = getRenderResource(impl_->buffers, handle)) {
        resource->buffer.Reset();
    }
}

void RHID3D11::Destroy(RHITexture handle) noexcept {
    if (Impl::TextureResource* resource = getRenderResource(impl_->textures, handle)) {
        resource->resource.Reset();
    }
}

void RHID3D11::Destroy(RHITextureView handle) noexcept {
    if (Impl::TextureViewResource* resource = getRenderResource(impl_->textureViews, handle)) {
        resource->srv.Reset();
        resource->rtv.Reset();
        resource->dsv.Reset();
        resource->uav.Reset();
    }
}

void RHID3D11::Destroy(RHISampler handle) noexcept {
    if (Impl::SamplerResource* resource = getRenderResource(impl_->samplers, handle)) {
        resource->sampler.Reset();
    }
}

void RHID3D11::Destroy(RHIShader handle) noexcept {
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

void RHID3D11::Destroy(RHIBindSetLayout handle) noexcept {
    if (Impl::BindSetLayoutResource* resource = getRenderResource(impl_->bindSetLayouts, handle)) {
        resource->desc = {};
    }
}

void RHID3D11::Destroy(RHIBindSet handle) noexcept {
    if (Impl::BindSetResource* resource = getRenderResource(impl_->bindSets, handle)) {
        resource->bindings.clear();
        resource->desc = {};
    }
}

void RHID3D11::Destroy(RHIPipelineLayout handle) noexcept {
    if (Impl::PipelineLayoutResource* resource = getRenderResource(impl_->pipelineLayouts, handle)) {
        resource->desc = {};
    }
}

void RHID3D11::Destroy(RHIPipelineCache handle) noexcept {
    if (Impl::PipelineCacheResource* resource = getRenderResource(impl_->pipelineCaches, handle)) {
        resource->desc = {};
    }
}

void RHID3D11::Destroy(RHIPipeline handle) noexcept {
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

void RHID3D11::Destroy(RHIQueryPool handle) noexcept {
    if (Impl::QueryPoolResource* resource = getRenderResource(impl_->queryPools, handle)) {
        resource->queries.clear();
        resource->desc = {};
    }
}

void RHID3D11::Destroy(RHIGPUWaitGPUSignal handle) noexcept {
    if (Impl::GPUWaitGPUSignalResource* resource = getRenderResource(impl_->gpuWaitGPUSignals, handle)) {
        resource->desc = {};
        resource->value = 0;
        resource->signaled = false;
    }
}

void RHID3D11::Destroy(RHICPUWaitGPUSignal handle) noexcept {
    if (Impl::CPUWaitGPUSignalResource* resource = getRenderResource(impl_->cpuWaitGPUSignals, handle)) {
        resource->eventQuery.Reset();
        resource->desc = {};
        resource->signaled = false;
    }
}

void RHID3D11::Destroy(RHISwapchain handle) noexcept {
    if (Impl::SwapchainResource* resource = getRenderResource(impl_->swapchains, handle)) {
        for (RHITextureView view : resource->imageViews) {
            Destroy(view);
        }
        for (RHITexture image : resource->images) {
            Destroy(image);
        }
        resource->imageViews.clear();
        resource->images.clear();
        resource->swapchain.Reset();
        resource->desc = {};
        resource->format = RHIFormat::Undefined;
        resource->extent = {};
    }
}
// D3D11 Destroy 片段集中释放统一句柄对应的 COM 对象。
// 大多数资源只需要 ComPtr::Reset；真正要注意的是依赖方向：
// view 引用 texture，pipeline 引用 shader/state，swapchain 引用 back buffer。
// Shutdown 会按反向依赖顺序调用这些 Destroy，避免 context 或 view 还持有底层资源。

} // namespace rhi









