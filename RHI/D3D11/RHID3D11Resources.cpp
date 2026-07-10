#include "RHID3D11Private.hpp"

namespace rhi {
RHIBuffer RHID3D11Device::createBuffer(const RHIBufferDesc& desc) { return impl_->renderer.createBuffer(desc); }
RHITexture RHID3D11Device::createTexture(const RHITextureDesc& desc) { return impl_->renderer.createTexture(desc); }
RHITextureView RHID3D11Device::createTextureView(const RHITextureViewDesc& desc) { return impl_->renderer.createTextureView(desc); }
RHISampler RHID3D11Device::createSampler(const RHISamplerDesc& desc) { return impl_->renderer.createSampler(desc); }
RHIShader RHID3D11Device::createShaderModule(const RHIShaderDesc& desc) { return impl_->renderer.createShaderModule(desc); }
RHIBindGroupLayout RHID3D11Device::createBindGroupLayout(const RHIBindGroupLayoutDesc& desc) { return impl_->renderer.createBindGroupLayout(desc); }
RHIBindGroup RHID3D11Device::createBindGroup(const RHIBindGroupDesc& desc) { return impl_->renderer.createBindGroup(desc); }
void RHID3D11Device::destroy(RHIBuffer handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D11Device::destroy(RHITexture handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D11Device::destroy(RHITextureView handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D11Device::destroy(RHISampler handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D11Device::destroy(RHIShader handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D11Device::destroy(RHIBindGroupLayout handle) noexcept { impl_->renderer.destroy(handle); }
void RHID3D11Device::destroy(RHIBindGroup handle) noexcept { impl_->renderer.destroy(handle); }
} // namespace rhi