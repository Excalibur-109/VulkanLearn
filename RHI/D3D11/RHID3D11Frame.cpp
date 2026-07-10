#include "RHID3D11Private.hpp"

namespace rhi {
bool RHID3D11Device::submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) { return impl_->renderer.submit(desc, errorMessage); }
bool RHID3D11Device::submitFrame(const RHIFramePacket& packet, std::string* errorMessage) { return impl_->renderer.submitFrame(packet, errorMessage); }
void RHID3D11Device::waitIdle() const noexcept { impl_->renderer.waitIdle(); }
} // namespace rhi