#pragma once

#if defined(__INTELLISENSE__) && !defined(RHI_D3D12_IMPLEMENTATION_ASSEMBLY)
#include "RHID3D12.cpp"

namespace rhi {
#endif

RHIQueryPool RHID3D12::createQueryPool(const RHIQueryPoolDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }

    Impl::QueryPoolResource resource{};
    resource.desc = desc;

    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Count = desc.queryCount;
    switch (desc.type) {
    case RHIQueryType::Timestamp:
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        break;
    case RHIQueryType::Occlusion:
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
        break;
    case RHIQueryType::PipelineStatistics:
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
        break;
    }

    throwIfFailed(impl_->device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&resource.heap)), "CreateQueryHeap failed");
    impl_->setDebugName(resource.heap.Get(), desc.debugName);
    return makeRenderHandle<RHIQueryPool>(impl_->queryPools, std::move(resource));
}

RHISemaphore RHID3D12::createSemaphore(const RHISemaphoreDesc& desc) {
    Impl::SemaphoreResource resource{};
    resource.desc = desc;
    resource.value = desc.initialValue;
    resource.signaled = desc.type == RHISemaphoreType::Timeline && desc.initialValue > 0;
    return makeRenderHandle<RHISemaphore>(impl_->semaphores, std::move(resource));
}

RHIFence RHID3D12::createFence(const RHIFenceDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }

    Impl::FenceResource resource{};
    resource.desc = desc;
    resource.value = desc.signaled ? 1 : 0;
    resource.signaled = desc.signaled;
    throwIfFailed(impl_->device->CreateFence(resource.value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&resource.fence)), "CreateFence failed");
    impl_->setDebugName(resource.fence.Get(), desc.debugName);
    resource.eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (resource.eventHandle == nullptr) {
        throw std::runtime_error("CreateEventW for D3D12 fence failed");
    }
    return makeRenderHandle<RHIFence>(impl_->fences, std::move(resource));
}

RHISwapchain RHID3D12::createSwapchain(const RHISwapchainDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }
    if (impl_->initDesc.surface.hwnd == nullptr) {
        throw std::runtime_error("D3D12 swapchain creation requires RHID3D12SurfaceDesc::hwnd");
    }

    const bool allowTearing = desc.allowTearing && supportsTearing(impl_->factory.Get());
    DXGI_SWAP_CHAIN_DESC1 swapDesc{};
    swapDesc.Width = desc.extent.width;
    swapDesc.Height = desc.extent.height;
    swapDesc.Format = toSwapchainFormat(desc.preferredFormat);
    swapDesc.Stereo = FALSE;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = std::max(2u, desc.imageCount);
    swapDesc.Scaling = DXGI_SCALING_STRETCH;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapDesc.Flags = allowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    Impl::SwapchainResource resource{};
    resource.desc = desc;
    resource.format = fromDxgiFormat(swapDesc.Format);
    resource.extent = desc.extent;

    ComPtr<IDXGISwapChain1> swapchain1;
    throwIfFailed(
        impl_->factory->CreateSwapChainForHwnd(
            impl_->graphicsQueue.Get(),
            impl_->initDesc.surface.hwnd,
            &swapDesc,
            nullptr,
            nullptr,
            &swapchain1),
        "CreateSwapChainForHwnd failed");
    throwIfFailed(swapchain1.As(&resource.swapchain), "Query IDXGISwapChain3 failed");
    impl_->factory->MakeWindowAssociation(impl_->initDesc.surface.hwnd, DXGI_MWA_NO_ALT_ENTER);

    resource.images.reserve(swapDesc.BufferCount);
    resource.imageViews.reserve(swapDesc.BufferCount);
    for (UINT i = 0; i < swapDesc.BufferCount; ++i) {
        ComPtr<ID3D12Resource> backBuffer;
        throwIfFailed(resource.swapchain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)), "IDXGISwapChain::GetBuffer failed");

        RHITextureDesc textureDesc{};
        textureDesc.debugName = desc.debugName + ".BackBuffer" + std::to_string(i);
        textureDesc.dimension = RHITextureDimension::Texture2D;
        textureDesc.extent = {desc.extent.width, desc.extent.height, 1};
        textureDesc.arrayLayers = 1;
        textureDesc.mipLevels = 1;
        textureDesc.format = resource.format == RHIFormat::Undefined ? RHIFormat::BGRA8_UNorm : resource.format;
        textureDesc.samples = RHISampleCount::Count1;
        textureDesc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Present;
        textureDesc.initialState = RHIResourceState::Present;

        Impl::TextureResource textureResource{};
        textureResource.desc = textureDesc;
        textureResource.resource = backBuffer;
        textureResource.currentState = D3D12_RESOURCE_STATE_PRESENT;
        textureResource.swapchainImage = true;
        impl_->setDebugName(textureResource.resource.Get(), textureDesc.debugName);
        RHITexture textureHandle = makeRenderHandle<RHITexture>(impl_->textures, std::move(textureResource));

        RHITextureViewDesc viewDesc{};
        viewDesc.debugName = desc.debugName + ".BackBufferView" + std::to_string(i);
        viewDesc.texture = textureHandle;
        viewDesc.dimension = RHITextureViewDimension::View2D;
        viewDesc.format = textureDesc.format;
        viewDesc.aspect = RHITextureAspect::Color;

        Impl::TextureViewResource viewResource{};
        viewResource.desc = viewDesc;
        viewResource.rtv = impl_->allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        impl_->device->CreateRenderTargetView(backBuffer.Get(), nullptr, viewResource.rtv.handle);
        RHITextureView viewHandle = makeRenderHandle<RHITextureView>(impl_->textureViews, std::move(viewResource));

        resource.images.push_back(textureHandle);
        resource.imageViews.push_back(viewHandle);
    }

    resource.currentImage = resource.swapchain->GetCurrentBackBufferIndex();
    return makeRenderHandle<RHISwapchain>(impl_->swapchains, std::move(resource));
}

std::vector<RHITexture> RHID3D12::getSwapchainImages(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->images;
    }
    return {};
}

std::vector<RHITextureView> RHID3D12::getSwapchainImageViews(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->imageViews;
    }
    return {};
}

RHIFormat RHID3D12::getSwapchainFormat(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->format;
    }
    return RHIFormat::Undefined;
}

RHIExtent2D RHID3D12::getSwapchainExtent(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->extent;
    }
    return {};
}

bool RHID3D12::acquireNextImage(
    RHISwapchain swapchain,
    RHISemaphore signalSemaphore,
    RHIFence signalFence,
    u32* imageIndex,
    std::string* errorMessage) {
    try {
        Impl::SwapchainResource* swapchainResource = getRenderResource(impl_->swapchains, swapchain);
        if (swapchainResource == nullptr || !swapchainResource->swapchain) {
            throw std::runtime_error("acquireNextImage swapchain is invalid");
        }

        swapchainResource->currentImage = swapchainResource->swapchain->GetCurrentBackBufferIndex();
        if (imageIndex != nullptr) {
            *imageIndex = swapchainResource->currentImage;
        }

        if (Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, signalSemaphore)) {
            semaphore->signaled = true;
            if (semaphore->desc.type == RHISemaphoreType::Timeline) {
                ++semaphore->value;
            }
        }
        if (Impl::FenceResource* fence = getRenderResource(impl_->fences, signalFence)) {
            ++fence->value;
            throwIfFailed(fence->fence->Signal(fence->value), "ID3D12Fence::Signal acquire fence failed");
            fence->signaled = true;
        }
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool RHID3D12::submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) {
    try {
        for (const RHIQueueWaitDesc& wait : desc.waits) {
            const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, wait.semaphore);
            if (semaphore == nullptr) {
                throw std::runtime_error("RHIQueueSubmitDesc contains an invalid wait semaphore");
            }
            if (semaphore->desc.type == RHISemaphoreType::Timeline && semaphore->value < wait.value) {
                throw std::runtime_error("D3D12 timeline semaphore wait value has not been reached");
            }
            if (semaphore->desc.type == RHISemaphoreType::Binary && !semaphore->signaled) {
                throw std::runtime_error("D3D12 binary semaphore wait has not been signaled");
            }
        }

        // 当前 skeleton 没有完整 RHIFramePacket command list 录制。若未来 Frame 片段打开 commandList，
        // submit 会在这里 Close + ExecuteCommandLists，再用 fence 把 GPU 顺序暴露给 CPU。
        if (impl_->commandListOpen) {
            throwIfFailed(impl_->commandList->Close(), "Close command list failed");
            ID3D12CommandList* lists[] = {impl_->commandList.Get()};
            impl_->graphicsQueue->ExecuteCommandLists(1, lists);
            impl_->commandListOpen = false;
        }

        for (const RHIQueueSignalDesc& signal : desc.signals) {
            Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, signal.semaphore);
            if (semaphore == nullptr) {
                throw std::runtime_error("RHIQueueSubmitDesc contains an invalid signal semaphore");
            }
            semaphore->signaled = true;
            semaphore->value = semaphore->desc.type == RHISemaphoreType::Timeline ? signal.value : semaphore->value + 1;
        }

        if (Impl::FenceResource* fence = getRenderResource(impl_->fences, desc.fence)) {
            ++fence->value;
            throwIfFailed(impl_->graphicsQueue->Signal(fence->fence.Get(), fence->value), "ID3D12CommandQueue::Signal submit fence failed");
            fence->signaled = true;
        }

        ++impl_->fenceValue;
        throwIfFailed(impl_->graphicsQueue->Signal(impl_->fence.Get(), impl_->fenceValue), "ID3D12CommandQueue::Signal internal fence failed");
        impl_->refreshNativeHandles();
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool RHID3D12::present(const RHIPresentDesc& desc, std::string* errorMessage) {
    try {
        Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, desc.swapchain);
        if (swapchain == nullptr || !swapchain->swapchain) {
            throw std::runtime_error("RHIPresentDesc::swapchain is invalid");
        }

        for (RHISemaphore waitSemaphore : desc.waitSemaphores) {
            const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, waitSemaphore);
            if (semaphore == nullptr) {
                throw std::runtime_error("RHIPresentDesc contains an invalid wait semaphore");
            }
            if (semaphore->desc.type == RHISemaphoreType::Binary && !semaphore->signaled) {
                throw std::runtime_error("D3D12 present wait semaphore has not been signaled");
            }
        }

        const bool allowTearing = desc.allowTearing && supportsTearing(impl_->factory.Get());
        const UINT syncInterval = desc.presentMode == RHIPresentMode::Immediate || allowTearing ? 0u : 1u;
        const UINT flags = allowTearing && syncInterval == 0 ? DXGI_PRESENT_ALLOW_TEARING : 0u;
        throwIfFailed(swapchain->swapchain->Present(syncInterval, flags), "IDXGISwapChain::Present failed");
        swapchain->currentImage = swapchain->swapchain->GetCurrentBackBufferIndex();
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

// D3D12 sync/swapchain 片段负责帧边界：
// - QueryPool 使用 ID3D12QueryHeap；
// - Fence 使用真实 ID3D12Fence + Win32 event；
// - Swapchain 使用 IDXGISwapChain3 并把每个 back buffer 包装成统一 RHITexture；
// - Semaphore 暂时是 CPU 模拟，后续若需要跨进程/跨队列同步可扩展到 shared fence。
#if defined(__INTELLISENSE__) && !defined(RHI_D3D12_IMPLEMENTATION_ASSEMBLY)
} // namespace rhi
#endif


