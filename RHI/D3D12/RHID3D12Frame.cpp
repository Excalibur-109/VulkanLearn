#include "RHID3D12Private.hpp"

namespace rhi {
bool RHID3D12Device::submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) { return impl_->backend.submit(desc, errorMessage); }
bool RHID3D12Device::submitFrame(const RHIFramePacket& packet, std::string* errorMessage) { return impl_->backend.submitFrame(packet, errorMessage); }
void RHID3D12Device::waitIdle() const noexcept { impl_->backend.waitIdle(); }
} // namespace rhi