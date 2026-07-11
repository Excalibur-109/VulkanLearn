#include "RHID3D12Private.hpp"

namespace rhi {
RHIPipelineLayout RHID3D12Device::createPipelineLayout(const RHIPipelineLayoutDesc& desc) { return impl_->backend.createPipelineLayout(desc); }
RHIPipelineCache RHID3D12Device::createPipelineCache(const RHIPipelineCacheDesc& desc) { return impl_->backend.createPipelineCache(desc); }
RHIPipeline RHID3D12Device::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) { return impl_->backend.createGraphicsPipeline(desc); }
RHIPipeline RHID3D12Device::createComputePipeline(const RHIComputePipelineDesc& desc) { return impl_->backend.createComputePipeline(desc); }
void RHID3D12Device::destroy(RHIPipelineLayout handle) noexcept { impl_->backend.destroy(handle); }
void RHID3D12Device::destroy(RHIPipelineCache handle) noexcept { impl_->backend.destroy(handle); }
void RHID3D12Device::destroy(RHIPipeline handle) noexcept { impl_->backend.destroy(handle); }
} // namespace rhi