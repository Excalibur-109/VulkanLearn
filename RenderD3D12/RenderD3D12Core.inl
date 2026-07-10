D3D12Renderer::D3D12Renderer()
    : impl_(std::make_unique<Impl>()) {
}

D3D12Renderer::~D3D12Renderer() {
    shutdown();
}

D3D12Renderer::D3D12Renderer(D3D12Renderer&&) noexcept = default;

D3D12Renderer& D3D12Renderer::operator=(D3D12Renderer&&) noexcept = default;

// 初始化 D3D12 后端的主流程：
// 1. 可选启用 debug layer，让 D3D12 在资源状态、descriptor、PSO 等错误上给出详细消息；
// 2. 创建 DXGI factory，并按 PowerPreference 选择支持 D3D12 的 adapter；
// 3. 创建 ID3D12Device、graphics queue、command allocator/list 和内部 fence；
// 4. 创建 CPU descriptor heaps。当前后端先保存 CPU descriptor，后续完整 FramePacket 录制时再拷贝到 shader-visible heap；
// 5. 生成 RenderCapabilities，并校验 requiredFeatures。
bool D3D12Renderer::initialize(const D3D12RendererDesc& desc, std::string* errorMessage) {
    try {
        if (isInitialized()) {
            shutdown();
        }

        impl_ = std::make_unique<Impl>();
        impl_->initDesc = desc;

        UINT factoryFlags = 0;
        if (desc.backend.validation != ValidationMode::Disabled) {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) {
                debug->EnableDebugLayer();
                factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
        }

        throwIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&impl_->factory)), "CreateDXGIFactory2 failed");
        impl_->adapter = chooseAdapter(impl_->factory.Get(), desc.backend.powerPreference, desc.minimumFeatureLevel);

        HRESULT hr = E_FAIL;
        if (impl_->adapter) {
            hr = D3D12CreateDevice(impl_->adapter.Get(), desc.minimumFeatureLevel, IID_PPV_ARGS(&impl_->device));
        }

        if (FAILED(hr) && desc.allowWarpFallback) {
            ComPtr<IDXGIAdapter> warpAdapter;
            throwIfFailed(impl_->factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)), "EnumWarpAdapter failed");
            throwIfFailed(warpAdapter.As(&impl_->adapter), "Query WARP IDXGIAdapter1 failed");
            hr = D3D12CreateDevice(impl_->adapter.Get(), desc.minimumFeatureLevel, IID_PPV_ARGS(&impl_->device));
        }
        throwIfFailed(hr, "D3D12CreateDevice failed");

        impl_->featureLevel = queryDeviceFeatureLevel(impl_->device.Get());
        if (impl_->featureLevel < desc.minimumFeatureLevel) {
            throw std::runtime_error("D3D12 feature level is below the requested minimum");
        }

        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        throwIfFailed(impl_->device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&impl_->graphicsQueue)), "CreateCommandQueue failed");
        impl_->setDebugName(impl_->graphicsQueue.Get(), "D3D12.GraphicsQueue");

        throwIfFailed(
            impl_->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&impl_->commandAllocator)),
            "CreateCommandAllocator failed");
        impl_->setDebugName(impl_->commandAllocator.Get(), "D3D12.CommandAllocator");

        throwIfFailed(
            impl_->device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                impl_->commandAllocator.Get(),
                nullptr,
                IID_PPV_ARGS(&impl_->commandList)),
            "CreateCommandList failed");
        impl_->setDebugName(impl_->commandList.Get(), "D3D12.CommandList");
        throwIfFailed(impl_->commandList->Close(), "Close initial command list failed");
        impl_->commandListOpen = false;

        throwIfFailed(impl_->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&impl_->fence)), "CreateFence failed");
        impl_->setDebugName(impl_->fence.Get(), "D3D12.InternalFence");
        impl_->fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (impl_->fenceEvent == nullptr) {
            throw std::runtime_error("CreateEventW for D3D12 fence failed");
        }

        impl_->createDescriptorHeap(impl_->cbvSrvUavHeap, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 4096);
        impl_->createDescriptorHeap(impl_->rtvHeap, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 512);
        impl_->createDescriptorHeap(impl_->dsvHeap, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 256);
        impl_->createDescriptorHeap(impl_->samplerHeap, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 512);

        impl_->caps = makeCapabilities(impl_->adapter.Get(), impl_->featureLevel);
        if (!supportsRequiredFeatures(impl_->caps, desc.backend.requiredFeatures)) {
            throw std::runtime_error("D3D12 device does not support all required RenderFeature flags");
        }

        impl_->refreshNativeHandles();
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        shutdown();
        return false;
    }
}

void D3D12Renderer::shutdown() noexcept {
    if (!impl_) {
        return;
    }

    waitIdle();

    for (u64 i = impl_->swapchains.size(); i > 0; --i)       destroy(SwapchainHandle(i));
    for (u64 i = impl_->pipelines.size(); i > 0; --i)        destroy(PipelineHandle(i));
    for (u64 i = impl_->pipelineCaches.size(); i > 0; --i)   destroy(PipelineCacheHandle(i));
    for (u64 i = impl_->pipelineLayouts.size(); i > 0; --i)  destroy(PipelineLayoutHandle(i));
    for (u64 i = impl_->bindGroups.size(); i > 0; --i)       destroy(BindGroupHandle(i));
    for (u64 i = impl_->bindGroupLayouts.size(); i > 0; --i) destroy(BindGroupLayoutHandle(i));
    for (u64 i = impl_->queryPools.size(); i > 0; --i)       destroy(QueryPoolHandle(i));
    for (u64 i = impl_->semaphores.size(); i > 0; --i)       destroy(SemaphoreHandle(i));
    for (u64 i = impl_->fences.size(); i > 0; --i)           destroy(FenceHandle(i));
    for (u64 i = impl_->shaders.size(); i > 0; --i)          destroy(ShaderHandle(i));
    for (u64 i = impl_->samplers.size(); i > 0; --i)         destroy(SamplerHandle(i));
    for (u64 i = impl_->textureViews.size(); i > 0; --i)     destroy(TextureViewHandle(i));
    for (u64 i = impl_->textures.size(); i > 0; --i)         destroy(TextureHandle(i));
    for (u64 i = impl_->buffers.size(); i > 0; --i)          destroy(BufferHandle(i));

    impl_->native = {};
    impl_->cbvSrvUavHeap = {};
    impl_->rtvHeap = {};
    impl_->dsvHeap = {};
    impl_->samplerHeap = {};
    impl_->commandList.Reset();
    impl_->commandAllocator.Reset();
    impl_->graphicsQueue.Reset();
    impl_->fence.Reset();
    if (impl_->fenceEvent != nullptr) {
        CloseHandle(impl_->fenceEvent);
        impl_->fenceEvent = nullptr;
    }
    impl_->device.Reset();
    impl_->adapter.Reset();
    impl_->factory.Reset();
    impl_->fenceValue = 0;
    impl_->commandListOpen = false;
}

bool D3D12Renderer::isInitialized() const noexcept {
    return impl_ != nullptr && impl_->device != nullptr && impl_->graphicsQueue != nullptr;
}

const RenderCapabilities& D3D12Renderer::capabilities() const noexcept {
    return impl_->caps;
}

const D3D12NativeHandles& D3D12Renderer::nativeHandles() const noexcept {
    return impl_->native;
}

// D3D12 core 片段只负责“从无到可用”的后端生命周期。
// 注意这里已经创建了 command list，但它只是基础命令录制容器；真正把 RenderGraph 变成 draw/dispatch，
// 需要在 Frame 片段中补 command allocator reset、barrier、root signature/descriptor heap 绑定等步骤。
