#pragma once

#include "RHID3D11Private.inl"

namespace rhi {

RHID3D11::RHID3D11()
    : impl_(std::make_unique<Impl>()) {
}

RHID3D11::~RHID3D11() {
    shutdown();
}

RHID3D11::RHID3D11(RHID3D11&&) noexcept = default;

RHID3D11& RHID3D11::operator=(RHID3D11&&) noexcept = default;

// 初始化 D3D11 后端的主流程：
// 1. 创建 DXGI factory 并按 RHIPowerPreference 选择 adapter；
// 2. 用 adapter 或指定 driverType 创建 ID3D11Device + immediate context；
// 3. 允许在硬件设备失败时 fallback 到 WARP；
// 4. 生成 RHICapabilities，并检查 minimumFeatureLevel/requiredFeatures；
// 5. 刷新 native handle，供示例或平台层必要时访问原生对象。
bool RHID3D11::initialize(const RHID3D11Desc& desc, std::string* errorMessage) {
    try {
        if (isInitialized()) {
            shutdown();
        }

        impl_ = std::make_unique<Impl>();
        impl_->initDesc = desc;

        throwIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&impl_->factory)), "CreateDXGIFactory1 failed");
        impl_->adapter = chooseAdapter(impl_->factory.Get(), desc.backend.powerPreference);

        UINT createFlags = 0;
        if (desc.backend.validation != RHIValidationMode::Disabled) {
            createFlags |= D3D11_CREATE_DEVICE_DEBUG;
        }

        const std::array<D3D_FEATURE_LEVEL, 5> featureLevels = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3
        };

        HRESULT hr = E_FAIL;
        if (impl_->adapter) {
            hr = D3D11CreateDevice(
                impl_->adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                createFlags,
                featureLevels.data(),
                static_cast<UINT>(featureLevels.size()),
                D3D11_SDK_VERSION,
                &impl_->device,
                &impl_->featureLevel,
                &impl_->context);
        } else if (desc.driverType != D3D_DRIVER_TYPE_UNKNOWN) {
            hr = D3D11CreateDevice(
                nullptr,
                desc.driverType,
                nullptr,
                createFlags,
                featureLevels.data(),
                static_cast<UINT>(featureLevels.size()),
                D3D11_SDK_VERSION,
                &impl_->device,
                &impl_->featureLevel,
                &impl_->context);
        }

        if (FAILED(hr) && desc.allowWarpFallback) {
            hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                createFlags,
                featureLevels.data(),
                static_cast<UINT>(featureLevels.size()),
                D3D11_SDK_VERSION,
                &impl_->device,
                &impl_->featureLevel,
                &impl_->context);
        }
        throwIfFailed(hr, "D3D11CreateDevice failed");

        if (!impl_->adapter) {
            ComPtr<IDXGIDevice> dxgiDevice;
            throwIfFailed(impl_->device.As(&dxgiDevice), "Query IDXGIDevice failed");
            ComPtr<IDXGIAdapter> baseAdapter;
            throwIfFailed(dxgiDevice->GetAdapter(&baseAdapter), "IDXGIDevice::GetAdapter failed");
            throwIfFailed(baseAdapter.As(&impl_->adapter), "Query IDXGIAdapter1 failed");
        }

        impl_->caps = makeCapabilities(impl_->adapter.Get(), impl_->featureLevel);
        if (impl_->featureLevel < desc.minimumFeatureLevel) {
            throw std::runtime_error("D3D11 feature level is below the requested minimum");
        }
        if (!supportsRequiredFeatures(impl_->caps, desc.backend.requiredFeatures)) {
            throw std::runtime_error("D3D11 device does not support all required RHIRenderFeature flags");
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

void RHID3D11::shutdown() noexcept {
    if (!impl_) {
        return;
    }

    if (impl_->context) {
        // ClearState 解除 context 对资源/状态对象的引用，再 Flush 让驱动尽快处理已提交命令；
        // 否则 COM 引用可能让资源在 Reset 时仍被 context 持有。
        impl_->context->ClearState();
        impl_->context->Flush();
    }

    // 按依赖反向释放：swapchain/backbuffer view 先释放，pipeline/bind group 等引用资源的对象
    // 先清掉，最后再释放 texture/buffer。ComPtr 会处理 COM Release，vector 槽位不压缩。
    for (u64 i = impl_->swapchains.size(); i > 0; --i)       destroy(RHISwapchain(i));
    for (u64 i = impl_->pipelines.size(); i > 0; --i)        destroy(RHIPipeline(i));
    for (u64 i = impl_->pipelineCaches.size(); i > 0; --i)   destroy(RHIPipelineCache(i));
    for (u64 i = impl_->pipelineLayouts.size(); i > 0; --i)  destroy(RHIPipelineLayout(i));
    for (u64 i = impl_->bindGroups.size(); i > 0; --i)       destroy(RHIBindGroup(i));
    for (u64 i = impl_->bindGroupLayouts.size(); i > 0; --i) destroy(RHIBindGroupLayout(i));
    for (u64 i = impl_->queryPools.size(); i > 0; --i)       destroy(RHIQueryPool(i));
    for (u64 i = impl_->semaphores.size(); i > 0; --i)       destroy(RHISemaphore(i));
    for (u64 i = impl_->fences.size(); i > 0; --i)           destroy(RHIFence(i));
    for (u64 i = impl_->shaders.size(); i > 0; --i)          destroy(RHIShader(i));
    for (u64 i = impl_->samplers.size(); i > 0; --i)         destroy(RHISampler(i));
    for (u64 i = impl_->textureViews.size(); i > 0; --i)     destroy(RHITextureView(i));
    for (u64 i = impl_->textures.size(); i > 0; --i)         destroy(RHITexture(i));
    for (u64 i = impl_->buffers.size(); i > 0; --i)          destroy(RHIBuffer(i));

    impl_->native = {};
    impl_->context.Reset();
    impl_->device.Reset();
    impl_->adapter.Reset();
    impl_->factory.Reset();
}

bool RHID3D11::isInitialized() const noexcept {
    return impl_ != nullptr && impl_->device != nullptr && impl_->context != nullptr;
}

const RHICapabilities& RHID3D11::capabilities() const noexcept {
    return impl_->caps;
}

const RHID3D11NativeHandles& RHID3D11::nativeHandles() const noexcept {
    return impl_->native;
}

// RHIBufferDesc 会映射到 D3D11_BUFFER_DESC。D3D11 constant buffer 要 16 字节对齐；raw storage
// buffer view 需要 4 字节对齐，并通过 MISC_BUFFER_ALLOW_RAW_VIEWS 允许创建 SRV/UAV。
// D3D11 core 片段负责“后端从无到可用”的生命周期：
// - initialize 创建 DXGI factory、选择 adapter、创建 device/context，并填写 RHICapabilities；
// - shutdown 按依赖反向释放资源，并清掉 immediate context 持有的状态引用；
// - capabilities/nativeHandles 提供上层查询能力和必要的原生对象访问。
// 这里对应 Vulkan 后端的 RHIVulkanCore.inl，但 D3D11 只有一个 immediate context，
// 不需要显式创建 queue、command pool 或 descriptor pool。

} // namespace rhi

