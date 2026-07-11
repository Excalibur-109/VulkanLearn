#include "RHIVulkanPrivate.hpp"

namespace rhi {
RHIQueryPool RHIVulkanDevice::createQueryPool(const RHIQueryPoolDesc& desc) { return impl_->backend.createQueryPool(desc); }
RHISemaphore RHIVulkanDevice::createSemaphore(const RHISemaphoreDesc& desc) { return impl_->backend.createSemaphore(desc); }
RHIFence RHIVulkanDevice::createFence(const RHIFenceDesc& desc) { return impl_->backend.createFence(desc); }
RHISwapchain RHIVulkanDevice::createSwapchain(const RHISwapchainDesc& desc) { return impl_->backend.createSwapchain(desc); }
std::vector<RHITexture> RHIVulkanDevice::getSwapchainImages(RHISwapchain handle) const { return impl_->backend.getSwapchainImages(handle); }
std::vector<RHITextureView> RHIVulkanDevice::getSwapchainImageViews(RHISwapchain handle) const { return impl_->backend.getSwapchainImageViews(handle); }
RHIFormat RHIVulkanDevice::getSwapchainFormat(RHISwapchain handle) const { return impl_->backend.getSwapchainFormat(handle); }
RHIExtent2D RHIVulkanDevice::getSwapchainExtent(RHISwapchain handle) const { return impl_->backend.getSwapchainExtent(handle); }
bool RHIVulkanDevice::acquireNextImage(RHISwapchain swapchain, RHISemaphore signalSemaphore, RHIFence signalFence, RHIUInt32* imageIndex, std::string* errorMessage) { return impl_->backend.acquireNextImage(swapchain, signalSemaphore, signalFence, imageIndex, errorMessage); }
bool RHIVulkanDevice::present(const RHIPresentDesc& desc, std::string* errorMessage) { return impl_->backend.present(desc, errorMessage); }
void RHIVulkanDevice::destroy(RHIQueryPool handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHISemaphore handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHIFence handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHISwapchain handle) noexcept { impl_->backend.destroy(handle); }
} // namespace rhi