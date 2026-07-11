#include "RHIVulkanPrivate.hpp"

namespace rhi {
RHIPipelineLayout RHIVulkanDevice::createPipelineLayout(const RHIPipelineLayoutDesc& desc) { return impl_->backend.createPipelineLayout(desc); }
RHIPipelineCache RHIVulkanDevice::createPipelineCache(const RHIPipelineCacheDesc& desc) { return impl_->backend.createPipelineCache(desc); }
RHIPipeline RHIVulkanDevice::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) { return impl_->backend.createGraphicsPipeline(desc); }
RHIPipeline RHIVulkanDevice::createComputePipeline(const RHIComputePipelineDesc& desc) { return impl_->backend.createComputePipeline(desc); }
void RHIVulkanDevice::destroy(RHIPipelineLayout handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHIPipelineCache handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHIPipeline handle) noexcept { impl_->backend.destroy(handle); }
} // namespace rhi