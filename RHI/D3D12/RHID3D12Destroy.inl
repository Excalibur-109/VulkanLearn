#pragma once

#include "RHID3D12Private.inl"

namespace rhi {

void RHID3D12::destroy(RHIBuffer handle) noexcept {
    if (Impl::BufferResource* resource = getRenderResource(impl_->buffers, handle)) {
        if (resource->resource && resource->mappedData != nullptr) {
            resource->resource->Unmap(0, nullptr);
            resource->mappedData = nullptr;
        }
        resource->resource.Reset();
        resource->currentState = D3D12_RESOURCE_STATE_COMMON;
    }
}

void RHID3D12::destroy(RHITexture handle) noexcept {
    if (Impl::TextureResource* resource = getRenderResource(impl_->textures, handle)) {
        resource->resource.Reset();
        resource->currentState = D3D12_RESOURCE_STATE_COMMON;
        resource->swapchainImage = false;
    }
}

void RHID3D12::destroy(RHITextureView handle) noexcept {
    if (Impl::TextureViewResource* resource = getRenderResource(impl_->textureViews, handle)) {
        resource->srv = {};
        resource->rtv = {};
        resource->dsv = {};
        resource->uav = {};
        resource->desc = {};
    }
}

void RHID3D12::destroy(RHISampler handle) noexcept {
    if (Impl::SamplerResource* resource = getRenderResource(impl_->samplers, handle)) {
        resource->sampler = {};
        resource->desc = {};
    }
}

void RHID3D12::destroy(RHIShader handle) noexcept {
    if (Impl::ShaderResource* resource = getRenderResource(impl_->shaders, handle)) {
        resource->bytecode.clear();
        resource->desc = {};
    }
}

void RHID3D12::destroy(RHIBindGroupLayout handle) noexcept {
    if (Impl::BindGroupLayoutResource* resource = getRenderResource(impl_->bindGroupLayouts, handle)) {
        resource->desc = {};
    }
}

void RHID3D12::destroy(RHIBindGroup handle) noexcept {
    if (Impl::BindGroupResource* resource = getRenderResource(impl_->bindGroups, handle)) {
        resource->bindings.clear();
        resource->desc = {};
    }
}

void RHID3D12::destroy(RHIPipelineLayout handle) noexcept {
    if (Impl::PipelineLayoutResource* resource = getRenderResource(impl_->pipelineLayouts, handle)) {
        resource->rootSignature.Reset();
        resource->desc = {};
    }
}

void RHID3D12::destroy(RHIPipelineCache handle) noexcept {
    if (Impl::PipelineCacheResource* resource = getRenderResource(impl_->pipelineCaches, handle)) {
        resource->desc = {};
    }
}

void RHID3D12::destroy(RHIPipeline handle) noexcept {
    if (Impl::PipelineResource* resource = getRenderResource(impl_->pipelines, handle)) {
        resource->pipelineState.Reset();
        resource->rootSignature.Reset();
        resource->compute = false;
    }
}

void RHID3D12::destroy(RHIQueryPool handle) noexcept {
    if (Impl::QueryPoolResource* resource = getRenderResource(impl_->queryPools, handle)) {
        resource->heap.Reset();
        resource->desc = {};
    }
}

void RHID3D12::destroy(RHIGPUWaitGPUSignal handle) noexcept {
    if (Impl::GPUWaitGPUSignalResource* resource = getRenderResource(impl_->gpuWaitGPUSignals, handle)) {
        resource->desc = {};
        resource->value = 0;
        resource->signaled = false;
    }
}

void RHID3D12::destroy(RHICPUWaitGPUSignal handle) noexcept {
    if (Impl::CPUWaitGPUSignalResource* resource = getRenderResource(impl_->cpuWaitGPUSignals, handle)) {
        resource->fence.Reset();
        if (resource->eventHandle != nullptr) {
            CloseHandle(resource->eventHandle);
            resource->eventHandle = nullptr;
        }
        resource->desc = {};
        resource->value = 0;
        resource->signaled = false;
    }
}

void RHID3D12::destroy(RHISwapchain handle) noexcept {
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
        resource->currentImage = 0;
    }
}

// D3D12 destroy 片段只释放资源，不回收 descriptor heap 槽位。
// 这是当前后端的简单线性分配策略：句柄槽位保持稳定，descriptor 槽位在 renderer 生命周期内单调增长。
// 后续如果做长期运行编辑器，需要再补 descriptor free-list 或按帧 ring allocator。

} // namespace rhi



