#pragma once

#include "RHID3D12.hpp"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace rhi {

using Microsoft::WRL::ComPtr;

// D3D12 资源句柄沿用现有后端的 1-based index 规则：0 是无效句柄，真实资源从 1 开始。
// 这样公共层 RHIHandle<T> 不需要知道 ID3D12Resource*，也不会把不同资源类型误传。
template <typename HandleT, typename ResourceT>
static HandleT makeRenderHandle(std::vector<ResourceT>& resources, ResourceT&& resource) {
    resources.push_back(std::move(resource));
    return HandleT(static_cast<u64>(resources.size()));
}

template <typename ResourceT, typename HandleT>
static ResourceT* getRenderResource(std::vector<ResourceT>& resources, HandleT handle) {
    if (!handle || handle.value == 0 || handle.value > resources.size()) {
        return nullptr;
    }
    return &resources[static_cast<size_t>(handle.value - 1)];
}

template <typename ResourceT, typename HandleT>
static const ResourceT* getRenderResource(const std::vector<ResourceT>& resources, HandleT handle) {
    if (!handle || handle.value == 0 || handle.value > resources.size()) {
        return nullptr;
    }
    return &resources[static_cast<size_t>(handle.value - 1)];
}

static void throwIfFailed(HRESULT hr, const char* message) {
    if (FAILED(hr)) {
        throw std::runtime_error(message);
    }
}

static std::wstring toWideString(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (size <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring result(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, result.data(), size);
    return result;
}

static std::string toUtf8String(const wchar_t* text) {
    if (text == nullptr || text[0] == L'\0') {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), size, nullptr, nullptr);
    return result;
}

template <typename T>
static T alignUp(T value, T alignment) {
    return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

// 统一 RHIFormat 到 DXGI_FORMAT。D3D11/D3D12 同属 DXGI 格式体系，所以大部分映射一致。
// 这里保留对齐后的 return，方便横向检查缺失格式。
static DXGI_FORMAT toDxgiFormat(RHIFormat format) {
    switch (format) {
    case RHIFormat::Undefined:         return DXGI_FORMAT_UNKNOWN;
    case RHIFormat::R8_UNorm:          return DXGI_FORMAT_R8_UNORM;
    case RHIFormat::R8_SNorm:          return DXGI_FORMAT_R8_SNORM;
    case RHIFormat::R8_UInt:           return DXGI_FORMAT_R8_UINT;
    case RHIFormat::R8_SInt:           return DXGI_FORMAT_R8_SINT;
    case RHIFormat::RG8_UNorm:         return DXGI_FORMAT_R8G8_UNORM;
    case RHIFormat::RG8_SNorm:         return DXGI_FORMAT_R8G8_SNORM;
    case RHIFormat::RG8_UInt:          return DXGI_FORMAT_R8G8_UINT;
    case RHIFormat::RG8_SInt:          return DXGI_FORMAT_R8G8_SINT;
    case RHIFormat::RGBA8_UNorm:       return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::RGBA8_SNorm:       return DXGI_FORMAT_R8G8B8A8_SNORM;
    case RHIFormat::RGBA8_UInt:        return DXGI_FORMAT_R8G8B8A8_UINT;
    case RHIFormat::RGBA8_SInt:        return DXGI_FORMAT_R8G8B8A8_SINT;
    case RHIFormat::RGBA8_SRGB:        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case RHIFormat::BGRA8_UNorm:       return DXGI_FORMAT_B8G8R8A8_UNORM;
    case RHIFormat::BGRA8_SRGB:        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case RHIFormat::R16_UNorm:         return DXGI_FORMAT_R16_UNORM;
    case RHIFormat::R16_SNorm:         return DXGI_FORMAT_R16_SNORM;
    case RHIFormat::R16_UInt:          return DXGI_FORMAT_R16_UINT;
    case RHIFormat::R16_SInt:          return DXGI_FORMAT_R16_SINT;
    case RHIFormat::R16_Float:         return DXGI_FORMAT_R16_FLOAT;
    case RHIFormat::RG16_UNorm:        return DXGI_FORMAT_R16G16_UNORM;
    case RHIFormat::RG16_SNorm:        return DXGI_FORMAT_R16G16_SNORM;
    case RHIFormat::RG16_UInt:         return DXGI_FORMAT_R16G16_UINT;
    case RHIFormat::RG16_SInt:         return DXGI_FORMAT_R16G16_SINT;
    case RHIFormat::RG16_Float:        return DXGI_FORMAT_R16G16_FLOAT;
    case RHIFormat::RGBA16_UNorm:      return DXGI_FORMAT_R16G16B16A16_UNORM;
    case RHIFormat::RGBA16_SNorm:      return DXGI_FORMAT_R16G16B16A16_SNORM;
    case RHIFormat::RGBA16_UInt:       return DXGI_FORMAT_R16G16B16A16_UINT;
    case RHIFormat::RGBA16_SInt:       return DXGI_FORMAT_R16G16B16A16_SINT;
    case RHIFormat::RGBA16_Float:      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case RHIFormat::R32_UInt:          return DXGI_FORMAT_R32_UINT;
    case RHIFormat::R32_SInt:          return DXGI_FORMAT_R32_SINT;
    case RHIFormat::R32_Float:         return DXGI_FORMAT_R32_FLOAT;
    case RHIFormat::RG32_UInt:         return DXGI_FORMAT_R32G32_UINT;
    case RHIFormat::RG32_SInt:         return DXGI_FORMAT_R32G32_SINT;
    case RHIFormat::RG32_Float:        return DXGI_FORMAT_R32G32_FLOAT;
    case RHIFormat::RGB32_UInt:        return DXGI_FORMAT_R32G32B32_UINT;
    case RHIFormat::RGB32_SInt:        return DXGI_FORMAT_R32G32B32_SINT;
    case RHIFormat::RGB32_Float:       return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHIFormat::RGBA32_UInt:       return DXGI_FORMAT_R32G32B32A32_UINT;
    case RHIFormat::RGBA32_SInt:       return DXGI_FORMAT_R32G32B32A32_SINT;
    case RHIFormat::RGBA32_Float:      return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHIFormat::RGB10A2_UNorm:     return DXGI_FORMAT_R10G10B10A2_UNORM;
    case RHIFormat::R11G11B10_Float:   return DXGI_FORMAT_R11G11B10_FLOAT;
    case RHIFormat::D16_UNorm:         return DXGI_FORMAT_D16_UNORM;
    case RHIFormat::D24_UNorm:         return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case RHIFormat::S8_UInt:           return DXGI_FORMAT_UNKNOWN;
    case RHIFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case RHIFormat::D32_Float:         return DXGI_FORMAT_D32_FLOAT;
    case RHIFormat::D32_Float_S8_UInt: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case RHIFormat::BC1RGBA_UNorm:     return DXGI_FORMAT_BC1_UNORM;
    case RHIFormat::BC1RGBA_SRGB:      return DXGI_FORMAT_BC1_UNORM_SRGB;
    case RHIFormat::BC3RGBA_UNorm:     return DXGI_FORMAT_BC3_UNORM;
    case RHIFormat::BC3RGBA_SRGB:      return DXGI_FORMAT_BC3_UNORM_SRGB;
    case RHIFormat::BC5RG_UNorm:       return DXGI_FORMAT_BC5_UNORM;
    case RHIFormat::BC5RG_SNorm:       return DXGI_FORMAT_BC5_SNORM;
    case RHIFormat::BC7RGBA_UNorm:     return DXGI_FORMAT_BC7_UNORM;
    case RHIFormat::BC7RGBA_SRGB:      return DXGI_FORMAT_BC7_UNORM_SRGB;
    case RHIFormat::ETC2RGB8_UNorm:
    case RHIFormat::ETC2RGB8_SRGB:
    case RHIFormat::ETC2RGBA8_UNorm:
    case RHIFormat::ETC2RGBA8_SRGB:
    case RHIFormat::ASTC4x4_UNorm:
    case RHIFormat::ASTC4x4_SRGB:
    case RHIFormat::ASTC8x8_UNorm:
    case RHIFormat::ASTC8x8_SRGB:
        return DXGI_FORMAT_UNKNOWN;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static RHIFormat fromDxgiFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8_UNORM:             return RHIFormat::R8_UNorm;
    case DXGI_FORMAT_R8_SNORM:             return RHIFormat::R8_SNorm;
    case DXGI_FORMAT_R8_UINT:              return RHIFormat::R8_UInt;
    case DXGI_FORMAT_R8_SINT:              return RHIFormat::R8_SInt;
    case DXGI_FORMAT_R8G8_UNORM:           return RHIFormat::RG8_UNorm;
    case DXGI_FORMAT_R8G8_SNORM:           return RHIFormat::RG8_SNorm;
    case DXGI_FORMAT_R8G8_UINT:            return RHIFormat::RG8_UInt;
    case DXGI_FORMAT_R8G8_SINT:            return RHIFormat::RG8_SInt;
    case DXGI_FORMAT_R8G8B8A8_UNORM:       return RHIFormat::RGBA8_UNorm;
    case DXGI_FORMAT_R8G8B8A8_SNORM:       return RHIFormat::RGBA8_SNorm;
    case DXGI_FORMAT_R8G8B8A8_UINT:        return RHIFormat::RGBA8_UInt;
    case DXGI_FORMAT_R8G8B8A8_SINT:        return RHIFormat::RGBA8_SInt;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:  return RHIFormat::RGBA8_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM:       return RHIFormat::BGRA8_UNorm;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:  return RHIFormat::BGRA8_SRGB;
    case DXGI_FORMAT_R16_UNORM:            return RHIFormat::R16_UNorm;
    case DXGI_FORMAT_R16_SNORM:            return RHIFormat::R16_SNorm;
    case DXGI_FORMAT_R16_UINT:             return RHIFormat::R16_UInt;
    case DXGI_FORMAT_R16_SINT:             return RHIFormat::R16_SInt;
    case DXGI_FORMAT_R16_FLOAT:            return RHIFormat::R16_Float;
    case DXGI_FORMAT_R16G16_UNORM:         return RHIFormat::RG16_UNorm;
    case DXGI_FORMAT_R16G16_SNORM:         return RHIFormat::RG16_SNorm;
    case DXGI_FORMAT_R16G16_UINT:          return RHIFormat::RG16_UInt;
    case DXGI_FORMAT_R16G16_SINT:          return RHIFormat::RG16_SInt;
    case DXGI_FORMAT_R16G16_FLOAT:         return RHIFormat::RG16_Float;
    case DXGI_FORMAT_R16G16B16A16_UNORM:   return RHIFormat::RGBA16_UNorm;
    case DXGI_FORMAT_R16G16B16A16_SNORM:   return RHIFormat::RGBA16_SNorm;
    case DXGI_FORMAT_R16G16B16A16_UINT:    return RHIFormat::RGBA16_UInt;
    case DXGI_FORMAT_R16G16B16A16_SINT:    return RHIFormat::RGBA16_SInt;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:   return RHIFormat::RGBA16_Float;
    case DXGI_FORMAT_R32_UINT:             return RHIFormat::R32_UInt;
    case DXGI_FORMAT_R32_SINT:             return RHIFormat::R32_SInt;
    case DXGI_FORMAT_R32_FLOAT:            return RHIFormat::R32_Float;
    case DXGI_FORMAT_R32G32_UINT:          return RHIFormat::RG32_UInt;
    case DXGI_FORMAT_R32G32_SINT:          return RHIFormat::RG32_SInt;
    case DXGI_FORMAT_R32G32_FLOAT:         return RHIFormat::RG32_Float;
    case DXGI_FORMAT_R32G32B32_UINT:       return RHIFormat::RGB32_UInt;
    case DXGI_FORMAT_R32G32B32_SINT:       return RHIFormat::RGB32_SInt;
    case DXGI_FORMAT_R32G32B32_FLOAT:      return RHIFormat::RGB32_Float;
    case DXGI_FORMAT_R32G32B32A32_UINT:    return RHIFormat::RGBA32_UInt;
    case DXGI_FORMAT_R32G32B32A32_SINT:    return RHIFormat::RGBA32_SInt;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:   return RHIFormat::RGBA32_Float;
    case DXGI_FORMAT_R10G10B10A2_UNORM:    return RHIFormat::RGB10A2_UNorm;
    case DXGI_FORMAT_R11G11B10_FLOAT:      return RHIFormat::R11G11B10_Float;
    case DXGI_FORMAT_D16_UNORM:            return RHIFormat::D16_UNorm;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:    return RHIFormat::D24_UNorm_S8_UInt;
    case DXGI_FORMAT_D32_FLOAT:            return RHIFormat::D32_Float;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return RHIFormat::D32_Float_S8_UInt;
    case DXGI_FORMAT_BC1_UNORM:            return RHIFormat::BC1RGBA_UNorm;
    case DXGI_FORMAT_BC1_UNORM_SRGB:       return RHIFormat::BC1RGBA_SRGB;
    case DXGI_FORMAT_BC3_UNORM:            return RHIFormat::BC3RGBA_UNorm;
    case DXGI_FORMAT_BC3_UNORM_SRGB:       return RHIFormat::BC3RGBA_SRGB;
    case DXGI_FORMAT_BC5_UNORM:            return RHIFormat::BC5RG_UNorm;
    case DXGI_FORMAT_BC5_SNORM:            return RHIFormat::BC5RG_SNorm;
    case DXGI_FORMAT_BC7_UNORM:            return RHIFormat::BC7RGBA_UNorm;
    case DXGI_FORMAT_BC7_UNORM_SRGB:       return RHIFormat::BC7RGBA_SRGB;
    default:                               return RHIFormat::Undefined;
    }
}

static DXGI_FORMAT toTypelessDepthFormat(RHIFormat format) {
    switch (format) {
    case RHIFormat::D16_UNorm:         return DXGI_FORMAT_R16_TYPELESS;
    case RHIFormat::D24_UNorm:
    case RHIFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24G8_TYPELESS;
    case RHIFormat::D32_Float:         return DXGI_FORMAT_R32_TYPELESS;
    case RHIFormat::D32_Float_S8_UInt: return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:                        return toDxgiFormat(format);
    }
}

static DXGI_FORMAT toSrvFormat(RHIFormat format) {
    switch (format) {
    case RHIFormat::D16_UNorm:         return DXGI_FORMAT_R16_UNORM;
    case RHIFormat::D24_UNorm:
    case RHIFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case RHIFormat::D32_Float:         return DXGI_FORMAT_R32_FLOAT;
    case RHIFormat::D32_Float_S8_UInt: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:                        return toDxgiFormat(format);
    }
}

static DXGI_FORMAT toDsvFormat(RHIFormat format) {
    switch (format) {
    case RHIFormat::D16_UNorm:         return DXGI_FORMAT_D16_UNORM;
    case RHIFormat::D24_UNorm:
    case RHIFormat::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case RHIFormat::D32_Float:         return DXGI_FORMAT_D32_FLOAT;
    case RHIFormat::D32_Float_S8_UInt: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

static DXGI_FORMAT toSwapchainFormat(RHIFormat format) {
    switch (format) {
    case RHIFormat::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHIFormat::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default: {
        const DXGI_FORMAT dxgi = toDxgiFormat(format);
        return dxgi == DXGI_FORMAT_UNKNOWN ? DXGI_FORMAT_B8G8R8A8_UNORM : dxgi;
    }
    }
}

// D3D12 的 resource state 是显式同步的核心。RHIDefinitions.hpp 的 RHIResourceState 是“意图”，
// 后端在 barrier/创建资源时把它翻译成 D3D12_RESOURCE_STATES。
static D3D12_RESOURCE_STATES toD3D12ResourceStates(RHIResourceState state) {
    switch (state) {
    case RHIResourceState::Undefined:                  return D3D12_RESOURCE_STATE_COMMON;
    case RHIResourceState::Common:                     return D3D12_RESOURCE_STATE_COMMON;
    case RHIResourceState::CopySource:                 return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case RHIResourceState::CopyDestination:            return D3D12_RESOURCE_STATE_COPY_DEST;
    case RHIResourceState::VertexBuffer:               return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case RHIResourceState::IndexBuffer:                return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case RHIResourceState::ConstantBuffer:             return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case RHIResourceState::ShaderRead:                 return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case RHIResourceState::ShaderWrite:                return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case RHIResourceState::RenderTarget:               return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case RHIResourceState::DepthRead:                  return D3D12_RESOURCE_STATE_DEPTH_READ;
    case RHIResourceState::DepthWrite:                 return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case RHIResourceState::ResolveSource:              return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    case RHIResourceState::ResolveDestination:         return D3D12_RESOURCE_STATE_RESOLVE_DEST;
    case RHIResourceState::Present:                    return D3D12_RESOURCE_STATE_PRESENT;
    case RHIResourceState::IndirectArgument:           return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case RHIResourceState::AccelerationStructureRead:  return D3D12_RESOURCE_STATE_COMMON;
    case RHIResourceState::AccelerationStructureWrite: return D3D12_RESOURCE_STATE_COMMON;
    case RHIResourceState::ShadingRateTexture:         return D3D12_RESOURCE_STATE_COMMON;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

static D3D12_HEAP_TYPE toD3D12HeapType(RHIMemoryUsage usage) {
    switch (usage) {
    case RHIMemoryUsage::GpuOnly:  return D3D12_HEAP_TYPE_DEFAULT;
    case RHIMemoryUsage::CpuToGpu: return D3D12_HEAP_TYPE_UPLOAD;
    case RHIMemoryUsage::GpuToCpu: return D3D12_HEAP_TYPE_READBACK;
    case RHIMemoryUsage::CpuOnly:  return D3D12_HEAP_TYPE_UPLOAD;
    }
    return D3D12_HEAP_TYPE_DEFAULT;
}

static D3D12_RESOURCE_STATES initialBufferState(RHIMemoryUsage usage, RHIResourceState requested) {
    switch (usage) {
    case RHIMemoryUsage::CpuToGpu:
    case RHIMemoryUsage::CpuOnly:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case RHIMemoryUsage::GpuToCpu:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case RHIMemoryUsage::GpuOnly:
        return requested == RHIResourceState::Undefined ? D3D12_RESOURCE_STATE_COMMON : toD3D12ResourceStates(requested);
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

static D3D12_RESOURCE_FLAGS toBufferResourceFlags(RHIBufferUsage usage, RHIMemoryUsage memoryUsage) {
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (memoryUsage == RHIMemoryUsage::GpuOnly && RHIHasAny(usage, RHIBufferUsage::Storage)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    return flags;
}

static D3D12_RESOURCE_FLAGS toTextureResourceFlags(RHITextureUsage usage) {
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (RHIHasAny(usage, RHITextureUsage::ColorAttachment) || RHIHasAny(usage, RHITextureUsage::Present)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (RHIHasAny(usage, RHITextureUsage::DepthStencilAttachment)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (RHIHasAny(usage, RHITextureUsage::Storage)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    return flags;
}

static D3D12_FILTER toD3D12Filter(const RHISamplerDesc& desc) {
    if (desc.enableAnisotropy) {
        return desc.enableCompare ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
    }

    const bool minLinear = desc.minFilter == RHIFilterMode::Linear;
    const bool magLinear = desc.magFilter == RHIFilterMode::Linear;
    const bool mipLinear = desc.mipmapMode == RHIMipmapMode::Linear;
    if (desc.enableCompare) {
        if (minLinear && magLinear && mipLinear) return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        if (minLinear && magLinear)              return D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        if (minLinear && mipLinear)              return D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        if (magLinear && mipLinear)              return D3D12_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR;
        if (minLinear)                           return D3D12_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT;
        if (magLinear)                           return D3D12_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT;
        if (mipLinear)                           return D3D12_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR;
        return D3D12_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    }

    if (minLinear && magLinear && mipLinear) return D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    if (minLinear && magLinear)              return D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (minLinear && mipLinear)              return D3D12_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    if (magLinear && mipLinear)              return D3D12_FILTER_MIN_POINT_MAG_MIP_LINEAR;
    if (minLinear)                           return D3D12_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    if (magLinear)                           return D3D12_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    if (mipLinear)                           return D3D12_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    return D3D12_FILTER_MIN_MAG_MIP_POINT;
}

static D3D12_TEXTURE_ADDRESS_MODE toD3D12AddressMode(RHIAddressMode mode) {
    switch (mode) {
    case RHIAddressMode::Repeat:         return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case RHIAddressMode::MirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case RHIAddressMode::ClampToEdge:    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case RHIAddressMode::ClampToBorder:  return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

static D3D12_COMPARISON_FUNC toD3D12Compare(RHICompareOp op) {
    switch (op) {
    case RHICompareOp::Never:          return D3D12_COMPARISON_FUNC_NEVER;
    case RHICompareOp::Less:           return D3D12_COMPARISON_FUNC_LESS;
    case RHICompareOp::Equal:          return D3D12_COMPARISON_FUNC_EQUAL;
    case RHICompareOp::LessOrEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case RHICompareOp::Greater:        return D3D12_COMPARISON_FUNC_GREATER;
    case RHICompareOp::NotEqual:       return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case RHICompareOp::GreaterOrEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case RHICompareOp::Always:         return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_ALWAYS;
}

static DXGI_FORMAT toD3D12VertexFormat(RHIVertexFormat format) {
    switch (format) {
    case RHIVertexFormat::Float32:   return DXGI_FORMAT_R32_FLOAT;
    case RHIVertexFormat::Float32x2: return DXGI_FORMAT_R32G32_FLOAT;
    case RHIVertexFormat::Float32x3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case RHIVertexFormat::Float32x4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case RHIVertexFormat::UInt32:    return DXGI_FORMAT_R32_UINT;
    case RHIVertexFormat::UInt32x2:  return DXGI_FORMAT_R32G32_UINT;
    case RHIVertexFormat::UInt32x3:  return DXGI_FORMAT_R32G32B32_UINT;
    case RHIVertexFormat::UInt32x4:  return DXGI_FORMAT_R32G32B32A32_UINT;
    case RHIVertexFormat::SInt32:    return DXGI_FORMAT_R32_SINT;
    case RHIVertexFormat::SInt32x2:  return DXGI_FORMAT_R32G32_SINT;
    case RHIVertexFormat::SInt32x3:  return DXGI_FORMAT_R32G32B32_SINT;
    case RHIVertexFormat::SInt32x4:  return DXGI_FORMAT_R32G32B32A32_SINT;
    case RHIVertexFormat::UNorm8x4:  return DXGI_FORMAT_R8G8B8A8_UNORM;
    case RHIVertexFormat::SNorm8x4:  return DXGI_FORMAT_R8G8B8A8_SNORM;
    case RHIVertexFormat::UInt16x2:  return DXGI_FORMAT_R16G16_UINT;
    case RHIVertexFormat::UInt16x4:  return DXGI_FORMAT_R16G16B16A16_UINT;
    case RHIVertexFormat::SInt16x2:  return DXGI_FORMAT_R16G16_SINT;
    case RHIVertexFormat::SInt16x4:  return DXGI_FORMAT_R16G16B16A16_SINT;
    case RHIVertexFormat::UNorm16x2: return DXGI_FORMAT_R16G16_UNORM;
    case RHIVertexFormat::UNorm16x4: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case RHIVertexFormat::SNorm16x2: return DXGI_FORMAT_R16G16_SNORM;
    case RHIVertexFormat::SNorm16x4: return DXGI_FORMAT_R16G16B16A16_SNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static D3D_PRIMITIVE_TOPOLOGY toD3D12Topology(const RHIInputAssemblyState& state) {
    switch (state.topology) {
    case RHIPrimitiveTopology::PointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case RHIPrimitiveTopology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case RHIPrimitiveTopology::LineStrip:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case RHIPrimitiveTopology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case RHIPrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case RHIPrimitiveTopology::PatchList: {
        const u32 points = std::clamp(state.patchControlPoints == 0 ? 3u : state.patchControlPoints, 1u, 32u);
        return static_cast<D3D_PRIMITIVE_TOPOLOGY>(D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + points - 1);
    }
    }
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3D12TopologyType(const RHIInputAssemblyState& state) {
    switch (state.topology) {
    case RHIPrimitiveTopology::PointList:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case RHIPrimitiveTopology::LineList:
    case RHIPrimitiveTopology::LineStrip:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case RHIPrimitiveTopology::TriangleList:
    case RHIPrimitiveTopology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case RHIPrimitiveTopology::PatchList:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

static D3D12_STENCIL_OP toD3D12StencilOp(RHIStencilOp op) {
    switch (op) {
    case RHIStencilOp::Keep:           return D3D12_STENCIL_OP_KEEP;
    case RHIStencilOp::Zero:           return D3D12_STENCIL_OP_ZERO;
    case RHIStencilOp::Replace:        return D3D12_STENCIL_OP_REPLACE;
    case RHIStencilOp::IncrementClamp: return D3D12_STENCIL_OP_INCR_SAT;
    case RHIStencilOp::DecrementClamp: return D3D12_STENCIL_OP_DECR_SAT;
    case RHIStencilOp::Invert:         return D3D12_STENCIL_OP_INVERT;
    case RHIStencilOp::IncrementWrap:  return D3D12_STENCIL_OP_INCR;
    case RHIStencilOp::DecrementWrap:  return D3D12_STENCIL_OP_DECR;
    }
    return D3D12_STENCIL_OP_KEEP;
}

static D3D12_BLEND toD3D12Blend(RHIBlendFactor factor) {
    switch (factor) {
    case RHIBlendFactor::Zero:                     return D3D12_BLEND_ZERO;
    case RHIBlendFactor::One:                      return D3D12_BLEND_ONE;
    case RHIBlendFactor::SourceColor:              return D3D12_BLEND_SRC_COLOR;
    case RHIBlendFactor::OneMinusSourceColor:      return D3D12_BLEND_INV_SRC_COLOR;
    case RHIBlendFactor::DestinationColor:         return D3D12_BLEND_DEST_COLOR;
    case RHIBlendFactor::OneMinusDestinationColor: return D3D12_BLEND_INV_DEST_COLOR;
    case RHIBlendFactor::SourceAlpha:              return D3D12_BLEND_SRC_ALPHA;
    case RHIBlendFactor::OneMinusSourceAlpha:      return D3D12_BLEND_INV_SRC_ALPHA;
    case RHIBlendFactor::DestinationAlpha:         return D3D12_BLEND_DEST_ALPHA;
    case RHIBlendFactor::OneMinusDestinationAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
    case RHIBlendFactor::ConstantColor:
    case RHIBlendFactor::ConstantAlpha:            return D3D12_BLEND_BLEND_FACTOR;
    case RHIBlendFactor::OneMinusConstantColor:
    case RHIBlendFactor::OneMinusConstantAlpha:    return D3D12_BLEND_INV_BLEND_FACTOR;
    }
    return D3D12_BLEND_ONE;
}

static D3D12_BLEND_OP toD3D12BlendOp(RHIBlendOp op) {
    switch (op) {
    case RHIBlendOp::Add:             return D3D12_BLEND_OP_ADD;
    case RHIBlendOp::Subtract:        return D3D12_BLEND_OP_SUBTRACT;
    case RHIBlendOp::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
    case RHIBlendOp::Min:             return D3D12_BLEND_OP_MIN;
    case RHIBlendOp::Max:             return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}

static UINT8 toD3D12ColorWriteMask(RHIColorWriteMask mask) {
    UINT8 flags = 0;
    if (RHIHasAny(mask, RHIColorWriteMask::R)) flags |= D3D12_COLOR_WRITE_ENABLE_RED;
    if (RHIHasAny(mask, RHIColorWriteMask::G)) flags |= D3D12_COLOR_WRITE_ENABLE_GREEN;
    if (RHIHasAny(mask, RHIColorWriteMask::B)) flags |= D3D12_COLOR_WRITE_ENABLE_BLUE;
    if (RHIHasAny(mask, RHIColorWriteMask::A)) flags |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
    return flags;
}

static D3D12_SHADER_VISIBILITY toD3D12ShaderVisibility(RHIShaderStage visibility) {
    if (visibility == RHIShaderStage::Vertex)         return D3D12_SHADER_VISIBILITY_VERTEX;
    if (visibility == RHIShaderStage::TessControl)    return D3D12_SHADER_VISIBILITY_HULL;
    if (visibility == RHIShaderStage::TessEvaluation) return D3D12_SHADER_VISIBILITY_DOMAIN;
    if (visibility == RHIShaderStage::Geometry)       return D3D12_SHADER_VISIBILITY_GEOMETRY;
    if (visibility == RHIShaderStage::Fragment)       return D3D12_SHADER_VISIBILITY_PIXEL;
    return D3D12_SHADER_VISIBILITY_ALL;
}

static UINT formatBytesPerBlock(RHIFormat format) {
    switch (format) {
    case RHIFormat::R8_UNorm:
    case RHIFormat::R8_SNorm:
    case RHIFormat::R8_UInt:
    case RHIFormat::R8_SInt:
        return 1;
    case RHIFormat::RG8_UNorm:
    case RHIFormat::RG8_SNorm:
    case RHIFormat::RG8_UInt:
    case RHIFormat::RG8_SInt:
    case RHIFormat::R16_UNorm:
    case RHIFormat::R16_SNorm:
    case RHIFormat::R16_UInt:
    case RHIFormat::R16_SInt:
    case RHIFormat::R16_Float:
    case RHIFormat::D16_UNorm:
        return 2;
    case RHIFormat::RGBA8_UNorm:
    case RHIFormat::RGBA8_SNorm:
    case RHIFormat::RGBA8_UInt:
    case RHIFormat::RGBA8_SInt:
    case RHIFormat::RGBA8_SRGB:
    case RHIFormat::BGRA8_UNorm:
    case RHIFormat::BGRA8_SRGB:
    case RHIFormat::RG16_UNorm:
    case RHIFormat::RG16_SNorm:
    case RHIFormat::RG16_UInt:
    case RHIFormat::RG16_SInt:
    case RHIFormat::RG16_Float:
    case RHIFormat::R32_UInt:
    case RHIFormat::R32_SInt:
    case RHIFormat::R32_Float:
    case RHIFormat::RGB10A2_UNorm:
    case RHIFormat::R11G11B10_Float:
    case RHIFormat::D24_UNorm:
    case RHIFormat::D24_UNorm_S8_UInt:
    case RHIFormat::D32_Float:
        return 4;
    case RHIFormat::RGBA16_UNorm:
    case RHIFormat::RGBA16_SNorm:
    case RHIFormat::RGBA16_UInt:
    case RHIFormat::RGBA16_SInt:
    case RHIFormat::RGBA16_Float:
    case RHIFormat::RG32_UInt:
    case RHIFormat::RG32_SInt:
    case RHIFormat::RG32_Float:
    case RHIFormat::D32_Float_S8_UInt:
    case RHIFormat::BC1RGBA_UNorm:
    case RHIFormat::BC1RGBA_SRGB:
    case RHIFormat::BC5RG_UNorm:
    case RHIFormat::BC5RG_SNorm:
        return 8;
    case RHIFormat::RGB32_UInt:
    case RHIFormat::RGB32_SInt:
    case RHIFormat::RGB32_Float:
        return 12;
    case RHIFormat::RGBA32_UInt:
    case RHIFormat::RGBA32_SInt:
    case RHIFormat::RGBA32_Float:
    case RHIFormat::BC3RGBA_UNorm:
    case RHIFormat::BC3RGBA_SRGB:
    case RHIFormat::BC7RGBA_UNorm:
    case RHIFormat::BC7RGBA_SRGB:
        return 16;
    default:
        return 4;
    }
}

static bool isBlockCompressed(RHIFormat format) {
    return format == RHIFormat::BC1RGBA_UNorm ||
           format == RHIFormat::BC1RGBA_SRGB ||
           format == RHIFormat::BC3RGBA_UNorm ||
           format == RHIFormat::BC3RGBA_SRGB ||
           format == RHIFormat::BC5RG_UNorm ||
           format == RHIFormat::BC5RG_SNorm ||
           format == RHIFormat::BC7RGBA_UNorm ||
           format == RHIFormat::BC7RGBA_SRGB;
}

static UINT rowPitchForFormat(RHIFormat format, u32 width) {
    if (isBlockCompressed(format)) {
        return std::max(1u, (width + 3u) / 4u) * formatBytesPerBlock(format);
    }
    return width * formatBytesPerBlock(format);
}

static std::string defaultProfileForStage(RHIShaderStage stage) {
    switch (stage) {
    case RHIShaderStage::Vertex:         return "vs_5_1";
    case RHIShaderStage::TessControl:    return "hs_5_1";
    case RHIShaderStage::TessEvaluation: return "ds_5_1";
    case RHIShaderStage::Geometry:       return "gs_5_1";
    case RHIShaderStage::Fragment:       return "ps_5_1";
    case RHIShaderStage::Compute:        return "cs_5_1";
    default:                          return {};
    }
}

struct CpuDescriptor {
    D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    D3D12_CPU_DESCRIPTOR_HANDLE handle{};
    UINT index = 0;
    bool valid = false;
};

struct DescriptorHeapArena {
    ComPtr<ID3D12DescriptorHeap> heap;
    D3D12_DESCRIPTOR_HEAP_TYPE type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    UINT increment = 0;
    UINT capacity = 0;
    UINT used = 0;
    std::vector<UINT> freeIndices;
};

static D3D12_SHADER_RESOURCE_VIEW_DESC makeTextureSrvDesc(const RHITextureDesc& texture, const RHITextureViewDesc& view, RHIFormat viewFormat) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = toSrvFormat(viewFormat);
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    switch (view.dimension) {
    case RHITextureViewDimension::View1D:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        desc.Texture1D.MostDetailedMip = view.baseMipLevel;
        desc.Texture1D.MipLevels = view.mipLevelCount;
        break;
    case RHITextureViewDimension::View1DArray:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        desc.Texture1DArray.MostDetailedMip = view.baseMipLevel;
        desc.Texture1DArray.MipLevels = view.mipLevelCount;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
        break;
    case RHITextureViewDimension::View2D:
        desc.ViewDimension = texture.samples == RHISampleCount::Count1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE2DMS;
        desc.Texture2D.MostDetailedMip = view.baseMipLevel;
        desc.Texture2D.MipLevels = view.mipLevelCount;
        break;
    case RHITextureViewDimension::View2DArray:
        desc.ViewDimension = texture.samples == RHISampleCount::Count1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
        desc.Texture2DArray.MostDetailedMip = view.baseMipLevel;
        desc.Texture2DArray.MipLevels = view.mipLevelCount;
        desc.Texture2DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DArray.ArraySize = view.arrayLayerCount;
        break;
    case RHITextureViewDimension::View3D:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MostDetailedMip = view.baseMipLevel;
        desc.Texture3D.MipLevels = view.mipLevelCount;
        break;
    case RHITextureViewDimension::Cube:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MostDetailedMip = view.baseMipLevel;
        desc.TextureCube.MipLevels = view.mipLevelCount;
        break;
    case RHITextureViewDimension::CubeArray:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        desc.TextureCubeArray.MostDetailedMip = view.baseMipLevel;
        desc.TextureCubeArray.MipLevels = view.mipLevelCount;
        desc.TextureCubeArray.First2DArrayFace = view.baseArrayLayer;
        desc.TextureCubeArray.NumCubes = std::max(1u, view.arrayLayerCount / 6u);
        break;
    }
    return desc;
}

static D3D12_RENDER_TARGET_VIEW_DESC makeRtvDesc(const RHITextureDesc& texture, const RHITextureViewDesc& view, RHIFormat viewFormat) {
    D3D12_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = toDxgiFormat(viewFormat);
    if (texture.dimension == RHITextureDimension::Texture3D) {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = view.baseMipLevel;
        desc.Texture3D.FirstWSlice = view.baseArrayLayer;
        desc.Texture3D.WSize = view.arrayLayerCount;
    } else if (texture.dimension == RHITextureDimension::Texture1D) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_RTV_DIMENSION_TEXTURE1DARRAY : D3D12_RTV_DIMENSION_TEXTURE1D;
        desc.Texture1DArray.MipSlice = view.baseMipLevel;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
    } else if (texture.samples != RHISampleCount::Count1) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D12_RTV_DIMENSION_TEXTURE2DMS;
        desc.Texture2DMSArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DMSArray.ArraySize = view.arrayLayerCount;
    } else {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DARRAY : D3D12_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2DArray.MipSlice = view.baseMipLevel;
        desc.Texture2DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DArray.ArraySize = view.arrayLayerCount;
    }
    return desc;
}

static D3D12_DEPTH_STENCIL_VIEW_DESC makeDsvDesc(const RHITextureDesc& texture, const RHITextureViewDesc& view, RHIFormat viewFormat) {
    D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
    desc.Format = toDsvFormat(viewFormat);
    desc.Flags = D3D12_DSV_FLAG_NONE;
    if (texture.dimension == RHITextureDimension::Texture1D) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_DSV_DIMENSION_TEXTURE1DARRAY : D3D12_DSV_DIMENSION_TEXTURE1D;
        desc.Texture1DArray.MipSlice = view.baseMipLevel;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
    } else if (texture.samples != RHISampleCount::Count1) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D12_DSV_DIMENSION_TEXTURE2DMS;
        desc.Texture2DMSArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DMSArray.ArraySize = view.arrayLayerCount;
    } else {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DARRAY : D3D12_DSV_DIMENSION_TEXTURE2D;
        desc.Texture2DArray.MipSlice = view.baseMipLevel;
        desc.Texture2DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DArray.ArraySize = view.arrayLayerCount;
    }
    return desc;
}

static D3D12_UNORDERED_ACCESS_VIEW_DESC makeTextureUavDesc(const RHITextureDesc& texture, const RHITextureViewDesc& view, RHIFormat viewFormat) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = toDxgiFormat(viewFormat);
    if (texture.dimension == RHITextureDimension::Texture3D) {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = view.baseMipLevel;
        desc.Texture3D.FirstWSlice = view.baseArrayLayer;
        desc.Texture3D.WSize = view.arrayLayerCount;
    } else if (texture.dimension == RHITextureDimension::Texture1D) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_UAV_DIMENSION_TEXTURE1DARRAY : D3D12_UAV_DIMENSION_TEXTURE1D;
        desc.Texture1DArray.MipSlice = view.baseMipLevel;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
    } else {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
        desc.Texture2DArray.MipSlice = view.baseMipLevel;
        desc.Texture2DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DArray.ArraySize = view.arrayLayerCount;
    }
    return desc;
}

// Impl 是 RHID3D12 的状态仓库。D3D12 的核心对象都是 COM 对象，用 ComPtr 管理生命周期。
// Descriptor 不是 COM 对象，只是 descriptor heap 中的一个槽位，所以用 CpuDescriptor 保存 CPU handle。
struct RHID3D12::Impl {
    struct BufferResource {
        RHIBufferDesc desc{};
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
        void* mappedData = nullptr;
    };

    struct TextureResource {
        RHITextureDesc desc{};
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
        bool swapchainImage = false;
    };

    struct TextureViewResource {
        RHITextureViewDesc desc{};
        CpuDescriptor srv{};
        CpuDescriptor rtv{};
        CpuDescriptor dsv{};
        CpuDescriptor uav{};
    };

    struct SamplerResource {
        RHISamplerDesc desc{};
        CpuDescriptor sampler{};
    };

    struct ShaderResource {
        RHIShaderDesc desc{};
        std::vector<std::byte> bytecode;
    };

    struct BindSetLayoutResource {
        RHIBindSetLayoutDesc desc{};
    };

    struct ResolvedBinding {
        u32 slot = 0;
        RHIBindingType type = RHIBindingType::UniformBuffer;
        RHIShaderStage visibility = RHIShaderStage::AllGraphics;
        CpuDescriptor resourceDescriptor{};
        CpuDescriptor samplerDescriptor{};
        bool ownsResourceDescriptor = false;
    };

    struct BindSetResource {
        RHIBindSetDesc desc{};
        std::vector<ResolvedBinding> bindings;
    };

    struct PipelineLayoutResource {
        RHIPipelineLayoutDesc desc{};
        ComPtr<ID3D12RootSignature> rootSignature;
    };

    struct PipelineCacheResource {
        RHIPipelineCacheDesc desc{};
    };

    struct PipelineResource {
        bool compute = false;
        D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        UINT stencilRef = 0;
        std::array<float, 4> blendConstants{0.0F, 0.0F, 0.0F, 0.0F};
        ComPtr<ID3D12RootSignature> rootSignature;
        ComPtr<ID3D12PipelineState> pipelineState;
    };

    struct QueryPoolResource {
        RHIQueryPoolDesc desc{};
        ComPtr<ID3D12QueryHeap> heap;
    };

    struct GPUWaitGPUSignalResource {
        RHIGPUWaitGPUSignalDesc desc{};
        u64 value = 0;
        bool signaled = false;
    };

    struct CPUWaitGPUSignalResource {
        RHICPUWaitGPUSignalDesc desc{};
        ComPtr<ID3D12Fence> fence;
        HANDLE eventHandle = nullptr;
        u64 value = 0;
        bool signaled = false;
    };

    struct SwapchainResource {
        RHISwapchainDesc desc{};
        ComPtr<IDXGISwapChain3> swapchain;
        RHIFormat format = RHIFormat::Undefined;
        RHIExtent2D extent{};
        std::vector<RHITexture> images;
        std::vector<RHITextureView> imageViews;
        u32 currentImage = 0;
    };

    RHID3D12NativeHandles native{};
    RHID3D12Desc initDesc{};
    RHICapabilities caps{};

    ComPtr<IDXGIFactory6> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> graphicsQueue;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    bool commandListOpen = false;

    DescriptorHeapArena cbvSrvUavHeap{};
    DescriptorHeapArena rtvHeap{};
    DescriptorHeapArena dsvHeap{};
    DescriptorHeapArena samplerHeap{};
    DescriptorHeapArena shaderVisibleCbvSrvUavHeap{};
    DescriptorHeapArena shaderVisibleSamplerHeap{};

    // 单队列版本在复用 command allocator 前等待 internal fence，因此这些对象可以在下一帧
    // 开始时统一释放。后续扩展 frames-in-flight 时可把它们直接移动进 FrameContext。
    std::vector<ComPtr<ID3D12Resource>> pendingStagingResources;
    std::vector<RHIBuffer> pendingTransientBuffers;
    std::vector<RHITexture> pendingTransientTextures;
    std::vector<RHITextureView> pendingTransientTextureViews;

    std::vector<BufferResource> buffers;
    std::vector<TextureResource> textures;
    std::vector<TextureViewResource> textureViews;
    std::vector<SamplerResource> samplers;
    std::vector<ShaderResource> shaders;
    std::vector<BindSetLayoutResource> bindSetLayouts;
    std::vector<BindSetResource> bindSets;
    std::vector<PipelineLayoutResource> pipelineLayouts;
    std::vector<PipelineCacheResource> pipelineCaches;
    std::vector<PipelineResource> pipelines;
    std::vector<QueryPoolResource> queryPools;
    std::vector<GPUWaitGPUSignalResource> gpuWaitGPUSignals;
    std::vector<CPUWaitGPUSignalResource> cpuWaitGPUSignals;
    std::vector<SwapchainResource> swapchains;

    void refreshNativeHandles() noexcept {
        native.factory = factory.Get();
        native.adapter = adapter.Get();
        native.device = device.Get();
        native.graphicsQueue = graphicsQueue.Get();
        native.commandAllocator = commandAllocator.Get();
        native.commandList = commandList.Get();
        native.fence = fence.Get();
        native.fenceValue = fenceValue;
        native.featureLevel = featureLevel;
        native.hwnd = initDesc.surface.hwnd;
    }

    void setDebugName(ID3D12Object* object, const std::string& name) const noexcept {
        if (object == nullptr || name.empty()) {
            return;
        }
        const std::wstring wideName = toWideString(name);
        object->SetName(wideName.c_str());
    }

    void createDescriptorHeap(
        DescriptorHeapArena& arena,
        D3D12_DESCRIPTOR_HEAP_TYPE type,
        UINT capacity,
        D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE) {
        arena.type = type;
        arena.capacity = capacity;
        arena.used = 0;
        arena.freeIndices.clear();
        arena.freeIndices.reserve(capacity);
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type;
        desc.NumDescriptors = capacity;
        desc.Flags = flags;
        throwIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&arena.heap)), "CreateDescriptorHeap failed");
        arena.increment = device->GetDescriptorHandleIncrementSize(type);
    }

    CpuDescriptor allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE type) {
        DescriptorHeapArena* arena = nullptr;
        if (type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
            arena = &cbvSrvUavHeap;
        } else if (type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
            arena = &rtvHeap;
        } else if (type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
            arena = &dsvHeap;
        } else if (type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
            arena = &samplerHeap;
        }
        if (arena == nullptr || !arena->heap ||
            (arena->freeIndices.empty() && arena->used >= arena->capacity)) {
            throw std::runtime_error("D3D12 descriptor heap is exhausted");
        }

        CpuDescriptor descriptor{};
        descriptor.type = type;
        if (!arena->freeIndices.empty()) {
            descriptor.index = arena->freeIndices.back();
            arena->freeIndices.pop_back();
        } else {
            descriptor.index = arena->used++;
        }
        descriptor.valid = true;
        descriptor.handle = arena->heap->GetCPUDescriptorHandleForHeapStart();
        descriptor.handle.ptr += static_cast<SIZE_T>(descriptor.index) * arena->increment;
        return descriptor;
    }

    void releaseDescriptor(CpuDescriptor& descriptor) noexcept {
        if (!descriptor.valid) {
            return;
        }
        DescriptorHeapArena* arena = nullptr;
        if (descriptor.type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) {
            arena = &cbvSrvUavHeap;
        } else if (descriptor.type == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) {
            arena = &rtvHeap;
        } else if (descriptor.type == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) {
            arena = &dsvHeap;
        } else if (descriptor.type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
            arena = &samplerHeap;
        }
        if (arena != nullptr && descriptor.index < arena->capacity) {
            arena->freeIndices.push_back(descriptor.index);
        }
        descriptor = {};
    }

    D3D12_GPU_DESCRIPTOR_HANDLE copyToShaderVisible(CpuDescriptor source) {
        DescriptorHeapArena* arena = nullptr;
        if (source.type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER) {
            arena = &shaderVisibleSamplerHeap;
        } else {
            arena = &shaderVisibleCbvSrvUavHeap;
        }
        if (!source.valid || arena == nullptr || !arena->heap ||
            arena->used >= arena->capacity) {
            throw std::runtime_error("D3D12 shader-visible descriptor heap is exhausted");
        }

        const UINT index = arena->used++;
        D3D12_CPU_DESCRIPTOR_HANDLE destination =
            arena->heap->GetCPUDescriptorHandleForHeapStart();
        destination.ptr += static_cast<SIZE_T>(index) * arena->increment;
        device->CopyDescriptorsSimple(1, destination, source.handle, source.type);

        D3D12_GPU_DESCRIPTOR_HANDLE gpu =
            arena->heap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += static_cast<UINT64>(index) * arena->increment;
        return gpu;
    }

    void resetShaderVisibleDescriptors() noexcept {
        shaderVisibleCbvSrvUavHeap.used = 0;
        shaderVisibleSamplerHeap.used = 0;
    }

    const RHIBindSetLayoutEntry* findLayoutEntry(const BindSetLayoutResource& layout, u32 binding) const {
        const auto it = std::find_if(layout.desc.entries.begin(), layout.desc.entries.end(), [&](const RHIBindSetLayoutEntry& entry) {
            return entry.binding == binding;
        });
        return it == layout.desc.entries.end() ? nullptr : &*it;
    }
};

static bool isSoftwareAdapter(const DXGI_ADAPTER_DESC1& desc) {
    return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

static DXGI_GPU_PREFERENCE toDxgiGpuPreference(RHIPowerPreference preference) {
    switch (preference) {
    case RHIPowerPreference::Default:         return DXGI_GPU_PREFERENCE_UNSPECIFIED;
    case RHIPowerPreference::LowPower:        return DXGI_GPU_PREFERENCE_MINIMUM_POWER;
    case RHIPowerPreference::HighPerformance: return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
    }
    return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
}

static bool canCreateD3D12Device(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL minimumFeatureLevel) {
    return SUCCEEDED(D3D12CreateDevice(adapter, minimumFeatureLevel, __uuidof(ID3D12Device), nullptr));
}

static ComPtr<IDXGIAdapter1> chooseAdapter(IDXGIFactory6* factory, RHIPowerPreference preference, D3D_FEATURE_LEVEL minimumFeatureLevel) {
    ComPtr<IDXGIAdapter1> selected;
    const DXGI_GPU_PREFERENCE gpuPreference = toDxgiGpuPreference(preference);

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapterByGpuPreference(index, gpuPreference, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (isSoftwareAdapter(desc) || !canCreateD3D12Device(adapter.Get(), minimumFeatureLevel)) {
            continue;
        }
        selected = adapter;
        break;
    }

    if (selected) {
        return selected;
    }

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (isSoftwareAdapter(desc) || !canCreateD3D12Device(adapter.Get(), minimumFeatureLevel)) {
            continue;
        }
        return adapter;
    }

    return {};
}

static D3D_FEATURE_LEVEL queryDeviceFeatureLevel(ID3D12Device* device) {
    const std::array<D3D_FEATURE_LEVEL, 5> levels = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0
    };

    D3D12_FEATURE_DATA_FEATURE_LEVELS featureLevels{};
    featureLevels.NumFeatureLevels = static_cast<UINT>(levels.size());
    featureLevels.pFeatureLevelsRequested = levels.data();
    featureLevels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &featureLevels, sizeof(featureLevels)))) {
        return featureLevels.MaxSupportedFeatureLevel;
    }
    return D3D_FEATURE_LEVEL_11_0;
}

static bool supportsTearing(IDXGIFactory6* factory) {
    BOOL allowTearing = FALSE;
    if (factory == nullptr) {
        return false;
    }
    return SUCCEEDED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))) && allowTearing == TRUE;
}

static bool supportsRequiredFeatures(const RHICapabilities& caps, RHIRenderFeature required) {
    if (RHIHasAny(required, RHIRenderFeature::Compute)                 && !caps.supportsCompute)                 return false;
    if (RHIHasAny(required, RHIRenderFeature::GeometryShader)          && !caps.supportsGeometryShader)          return false;
    if (RHIHasAny(required, RHIRenderFeature::Tessellation)            && !caps.supportsTessellation)            return false;
    if (RHIHasAny(required, RHIRenderFeature::SamplerAnisotropy)       && !caps.supportsSamplerAnisotropy)       return false;
    if (RHIHasAny(required, RHIRenderFeature::SamplerCompare)          && !caps.supportsSamplerCompare)          return false;
    if (RHIHasAny(required, RHIRenderFeature::TimestampQuery)          && !caps.supportsTimestampQuery)          return false;
    if (RHIHasAny(required, RHIRenderFeature::OcclusionQuery)          && !caps.supportsOcclusionQuery)          return false;
    if (RHIHasAny(required, RHIRenderFeature::PipelineStatisticsQuery) && !caps.supportsPipelineStatisticsQuery) return false;
    if (RHIHasAny(required, RHIRenderFeature::IndirectDraw)            && !caps.supportsIndirectDraw)            return false;
    if (RHIHasAny(required, RHIRenderFeature::TextureCompressionBC)    && !caps.supportsTextureCompressionBC)    return false;

    const RHIRenderFeature unsupported =
        RHIRenderFeature::MeshShader                |
        RHIRenderFeature::RayTracing                |
        RHIRenderFeature::Bindless                  |
        RHIRenderFeature::DrawIndirectCount         |
        RHIRenderFeature::DynamicRendering          |
        RHIRenderFeature::ConservativeRasterization |
        RHIRenderFeature::TextureCompressionETC2    |
        RHIRenderFeature::TextureCompressionASTC    |
        RHIRenderFeature::Multiview;
    return !RHIHasAny(required, unsupported);
}

static RHICapabilities makeCapabilities(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL featureLevel) {
    RHICapabilities caps{};
    caps.api = RHIGraphicsAPI::Direct3D12;

    DXGI_ADAPTER_DESC1 desc{};
    if (adapter != nullptr) {
        adapter->GetDesc1(&desc);
        caps.adapterName = toUtf8String(desc.Description);
        caps.dedicatedVideoMemory = static_cast<u64>(desc.DedicatedVideoMemory);
        caps.sharedSystemMemory = static_cast<u64>(desc.SharedSystemMemory);
    }

    caps.maxTexture2DSize = D3D12_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    caps.maxTexture3DSize = D3D12_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
    caps.maxTextureCubeSize = D3D12_REQ_TEXTURECUBE_DIMENSION;
    caps.maxTextureArrayLayers = D3D12_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
    caps.maxColorAttachments = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
    caps.maxBindSets = 8;
    caps.maxBindingsPerGroup = D3D12_COMMONSHADER_INPUT_RESOURCE_REGISTER_COUNT;
    caps.maxVertexBuffers = D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
    caps.maxVertexAttributes = D3D12_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT;
    caps.maxPushConstantSize = D3D12_MAX_ROOT_COST * sizeof(u32);
    caps.minUniformBufferOffsetAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT;
    caps.minStorageBufferOffsetAlignment = D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT;
    caps.optimalBufferCopyOffsetAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT;
    caps.optimalBufferCopyRowPitchAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
    caps.maxSamplerAnisotropy = D3D12_REQ_MAXANISOTROPY;

    caps.supportsCompute = true;
    caps.supportsGeometryShader = true;
    caps.supportsTessellation = featureLevel >= D3D_FEATURE_LEVEL_11_0;
    caps.supportsSamplerAnisotropy = true;
    caps.supportsSamplerCompare = true;
    caps.supportsTimestampQuery = true;
    caps.supportsOcclusionQuery = true;
    caps.supportsPipelineStatisticsQuery = true;
    caps.supportsIndirectDraw = true;
    caps.supportsDrawIndirectCount = false;
    caps.supportsDynamicRendering = false;
    caps.supportsDebugMarkers = true;
    caps.supportsTextureCompressionBC = true;
    caps.supportsTextureCompressionETC2 = false;
    caps.supportsTextureCompressionASTC = false;

    caps.features =
        RHIRenderFeature::Compute                 |
        RHIRenderFeature::GeometryShader          |
        RHIRenderFeature::Tessellation            |
        RHIRenderFeature::SamplerAnisotropy       |
        RHIRenderFeature::SamplerCompare          |
        RHIRenderFeature::TimestampQuery          |
        RHIRenderFeature::OcclusionQuery          |
        RHIRenderFeature::PipelineStatisticsQuery |
        RHIRenderFeature::IndirectDraw            |
        RHIRenderFeature::TextureCompressionBC    |
        RHIRenderFeature::DebugMarkers;
    return caps;
}

// D3D12 private 片段是“公共渲染抽象 -> D3D12 原生语言”的词典：
// - RHIFormat/RHIResourceState/Sampler/PipelineState 的枚举转换；
// - Descriptor heap CPU 槽位分配；
// - Impl 中保存的 ID3D12Resource/RootSignature/PSO/Swapchain/Fence；
// - DXGI adapter 选择、feature level 和 RHICapabilities 生成。

} // namespace rhi










