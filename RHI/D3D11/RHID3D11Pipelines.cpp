#include "RHID3D11Private.hpp"

namespace rhi {
RHIPipelineLayout RHID3D11Device::createPipelineLayout(const RHIPipelineLayoutDesc& desc) { return impl_->renderer.createPipelineLayout(desc); }
RHIPipelineCache RHID3D11Device::createPipelineCache(const RHIPipelineCacheDesc& desc) { return impl_->renderer.createPipelineCache(desc); }
RHIPipeline RHID3D11Device::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) { return impl_->renderer.createGraphicsPipeline(desc); }
RHIPipeline RHID3D11Device::createComputePipeline(const RHIComputePipelineDesc& desc) { return impl_->renderer.createComputePipeline(desc); }
void RHID3D11Device::destroy(RHIPipelineLayout handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D11Device::destroy(RHIPipelineCache handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D11Device::destroy(RHIPipeline handle) noexcept { impl_->renderer.destroy(handle); }
} // namespace rhi