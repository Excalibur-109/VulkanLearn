#include "RHID3D11Private.hpp"

namespace rhi {
RHIPipelineLayout RHID3D11Device::createPipelineLayout(const RHIPipelineLayoutDesc& desc) { return impl_->backend.createPipelineLayout(desc); }
RHIPipelineCache RHID3D11Device::createPipelineCache(const RHIPipelineCacheDesc& desc) { return impl_->backend.createPipelineCache(desc); }
RHIPipeline RHID3D11Device::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) { return impl_->backend.createGraphicsPipeline(desc); }
RHIPipeline RHID3D11Device::createComputePipeline(const RHIComputePipelineDesc& desc) { return impl_->backend.createComputePipeline(desc); }
void RHID3D11Device::destroy(RHIPipelineLayout handle) noexcept { impl_->backend.destroy(handle); }
void RHID3D11Device::destroy(RHIPipelineCache handle) noexcept { impl_->backend.destroy(handle); }
void RHID3D11Device::destroy(RHIPipeline handle) noexcept { impl_->backend.destroy(handle); }
} // namespace rhi