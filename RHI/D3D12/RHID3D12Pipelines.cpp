#include "RHID3D12Private.hpp"

namespace rhi {
RHIPipelineLayout RHID3D12Device::createPipelineLayout(const RHIPipelineLayoutDesc& desc) { return impl_->renderer.createPipelineLayout(desc); }
RHIPipelineCache RHID3D12Device::createPipelineCache(const RHIPipelineCacheDesc& desc) { return impl_->renderer.createPipelineCache(desc); }
RHIPipeline RHID3D12Device::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) { return impl_->renderer.createGraphicsPipeline(desc); }
RHIPipeline RHID3D12Device::createComputePipeline(const RHIComputePipelineDesc& desc) { return impl_->renderer.createComputePipeline(desc); }
void RHID3D12Device::destroy(RHIPipelineLayout handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHIPipelineCache handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHIPipeline handle) noexcept { impl_->renderer.destroy(handle); }
} // namespace rhi