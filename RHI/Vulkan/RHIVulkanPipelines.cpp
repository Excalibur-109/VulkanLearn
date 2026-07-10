#include "RHIVulkanPrivate.hpp"

namespace rhi {
RHIPipelineLayout RHIVulkanDevice::createPipelineLayout(const RHIPipelineLayoutDesc& desc) { return impl_->renderer.createPipelineLayout(desc); }
RHIPipelineCache RHIVulkanDevice::createPipelineCache(const RHIPipelineCacheDesc& desc) { return impl_->renderer.createPipelineCache(desc); }
RHIPipeline RHIVulkanDevice::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) { return impl_->renderer.createGraphicsPipeline(desc); }
RHIPipeline RHIVulkanDevice::createComputePipeline(const RHIComputePipelineDesc& desc) { return impl_->renderer.createComputePipeline(desc); }
void RHIVulkanDevice::destroy(RHIPipelineLayout handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHIPipelineCache handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHIPipeline handle) noexcept { impl_->renderer.destroy(handle); }
} // namespace rhi