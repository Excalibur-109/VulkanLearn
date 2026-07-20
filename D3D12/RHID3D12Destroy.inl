#pragma once

#include "RHID3D12Private.inl"

namespace rhi {

void RHID3D12::Destroy(RHIBuffer handle) noexcept {
    if (Impl::BufferResource* resource = getRenderResource(impl_->buffers, handle)) {
        if (resource->resource && resource->mappedData != nullptr) {
            resource->resource->Unmap(0, nullptr);
            resource->mappedData = nullptr;
        }
        resource->resource.Reset();
        resource->currentState = D3D12_RESOURCE_STATE_COMMON;
    }
}

void RHID3D12::Destroy(RHITexture handle) noexcept {
    if (Impl::TextureResource* resource = getRenderResource(impl_->textures, handle)) {
        resource->resource.Reset();
        resource->currentState = D3D12_RESOURCE_STATE_COMMON;
        resource->swapchainImage = false;
    }
}

void RHID3D12::Destroy(RHITextureView handle) noexcept {
    if (Impl::TextureViewResource* resource = getRenderResource(impl_->textureViews, handle)) {
        impl_->releaseDescriptor(resource->srv);
        impl_->releaseDescriptor(resource->rtv);
        impl_->releaseDescriptor(resource->dsv);
        impl_->releaseDescriptor(resource->uav);
        resource->desc = {};
    }
}

void RHID3D12::Destroy(RHISampler handle) noexcept {
    if (Impl::SamplerResource* resource = getRenderResource(impl_->samplers, handle)) {
        impl_->releaseDescriptor(resource->sampler);
        resource->desc = {};
    }
}

void RHID3D12::Destroy(RHIShader handle) noexcept {
    if (Impl::ShaderResource* resource = getRenderResource(impl_->shaders, handle)) {
        resource->bytecode.clear();
        resource->desc = {};
    }
}

void RHID3D12::Destroy(RHIBindSetLayout handle) noexcept {
    if (Impl::BindSetLayoutResource* resource = getRenderResource(impl_->bindSetLayouts, handle)) {
        resource->desc = {};
    }
}

void RHID3D12::Destroy(RHIBindSet handle) noexcept {
    if (Impl::BindSetResource* resource = getRenderResource(impl_->bindSets, handle)) {
        for (Impl::ResolvedBinding& binding : resource->bindings) {
            if (binding.ownsResourceDescriptor) {
                impl_->releaseDescriptor(binding.resourceDescriptor);
            }
        }
        resource->bindings.clear();
        resource->desc = {};
    }
}

void RHID3D12::Destroy(RHIPipelineLayout handle) noexcept {
    if (Impl::PipelineLayoutResource* resource = getRenderResource(impl_->pipelineLayouts, handle)) {
        resource->rootSignature.Reset();
        resource->desc = {};
    }
}

void RHID3D12::Destroy(RHIPipelineCache handle) noexcept {
    if (Impl::PipelineCacheResource* resource = getRenderResource(impl_->pipelineCaches, handle)) {
        resource->desc = {};
    }
}

void RHID3D12::Destroy(RHIPipeline handle) noexcept {
    if (Impl::PipelineResource* resource = getRenderResource(impl_->pipelines, handle)) {
        resource->pipelineState.Reset();
        resource->rootSignature.Reset();
        resource->compute = false;
    }
}

void RHID3D12::Destroy(RHIQueryPool handle) noexcept {
    if (Impl::QueryPoolResource* resource = getRenderResource(impl_->queryPools, handle)) {
        resource->heap.Reset();
        resource->desc = {};
    }
}

void RHID3D12::Destroy(RHIGPUWaitGPUSignal handle) noexcept {
    if (Impl::GPUWaitGPUSignalResource* resource = getRenderResource(impl_->gpuWaitGPUSignals, handle)) {
        resource->desc = {};
        resource->value = 0;
        resource->signaled = false;
    }
}

void RHID3D12::Destroy(RHICPUWaitGPUSignal handle) noexcept {
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

void RHID3D12::Destroy(RHISwapchain handle) noexcept {
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
        resource->currentImage = 0;
    }
}

// D3D12 Destroy 片段只释放资源，不回收 descriptor heap 槽位。
// 这是当前后端的简单线性分配策略：句柄槽位保持稳定，descriptor 槽位在 renderer 生命周期内单调增长。
// 后续如果做长期运行编辑器，需要再补 descriptor free-list 或按帧 ring allocator。

} // namespace rhi









