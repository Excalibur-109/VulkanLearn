#include "RHID3D11Private.hpp"

namespace rhi {
RHIQueryPool RHID3D11Device::createQueryPool(const RHIQueryPoolDesc& desc) { return impl_->backend.createQueryPool(desc); }
RHISemaphore RHID3D11Device::createSemaphore(const RHISemaphoreDesc& desc) { return impl_->backend.createSemaphore(desc); }
RHIFence RHID3D11Device::createFence(const RHIFenceDesc& desc) { return impl_->backend.createFence(desc); }
RHISwapchain RHID3D11Device::createSwapchain(const RHISwapchainDesc& desc) { return impl_->backend.createSwapchain(desc); }
std::vector<RHITexture> RHID3D11Device::getSwapchainImages(RHISwapchain handle) const { return impl_->backend.getSwapchainImages(handle); }
std::vector<RHITextureView> RHID3D11Device::getSwapchainImageViews(RHISwapchain handle) const { return impl_->backend.getSwapchainImageViews(handle); }
RHIFormat RHID3D11Device::getSwapchainFormat(RHISwapchain handle) const { return impl_->backend.getSwapchainFormat(handle); }
RHIExtent2D RHID3D11Device::getSwapchainExtent(RHISwapchain handle) const { return impl_->backend.getSwapchainExtent(handle); }
bool RHID3D11Device::acquireNextImage(RHISwapchain swapchain, RHISemaphore signalSemaphore, RHIFence signalFence, RHIUInt32* imageIndex, std::string* errorMessage) { return impl_->backend.acquireNextImage(swapchain, signalSemaphore, signalFence, imageIndex, errorMessage); }
bool RHID3D11Device::present(const RHIPresentDesc& desc, std::string* errorMessage) { return impl_->backend.present(desc, errorMessage); }
void RHID3D11Device::destroy(RHIQueryPool handle) noexcept { impl_->backend.destroy(handle); }
void RHID3D11Device::destroy(RHISemaphore handle) noexcept { impl_->backend.destroy(handle); }
void RHID3D11Device::destroy(RHIFence handle) noexcept { impl_->backend.destroy(handle); }
void RHID3D11Device::destroy(RHISwapchain handle) noexcept { impl_->backend.destroy(handle); }
} // namespace rhi