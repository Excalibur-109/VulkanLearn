#include "RHIVulkanPrivate.hpp"

namespace rhi {
RHIBuffer RHIVulkanDevice::createBuffer(const RHIBufferDesc& desc) { return impl_->renderer.createBuffer(desc); }
RHITexture RHIVulkanDevice::createTexture(const RHITextureDesc& desc) { return impl_->renderer.createTexture(desc); }
RHITextureView RHIVulkanDevice::createTextureView(const RHITextureViewDesc& desc) { return impl_->renderer.createTextureView(desc); }
RHISampler RHIVulkanDevice::createSampler(const RHISamplerDesc& desc) { return impl_->renderer.createSampler(desc); }
RHIShader RHIVulkanDevice::createShaderModule(const RHIShaderDesc& desc) { return impl_->renderer.createShaderModule(desc); }
RHIBindGroupLayout RHIVulkanDevice::createBindGroupLayout(const RHIBindGroupLayoutDesc& desc) { return impl_->renderer.createBindGroupLayout(desc); }
RHIBindGroup RHIVulkanDevice::createBindGroup(const RHIBindGroupDesc& desc) { return impl_->renderer.createBindGroup(desc); }
void RHIVulkanDevice::destroy(RHIBuffer handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHITexture handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHITextureView handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHISampler handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHIShader handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHIBindGroupLayout handle) noexcept { impl_->renderer.destroy(handle); }
void RHIVulkanDevice::destroy(RHIBindGroup handle) noexcept { impl_->renderer.destroy(handle); }
} // namespace rhi