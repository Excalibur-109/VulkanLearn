#include "RHIVulkanPrivate.hpp"

namespace rhi {
RHIBuffer RHIVulkanDevice::createBuffer(const RHIBufferDesc& desc) { return impl_->backend.createBuffer(desc); }
RHITexture RHIVulkanDevice::createTexture(const RHITextureDesc& desc) { return impl_->backend.createTexture(desc); }
RHITextureView RHIVulkanDevice::createTextureView(const RHITextureViewDesc& desc) { return impl_->backend.createTextureView(desc); }
RHISampler RHIVulkanDevice::createSampler(const RHISamplerDesc& desc) { return impl_->backend.createSampler(desc); }
RHIShader RHIVulkanDevice::createShaderModule(const RHIShaderDesc& desc) { return impl_->backend.createShaderModule(desc); }
RHIBindGroupLayout RHIVulkanDevice::createBindGroupLayout(const RHIBindGroupLayoutDesc& desc) { return impl_->backend.createBindGroupLayout(desc); }
RHIBindGroup RHIVulkanDevice::createBindGroup(const RHIBindGroupDesc& desc) { return impl_->backend.createBindGroup(desc); }
void RHIVulkanDevice::destroy(RHIBuffer handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHITexture handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHITextureView handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHISampler handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHIShader handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHIBindGroupLayout handle) noexcept { impl_->backend.destroy(handle); }
void RHIVulkanDevice::destroy(RHIBindGroup handle) noexcept { impl_->backend.destroy(handle); }
} // namespace rhi