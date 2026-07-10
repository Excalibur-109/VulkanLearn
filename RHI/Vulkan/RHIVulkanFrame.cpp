#include "RHIVulkanPrivate.hpp"

namespace rhi {
bool RHIVulkanDevice::submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) { return impl_->renderer.submit(desc, errorMessage); }
bool RHIVulkanDevice::submitFrame(const RHIFramePacket& packet, std::string* errorMessage) { return impl_->renderer.submitFrame(packet, errorMessage); }
void RHIVulkanDevice::waitIdle() const noexcept { impl_->renderer.waitIdle(); }
} // namespace rhi