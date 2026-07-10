#include "RHID3D11Private.hpp"

namespace rhi {

RHID3D11Device::RHID3D11Device()
    : impl_(std::make_unique<Impl>()) {
}

RHID3D11Device::~RHID3D11Device() {
    shutdown();
}

RHIGraphicsAPI RHID3D11Device::api() const noexcept { return RHIGraphicsAPI::Direct3D11; }
const char* RHID3D11Device::backendName() const noexcept { return "Direct3D 11"; }

bool RHID3D11Device::initialize(const RHIDeviceCreateDesc& desc, std::string* errorMessage) {
    D3D11RendererDesc nativeDesc{};
    nativeDesc.backend = desc.backend;
    nativeDesc.backend.preferredApi = GraphicsApi::Direct3D11;
    nativeDesc.surface.hwnd = static_cast<HWND>(desc.nativeWindow);
    nativeDesc.minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    nativeDesc.allowWarpFallback = desc.allowSoftwareAdapter;
    return impl_->renderer.initialize(nativeDesc, errorMessage);
}

void RHID3D11Device::shutdown() noexcept { impl_->renderer.shutdown(); }
bool RHID3D11Device::isInitialized() const noexcept { return impl_->renderer.isInitialized(); }
const RHICapabilities& RHID3D11Device::capabilities() const noexcept { return impl_->renderer.capabilities(); }

} // namespace rhi