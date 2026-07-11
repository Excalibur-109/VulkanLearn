#include "RHIVulkanPrivate.hpp"

namespace rhi {
bool RHIVulkanDevice::submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) { return impl_->backend.submit(desc, errorMessage); }
bool RHIVulkanDevice::submitFrame(const RHIFramePacket& packet, std::string* errorMessage) { return impl_->backend.submitFrame(packet, errorMessage); }
void RHIVulkanDevice::waitIdle() const noexcept { impl_->backend.waitIdle(); }
} // namespace rhi