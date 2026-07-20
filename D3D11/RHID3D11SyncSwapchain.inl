#pragma once

#include "RHID3D11Private.inl"

namespace rhi {

RHIQueryPool RHID3D11::CreateQueryPool(const RHIQueryPoolDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHID3D11 is not initialized");
    }

    Impl::QueryPoolResource resource{};
    resource.desc = desc;
    resource.queries.reserve(desc.queryCount);

    D3D11_QUERY_DESC queryDesc{};
    switch (desc.type) {
    case RHIQueryType::Timestamp: queryDesc.Query = D3D11_QUERY_TIMESTAMP; break;
    case RHIQueryType::Occlusion: queryDesc.Query = D3D11_QUERY_OCCLUSION; break;
    case RHIQueryType::PipelineStatistics: queryDesc.Query = D3D11_QUERY_PIPELINE_STATISTICS; break;
    }

    for (u32 i = 0; i < desc.queryCount; ++i) {
        ComPtr<ID3D11Query> query;
        throwIfFailed(impl_->device->CreateQuery(&queryDesc, &query), "CreateQuery failed");
        resource.queries.push_back(query);
    }
    return makeRenderHandle<RHIQueryPool>(impl_->queryPools, std::move(resource));
}

// D3D11 没有 Vulkan timeline/binary semaphore 的原生等价物。本后端用轻量 CPU 状态模拟
// 统一接口里的 semaphore，适合示例和跨后端流程对齐，不适合作为跨 queue 精细同步。
RHIGPUWaitGPUSignal RHID3D11::CreateGPUWaitGPUSignal(const RHIGPUWaitGPUSignalDesc& desc) {
    Impl::GPUWaitGPUSignalResource resource{};
    resource.desc = desc;
    resource.value = desc.initialValue;
    resource.signaled = desc.type == RHIGPUWaitGPUSignalType::Timeline && desc.initialValue > 0;
    return makeRenderHandle<RHIGPUWaitGPUSignal>(impl_->gpuWaitGPUSignals, std::move(resource));
}

// Fence 用 D3D11_QUERY_EVENT 实现：Submit 时 End 一个 event query，WaitIdle/GetData 可检查
// GPU 是否执行到该点。
RHICPUWaitGPUSignal RHID3D11::CreateCPUWaitGPUSignal(const RHICPUWaitGPUSignalDesc& desc) {
    Impl::CPUWaitGPUSignalResource resource{};
    resource.desc = desc;
    resource.signaled = desc.signaled;
    return makeRenderHandle<RHICPUWaitGPUSignal>(impl_->cpuWaitGPUSignals, std::move(resource));
}

// D3D11 swapchain 通常只有一个当前 back buffer。为了和 Vulkan 多图像 swapchain 接口一致，
// 这里也把 back buffer 包装成 RHITexture + RHITextureView，供 RenderGraph 统一引用。
RHISwapchain RHID3D11::CreateSwapchain(const RHISwapchainDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHID3D11 is not initialized");
    }
    if (impl_->initDesc.surface.hwnd == nullptr) {
        throw std::runtime_error("D3D11 swapchain creation requires RHID3D11SurfaceDesc::hwnd");
    }

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferDesc.Width = desc.extent.width;
    swapDesc.BufferDesc.Height = desc.extent.height;
    swapDesc.BufferDesc.Format = toSwapchainFormat(desc.preferredFormat);
    swapDesc.BufferDesc.RefreshRate.Numerator = 0;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    swapDesc.BufferCount = std::max(1u, desc.imageCount);
    swapDesc.OutputWindow = impl_->initDesc.surface.hwnd;
    swapDesc.Windowed = !desc.fullscreen;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapDesc.Flags = desc.fullscreen ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;

    Impl::SwapchainResource resource{};
    resource.desc = desc;
    throwIfFailed(impl_->factory->CreateSwapChain(impl_->device.Get(), &swapDesc, &resource.swapchain), "IDXGIFactory::CreateSwapChain failed");
    impl_->factory->MakeWindowAssociation(impl_->initDesc.surface.hwnd, DXGI_MWA_NO_ALT_ENTER);
    resource.format = fromSwapchainFormat(
        desc.preferredFormat,
        swapDesc.BufferDesc.Format);
    resource.extent = desc.extent;

    ComPtr<ID3D11Texture2D> backBuffer;
    throwIfFailed(resource.swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "IDXGISwapChain::GetBuffer failed");

    RHITextureDesc textureDesc{};
    textureDesc.debugName = desc.debugName + ".BackBuffer";
    textureDesc.dimension = RHITextureDimension::Texture2D;
    textureDesc.extent = {desc.extent.width, desc.extent.height, 1};
    textureDesc.arrayLayers = 1;
    textureDesc.mipLevels = 1;
    textureDesc.format = resource.format == RHIFormat::Undefined ? RHIFormat::BGRA8_UNorm : resource.format;
    textureDesc.samples = RHISampleCount::Count1;
    textureDesc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Sampled | RHITextureUsage::Present;
    textureDesc.initialState = RHIResourceState::Present;

    Impl::TextureResource textureResource{};
    textureResource.desc = textureDesc;
    textureResource.resource = backBuffer;
    textureResource.currentState = RHIResourceState::Present;
    textureResource.swapchainImage = true;
    RHITexture textureHandle = makeRenderHandle<RHITexture>(impl_->textures, std::move(textureResource));

    RHITextureViewDesc viewDesc{};
    viewDesc.debugName = desc.debugName + ".BackBufferView";
    viewDesc.texture = textureHandle;
    viewDesc.dimension = RHITextureViewDimension::View2D;
    viewDesc.format = textureDesc.format;
    viewDesc.aspect = RHITextureAspect::Color;

    Impl::TextureViewResource viewResource{};
    viewResource.desc = viewDesc;
    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
    rtvDesc.Format = toDxgiFormat(viewDesc.format);
    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    throwIfFailed(
        impl_->device->CreateRenderTargetView(
            backBuffer.Get(),
            &rtvDesc,
            &viewResource.rtv),
        "Create backbuffer RTV failed");

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    // 部分 D3D11 驱动允许 back buffer 创建 SRGB RTV，却拒绝 SRGB SRV。
    // SRV 使用交换链物理 UNORM 格式；颜色空间转换只由呈现用 RTV 负责。
    srvDesc.Format = swapDesc.BufferDesc.Format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    throwIfFailed(
        impl_->device->CreateShaderResourceView(
            backBuffer.Get(),
            &srvDesc,
            &viewResource.srv),
        "Create backbuffer SRV failed");
    RHITextureView viewHandle = makeRenderHandle<RHITextureView>(impl_->textureViews, std::move(viewResource));

    resource.images = {textureHandle};
    resource.imageViews = {viewHandle};
    return makeRenderHandle<RHISwapchain>(impl_->swapchains, std::move(resource));
}

std::vector<RHITexture> RHID3D11::GetSwapchainImages(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->images;
    }
    return {};
}

std::vector<RHITextureView> RHID3D11::GetSwapchainImageViews(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->imageViews;
    }
    return {};
}

RHIFormat RHID3D11::GetSwapchainFormat(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->format;
    }
    return RHIFormat::Undefined;
}

RHIExtent2D RHID3D11::GetSwapchainExtent(RHISwapchain handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->extent;
    }
    return {};
}

// D3D11 sync/swapchain 片段负责和“帧边界”相关的对象：
// query pool、模拟 semaphore/fence、DXGI swapchain、acquire/Submit/Present。
// D3D11 immediate context 没有 Vulkan 那种显式 queue Submit；Submit 多数时候只是对模拟同步状态做标记，
// 真正的 GPU 命令已经在 RHIFramePacket 执行时写入 immediate context，Present 则通过 IDXGISwapChain::Present 完成。

} // namespace rhi








