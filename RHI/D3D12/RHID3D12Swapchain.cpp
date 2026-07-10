#include "RHID3D12Private.hpp"

namespace rhi {
RHIQueryPool RHID3D12Device::createQueryPool(const RHIQueryPoolDesc& desc) { return impl_->renderer.createQueryPool(desc); }
RHISemaphore RHID3D12Device::createSemaphore(const RHISemaphoreDesc& desc) { return impl_->renderer.createSemaphore(desc); }
RHIFence RHID3D12Device::createFence(const RHIFenceDesc& desc) { return impl_->renderer.createFence(desc); }
RHISwapchain RHID3D12Device::createSwapchain(const RHISwapchainDesc& desc) { return impl_->renderer.createSwapchain(desc); }
std::vector<RHITexture> RHID3D12Device::getSwapchainImages(RHISwapchain handle) const { return impl_->renderer.getSwapchainImages(handle); }
std::vector<RHITextureView> RHID3D12Device::getSwapchainImageViews(RHISwapchain handle) const { return impl_->renderer.getSwapchainImageViews(handle); }
RHIFormat RHID3D12Device::getSwapchainFormat(RHISwapchain handle) const { return impl_->renderer.getSwapchainFormat(handle); }
RHIExtent2D RHID3D12Device::getSwapchainExtent(RHISwapchain handle) const { return impl_->renderer.getSwapchainExtent(handle); }
bool RHID3D12Device::acquireNextImage(RHISwapchain swapchain, RHISemaphore signalSemaphore, RHIFence signalFence, RHIUInt32* imageIndex, std::string* errorMessage) { return impl_->renderer.acquireNextImage(swapchain, signalSemaphore, signalFence, imageIndex, errorMessage); }
bool RHID3D12Device::present(const RHIPresentDesc& desc, std::string* errorMessage) { return impl_->renderer.present(desc, errorMessage); }
void RHID3D12Device::destroy(RHIQueryPool handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHISemaphore handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHIFence handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHISwapchain handle) noexcept { impl_->renderer.destroy(handle); }
} // namespace rhi