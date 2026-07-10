QueryPoolHandle D3D12Renderer::createQueryPool(const QueryPoolDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D12Renderer is not initialized");
    }

    Impl::QueryPoolResource resource{};
    resource.desc = desc;

    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Count = desc.queryCount;
    switch (desc.type) {
    case QueryType::Timestamp:
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        break;
    case QueryType::Occlusion:
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
        break;
    case QueryType::PipelineStatistics:
        queryDesc.Type = D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS;
        break;
    }

    throwIfFailed(impl_->device->CreateQueryHeap(&queryDesc, IID_PPV_ARGS(&resource.heap)), "CreateQueryHeap failed");
    impl_->setDebugName(resource.heap.Get(), desc.debugName);
    return makeRenderHandle<QueryPoolHandle>(impl_->queryPools, std::move(resource));
}

SemaphoreHandle D3D12Renderer::createSemaphore(const SemaphoreDesc& desc) {
    Impl::SemaphoreResource resource{};
    resource.desc = desc;
    resource.value = desc.initialValue;
    resource.signaled = desc.type == SemaphoreType::Timeline && desc.initialValue > 0;
    return makeRenderHandle<SemaphoreHandle>(impl_->semaphores, std::move(resource));
}

FenceHandle D3D12Renderer::createFence(const FenceDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D12Renderer is not initialized");
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
    return makeRenderHandle<FenceHandle>(impl_->fences, std::move(resource));
}

SwapchainHandle D3D12Renderer::createSwapchain(const SwapchainDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D12Renderer is not initialized");
    }
    if (impl_->initDesc.surface.hwnd == nullptr) {
        throw std::runtime_error("D3D12 swapchain creation requires D3D12SurfaceDesc::hwnd");
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

        TextureDesc textureDesc{};
        textureDesc.debugName = desc.debugName + ".BackBuffer" + std::to_string(i);
        textureDesc.dimension = TextureDimension::Texture2D;
        textureDesc.extent = {desc.extent.width, desc.extent.height, 1};
        textureDesc.arrayLayers = 1;
        textureDesc.mipLevels = 1;
        textureDesc.format = resource.format == Format::Undefined ? Format::BGRA8_UNorm : resource.format;
        textureDesc.samples = SampleCount::Count1;
        textureDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Present;
        textureDesc.initialState = ResourceState::Present;

        Impl::TextureResource textureResource{};
        textureResource.desc = textureDesc;
        textureResource.resource = backBuffer;
        textureResource.currentState = D3D12_RESOURCE_STATE_PRESENT;
        textureResource.swapchainImage = true;
        impl_->setDebugName(textureResource.resource.Get(), textureDesc.debugName);
        TextureHandle textureHandle = makeRenderHandle<TextureHandle>(impl_->textures, std::move(textureResource));

        TextureViewDesc viewDesc{};
        viewDesc.debugName = desc.debugName + ".BackBufferView" + std::to_string(i);
        viewDesc.texture = textureHandle;
        viewDesc.dimension = TextureViewDimension::View2D;
        viewDesc.format = textureDesc.format;
        viewDesc.aspect = TextureAspect::Color;

        Impl::TextureViewResource viewResource{};
        viewResource.desc = viewDesc;
        viewResource.rtv = impl_->allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        impl_->device->CreateRenderTargetView(backBuffer.Get(), nullptr, viewResource.rtv.handle);
        TextureViewHandle viewHandle = makeRenderHandle<TextureViewHandle>(impl_->textureViews, std::move(viewResource));

        resource.images.push_back(textureHandle);
        resource.imageViews.push_back(viewHandle);
    }

    resource.currentImage = resource.swapchain->GetCurrentBackBufferIndex();
    return makeRenderHandle<SwapchainHandle>(impl_->swapchains, std::move(resource));
}

std::vector<TextureHandle> D3D12Renderer::getSwapchainImages(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->images;
    }
    return {};
}

std::vector<TextureViewHandle> D3D12Renderer::getSwapchainImageViews(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->imageViews;
    }
    return {};
}

Format D3D12Renderer::getSwapchainFormat(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->format;
    }
    return Format::Undefined;
}

Extent2D D3D12Renderer::getSwapchainExtent(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->extent;
    }
    return {};
}

bool D3D12Renderer::acquireNextImage(
    SwapchainHandle swapchain,
    SemaphoreHandle signalSemaphore,
    FenceHandle signalFence,
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
            if (semaphore->desc.type == SemaphoreType::Timeline) {
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

bool D3D12Renderer::submit(const QueueSubmitDesc& desc, std::string* errorMessage) {
    try {
        for (const QueueWaitDesc& wait : desc.waits) {
            const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, wait.semaphore);
            if (semaphore == nullptr) {
                throw std::runtime_error("QueueSubmitDesc contains an invalid wait semaphore");
            }
            if (semaphore->desc.type == SemaphoreType::Timeline && semaphore->value < wait.value) {
                throw std::runtime_error("D3D12 timeline semaphore wait value has not been reached");
            }
            if (semaphore->desc.type == SemaphoreType::Binary && !semaphore->signaled) {
                throw std::runtime_error("D3D12 binary semaphore wait has not been signaled");
            }
        }

        // 当前 skeleton 没有完整 FramePacket command list 录制。若未来 Frame 片段打开 commandList，
        // submit 会在这里 Close + ExecuteCommandLists，再用 fence 把 GPU 顺序暴露给 CPU。
        if (impl_->commandListOpen) {
            throwIfFailed(impl_->commandList->Close(), "Close command list failed");
            ID3D12CommandList* lists[] = {impl_->commandList.Get()};
            impl_->graphicsQueue->ExecuteCommandLists(1, lists);
            impl_->commandListOpen = false;
        }

        for (const QueueSignalDesc& signal : desc.signals) {
            Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, signal.semaphore);
            if (semaphore == nullptr) {
                throw std::runtime_error("QueueSubmitDesc contains an invalid signal semaphore");
            }
            semaphore->signaled = true;
            semaphore->value = semaphore->desc.type == SemaphoreType::Timeline ? signal.value : semaphore->value + 1;
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

bool D3D12Renderer::present(const PresentDesc& desc, std::string* errorMessage) {
    try {
        Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, desc.swapchain);
        if (swapchain == nullptr || !swapchain->swapchain) {
            throw std::runtime_error("PresentDesc::swapchain is invalid");
        }

        for (SemaphoreHandle waitSemaphore : desc.waitSemaphores) {
            const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, waitSemaphore);
            if (semaphore == nullptr) {
                throw std::runtime_error("PresentDesc contains an invalid wait semaphore");
            }
            if (semaphore->desc.type == SemaphoreType::Binary && !semaphore->signaled) {
                throw std::runtime_error("D3D12 present wait semaphore has not been signaled");
            }
        }

        const bool allowTearing = desc.allowTearing && supportsTearing(impl_->factory.Get());
        const UINT syncInterval = desc.presentMode == PresentMode::Immediate || allowTearing ? 0u : 1u;
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
// - Swapchain 使用 IDXGISwapChain3 并把每个 back buffer 包装成统一 TextureHandle；
// - Semaphore 暂时是 CPU 模拟，后续若需要跨进程/跨队列同步可扩展到 shared fence。
