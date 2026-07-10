#include "RHID3D12Private.hpp"

namespace rhi {

RHID3D12Device::RHID3D12Device()
    : impl_(std::make_unique<Impl>()) {
}

RHID3D12Device::~RHID3D12Device() {
    shutdown();
}

RHIGraphicsAPI RHID3D12Device::api() const noexcept { return RHIGraphicsAPI::Direct3D12; }
const char* RHID3D12Device::backendName() const noexcept { return "Direct3D 12"; }

bool RHID3D12Device::initialize(const RHIDeviceCreateDesc& desc, std::string* errorMessage) {
    D3D12RendererDesc nativeDesc{};
    nativeDesc.backend = desc.backend;
    nativeDesc.backend.preferredApi = GraphicsApi::Direct3D12;
    nativeDesc.surface.hwnd = static_cast<HWND>(desc.nativeWindow);
    nativeDesc.minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    nativeDesc.allowWarpFallback = desc.allowSoftwareAdapter;
    return impl_->renderer.initialize(nativeDesc, errorMessage);
}

void RHID3D12Device::shutdown() noexcept { impl_->renderer.shutdown(); }
bool RHID3D12Device::isInitialized() const noexcept { return impl_->renderer.isInitialized(); }
const RHICapabilities& RHID3D12Device::capabilities() const noexcept { return impl_->renderer.capabilities(); }

} // namespace rhi