#include "RHIVulkanPrivate.hpp"

namespace rhi {
RHIQueryPool RHIVulkanDevice::createQueryPool(const RHIQueryPoolDesc& desc) { return impl_->renderer.createQueryPool(desc); }
RHISemaphore RHIVulkanDevice::createSemaphore(const RHISemaphoreDesc& desc) { return impl_->renderer.createSemaphore(desc); }
RHIFence RHIVulkanDevice::createFence(const RHIFenceDesc& desc) { return impl_->renderer.createFence(desc); }
RHISwapchain RHIVulkanDevice::createSwapchain(const RHISwapchainDesc& desc) { return impl_->renderer.createSwapchain(desc); }
std::vector<RHITexture> RHIVulkanDevice::getSwapchainImages(RHISwapchain handle) const { return impl_->renderer.getSwapchainImages(handle); }
std::vector<RHITextureView> RHIVulkanDevice::getSwapchainImageViews(RHISwapchain handle) const { return impl_->renderer.getSwapchainImageViews(handle); }
RHIFormat RHIVulkanDevice::getSwapchainFormat(RHISwapchain handle) const { return impl_->renderer.getSwapchainFormat(handle); }
RHIExtent2D RHIVulkanDevice::getSwapchainExtent(RHISwapchain handle) const { return impl_->renderer.getSwapchainExtent(handle); }
bool RHIVulkanDevice::acquireNextImage(RHISwapchain swapchain, RHISemaphore signalSemaphore, RHIFence signalFence, RHIUInt32* imageIndex, std::string* errorMessage) { return impl_->renderer.acquireNextImage(swapchain, signalSemaphore, signalFence, imageIndex, errorMessage); }
bool RHIVulkanDevice::present(const RHIPresentDesc& desc, std::string* errorMessage) { return impl_->renderer.present(desc, errorMessage); }
void RHIVulkanDevice::destroy(RHIQueryPool handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHISemaphore handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHIFence handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHISwapchain handle) noexcept { impl_->renderer.destroy(handle); }
} // namespace rhi