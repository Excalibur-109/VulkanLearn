#include "RHID3D12Private.hpp"

namespace rhi {
RHIBuffer RHID3D12Device::createBuffer(const RHIBufferDesc& desc) { return impl_->renderer.createBuffer(desc); }
RHITexture RHID3D12Device::createTexture(const RHITextureDesc& desc) { return impl_->renderer.createTexture(desc); }
RHITextureView RHID3D12Device::createTextureView(const RHITextureViewDesc& desc) { return impl_->renderer.createTextureView(desc); }
RHISampler RHID3D12Device::createSampler(const RHISamplerDesc& desc) { return impl_->renderer.createSampler(desc); }
RHIShader RHID3D12Device::createShaderModule(const RHIShaderDesc& desc) { return impl_->renderer.createShaderModule(desc); }
RHIBindGroupLayout RHID3D12Device::createBindGroupLayout(const RHIBindGroupLayoutDesc& desc) { return impl_->renderer.createBindGroupLayout(desc); }
RHIBindGroup RHID3D12Device::createBindGroup(const RHIBindGroupDesc& desc) { return impl_->renderer.createBindGroup(desc); }
void RHID3D12Device::destroy(RHIBuffer handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHITexture handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHITextureView handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHISampler handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHIShader handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHIBindGroupLayout handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D12Device::destroy(RHIBindGroup handle) noexcept { impl_->renderer.destroy(handle); }
} // namespace rhi