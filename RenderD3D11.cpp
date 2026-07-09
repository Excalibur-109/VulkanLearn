#include "RenderD3D11.hpp"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

using Microsoft::WRL::ComPtr;

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

static DXGI_FORMAT toDxgiFormat(Format format) {
    switch (format) {
    case Format::Undefined:         return DXGI_FORMAT_UNKNOWN;
    case Format::R8_UNorm:          return DXGI_FORMAT_R8_UNORM;
    case Format::R8_SNorm:          return DXGI_FORMAT_R8_SNORM;
    case Format::R8_UInt:           return DXGI_FORMAT_R8_UINT;
    case Format::R8_SInt:           return DXGI_FORMAT_R8_SINT;
    case Format::RG8_UNorm:         return DXGI_FORMAT_R8G8_UNORM;
    case Format::RG8_SNorm:         return DXGI_FORMAT_R8G8_SNORM;
    case Format::RG8_UInt:          return DXGI_FORMAT_R8G8_UINT;
    case Format::RG8_SInt:          return DXGI_FORMAT_R8G8_SINT;
    case Format::RGBA8_UNorm:       return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_SNorm:       return DXGI_FORMAT_R8G8B8A8_SNORM;
    case Format::RGBA8_UInt:        return DXGI_FORMAT_R8G8B8A8_UINT;
    case Format::RGBA8_SInt:        return DXGI_FORMAT_R8G8B8A8_SINT;
    case Format::RGBA8_SRGB:        return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    case Format::BGRA8_UNorm:       return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8_SRGB:        return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
    case Format::R16_UNorm:         return DXGI_FORMAT_R16_UNORM;
    case Format::R16_SNorm:         return DXGI_FORMAT_R16_SNORM;
    case Format::R16_UInt:          return DXGI_FORMAT_R16_UINT;
    case Format::R16_SInt:          return DXGI_FORMAT_R16_SINT;
    case Format::R16_Float:         return DXGI_FORMAT_R16_FLOAT;
    case Format::RG16_UNorm:        return DXGI_FORMAT_R16G16_UNORM;
    case Format::RG16_SNorm:        return DXGI_FORMAT_R16G16_SNORM;
    case Format::RG16_UInt:         return DXGI_FORMAT_R16G16_UINT;
    case Format::RG16_SInt:         return DXGI_FORMAT_R16G16_SINT;
    case Format::RG16_Float:        return DXGI_FORMAT_R16G16_FLOAT;
    case Format::RGBA16_UNorm:      return DXGI_FORMAT_R16G16B16A16_UNORM;
    case Format::RGBA16_SNorm:      return DXGI_FORMAT_R16G16B16A16_SNORM;
    case Format::RGBA16_UInt:       return DXGI_FORMAT_R16G16B16A16_UINT;
    case Format::RGBA16_SInt:       return DXGI_FORMAT_R16G16B16A16_SINT;
    case Format::RGBA16_Float:      return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::R32_UInt:          return DXGI_FORMAT_R32_UINT;
    case Format::R32_SInt:          return DXGI_FORMAT_R32_SINT;
    case Format::R32_Float:         return DXGI_FORMAT_R32_FLOAT;
    case Format::RG32_UInt:         return DXGI_FORMAT_R32G32_UINT;
    case Format::RG32_SInt:         return DXGI_FORMAT_R32G32_SINT;
    case Format::RG32_Float:        return DXGI_FORMAT_R32G32_FLOAT;
    case Format::RGB32_UInt:        return DXGI_FORMAT_R32G32B32_UINT;
    case Format::RGB32_SInt:        return DXGI_FORMAT_R32G32B32_SINT;
    case Format::RGB32_Float:       return DXGI_FORMAT_R32G32B32_FLOAT;
    case Format::RGBA32_UInt:       return DXGI_FORMAT_R32G32B32A32_UINT;
    case Format::RGBA32_SInt:       return DXGI_FORMAT_R32G32B32A32_SINT;
    case Format::RGBA32_Float:      return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Format::RGB10A2_UNorm:     return DXGI_FORMAT_R10G10B10A2_UNORM;
    case Format::R11G11B10_Float:   return DXGI_FORMAT_R11G11B10_FLOAT;
    case Format::D16_UNorm:         return DXGI_FORMAT_D16_UNORM;
    case Format::D24_UNorm:         return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case Format::S8_UInt:           return DXGI_FORMAT_UNKNOWN;
    case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32_Float:         return DXGI_FORMAT_D32_FLOAT;
    case Format::D32_Float_S8_UInt: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    case Format::BC1RGBA_UNorm:     return DXGI_FORMAT_BC1_UNORM;
    case Format::BC1RGBA_SRGB:      return DXGI_FORMAT_BC1_UNORM_SRGB;
    case Format::BC3RGBA_UNorm:     return DXGI_FORMAT_BC3_UNORM;
    case Format::BC3RGBA_SRGB:      return DXGI_FORMAT_BC3_UNORM_SRGB;
    case Format::BC5RG_UNorm:       return DXGI_FORMAT_BC5_UNORM;
    case Format::BC5RG_SNorm:       return DXGI_FORMAT_BC5_SNORM;
    case Format::BC7RGBA_UNorm:     return DXGI_FORMAT_BC7_UNORM;
    case Format::BC7RGBA_SRGB:      return DXGI_FORMAT_BC7_UNORM_SRGB;
    case Format::ETC2RGB8_UNorm:
    case Format::ETC2RGB8_SRGB:
    case Format::ETC2RGBA8_UNorm:
    case Format::ETC2RGBA8_SRGB:
    case Format::ASTC4x4_UNorm:
    case Format::ASTC4x4_SRGB:
    case Format::ASTC8x8_UNorm:
    case Format::ASTC8x8_SRGB:
        return DXGI_FORMAT_UNKNOWN;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static Format fromDxgiFormat(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R8_UNORM:             return Format::R8_UNorm;
    case DXGI_FORMAT_R8_SNORM:             return Format::R8_SNorm;
    case DXGI_FORMAT_R8_UINT:              return Format::R8_UInt;
    case DXGI_FORMAT_R8_SINT:              return Format::R8_SInt;
    case DXGI_FORMAT_R8G8_UNORM:           return Format::RG8_UNorm;
    case DXGI_FORMAT_R8G8_SNORM:           return Format::RG8_SNorm;
    case DXGI_FORMAT_R8G8_UINT:            return Format::RG8_UInt;
    case DXGI_FORMAT_R8G8_SINT:            return Format::RG8_SInt;
    case DXGI_FORMAT_R8G8B8A8_UNORM:       return Format::RGBA8_UNorm;
    case DXGI_FORMAT_R8G8B8A8_SNORM:       return Format::RGBA8_SNorm;
    case DXGI_FORMAT_R8G8B8A8_UINT:        return Format::RGBA8_UInt;
    case DXGI_FORMAT_R8G8B8A8_SINT:        return Format::RGBA8_SInt;
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:  return Format::RGBA8_SRGB;
    case DXGI_FORMAT_B8G8R8A8_UNORM:       return Format::BGRA8_UNorm;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:  return Format::BGRA8_SRGB;
    case DXGI_FORMAT_R16_UNORM:            return Format::R16_UNorm;
    case DXGI_FORMAT_R16_SNORM:            return Format::R16_SNorm;
    case DXGI_FORMAT_R16_UINT:             return Format::R16_UInt;
    case DXGI_FORMAT_R16_SINT:             return Format::R16_SInt;
    case DXGI_FORMAT_R16_FLOAT:            return Format::R16_Float;
    case DXGI_FORMAT_R16G16_UNORM:         return Format::RG16_UNorm;
    case DXGI_FORMAT_R16G16_SNORM:         return Format::RG16_SNorm;
    case DXGI_FORMAT_R16G16_UINT:          return Format::RG16_UInt;
    case DXGI_FORMAT_R16G16_SINT:          return Format::RG16_SInt;
    case DXGI_FORMAT_R16G16_FLOAT:         return Format::RG16_Float;
    case DXGI_FORMAT_R16G16B16A16_UNORM:   return Format::RGBA16_UNorm;
    case DXGI_FORMAT_R16G16B16A16_SNORM:   return Format::RGBA16_SNorm;
    case DXGI_FORMAT_R16G16B16A16_UINT:    return Format::RGBA16_UInt;
    case DXGI_FORMAT_R16G16B16A16_SINT:    return Format::RGBA16_SInt;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:   return Format::RGBA16_Float;
    case DXGI_FORMAT_R32_UINT:             return Format::R32_UInt;
    case DXGI_FORMAT_R32_SINT:             return Format::R32_SInt;
    case DXGI_FORMAT_R32_FLOAT:            return Format::R32_Float;
    case DXGI_FORMAT_R32G32_UINT:          return Format::RG32_UInt;
    case DXGI_FORMAT_R32G32_SINT:          return Format::RG32_SInt;
    case DXGI_FORMAT_R32G32_FLOAT:         return Format::RG32_Float;
    case DXGI_FORMAT_R32G32B32_UINT:       return Format::RGB32_UInt;
    case DXGI_FORMAT_R32G32B32_SINT:       return Format::RGB32_SInt;
    case DXGI_FORMAT_R32G32B32_FLOAT:      return Format::RGB32_Float;
    case DXGI_FORMAT_R32G32B32A32_UINT:    return Format::RGBA32_UInt;
    case DXGI_FORMAT_R32G32B32A32_SINT:    return Format::RGBA32_SInt;
    case DXGI_FORMAT_R32G32B32A32_FLOAT:   return Format::RGBA32_Float;
    case DXGI_FORMAT_R10G10B10A2_UNORM:    return Format::RGB10A2_UNorm;
    case DXGI_FORMAT_R11G11B10_FLOAT:      return Format::R11G11B10_Float;
    case DXGI_FORMAT_D16_UNORM:            return Format::D16_UNorm;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:    return Format::D24_UNorm_S8_UInt;
    case DXGI_FORMAT_D32_FLOAT:            return Format::D32_Float;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return Format::D32_Float_S8_UInt;
    case DXGI_FORMAT_BC1_UNORM:            return Format::BC1RGBA_UNorm;
    case DXGI_FORMAT_BC1_UNORM_SRGB:       return Format::BC1RGBA_SRGB;
    case DXGI_FORMAT_BC3_UNORM:            return Format::BC3RGBA_UNorm;
    case DXGI_FORMAT_BC3_UNORM_SRGB:       return Format::BC3RGBA_SRGB;
    case DXGI_FORMAT_BC5_UNORM:            return Format::BC5RG_UNorm;
    case DXGI_FORMAT_BC5_SNORM:            return Format::BC5RG_SNorm;
    case DXGI_FORMAT_BC7_UNORM:            return Format::BC7RGBA_UNorm;
    case DXGI_FORMAT_BC7_UNORM_SRGB:       return Format::BC7RGBA_SRGB;
    default:                               return Format::Undefined;
    }
}

static DXGI_FORMAT toTypelessDepthFormat(Format format) {
    switch (format) {
    case Format::D16_UNorm:         return DXGI_FORMAT_R16_TYPELESS;
    case Format::D24_UNorm:
    case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24G8_TYPELESS;
    case Format::D32_Float:         return DXGI_FORMAT_R32_TYPELESS;
    case Format::D32_Float_S8_UInt: return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:                        return toDxgiFormat(format);
    }
}

static DXGI_FORMAT toSrvFormat(Format format) {
    switch (format) {
    case Format::D16_UNorm:         return DXGI_FORMAT_R16_UNORM;
    case Format::D24_UNorm:
    case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case Format::D32_Float:         return DXGI_FORMAT_R32_FLOAT;
    case Format::D32_Float_S8_UInt: return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    default:                        return toDxgiFormat(format);
    }
}

static DXGI_FORMAT toDsvFormat(Format format) {
    switch (format) {
    case Format::D16_UNorm:         return DXGI_FORMAT_D16_UNORM;
    case Format::D24_UNorm:
    case Format::D24_UNorm_S8_UInt: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32_Float:         return DXGI_FORMAT_D32_FLOAT;
    case Format::D32_Float_S8_UInt: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

static DXGI_FORMAT toSwapchainFormat(Format format) {
    switch (format) {
    case Format::RGBA8_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::BGRA8_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    default: {
        const DXGI_FORMAT dxgi = toDxgiFormat(format);
        return dxgi == DXGI_FORMAT_UNKNOWN ? DXGI_FORMAT_B8G8R8A8_UNORM : dxgi;
    }
    }
}

static UINT toSampleCount(SampleCount samples) {
    return static_cast<UINT>(samples);
}

static D3D11_USAGE toD3DUsage(MemoryUsage usage, bool persistentlyMapped) {
    if (persistentlyMapped || usage == MemoryUsage::CpuToGpu) {
        return D3D11_USAGE_DYNAMIC;
    }
    if (usage == MemoryUsage::GpuToCpu || usage == MemoryUsage::CpuOnly) {
        return D3D11_USAGE_STAGING;
    }
    return D3D11_USAGE_DEFAULT;
}

static UINT toCpuAccessFlags(MemoryUsage usage, bool persistentlyMapped) {
    UINT flags = 0;
    if (persistentlyMapped || usage == MemoryUsage::CpuToGpu || usage == MemoryUsage::CpuOnly) {
        flags |= D3D11_CPU_ACCESS_WRITE;
    }
    if (usage == MemoryUsage::GpuToCpu || usage == MemoryUsage::CpuOnly) {
        flags |= D3D11_CPU_ACCESS_READ;
    }
    return flags;
}

static UINT toBufferBindFlags(BufferUsage usage, MemoryUsage memoryUsage) {
    if (memoryUsage == MemoryUsage::GpuToCpu || memoryUsage == MemoryUsage::CpuOnly) {
        return 0;
    }

    UINT flags = 0;
    if (hasAny(usage, BufferUsage::Vertex)) flags |= D3D11_BIND_VERTEX_BUFFER;
    if (hasAny(usage, BufferUsage::Index)) flags |= D3D11_BIND_INDEX_BUFFER;
    if (hasAny(usage, BufferUsage::Uniform)) flags |= D3D11_BIND_CONSTANT_BUFFER;
    if (hasAny(usage, BufferUsage::Storage)) flags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

static UINT toTextureBindFlags(TextureUsage usage, MemoryUsage memoryUsage) {
    if (memoryUsage == MemoryUsage::GpuToCpu || memoryUsage == MemoryUsage::CpuOnly) {
        return 0;
    }

    UINT flags = 0;
    if (hasAny(usage, TextureUsage::Sampled)) flags |= D3D11_BIND_SHADER_RESOURCE;
    if (hasAny(usage, TextureUsage::Storage)) flags |= D3D11_BIND_UNORDERED_ACCESS;
    if (hasAny(usage, TextureUsage::ColorAttachment) || hasAny(usage, TextureUsage::Present)) flags |= D3D11_BIND_RENDER_TARGET;
    if (hasAny(usage, TextureUsage::DepthStencilAttachment)) flags |= D3D11_BIND_DEPTH_STENCIL;
    return flags == 0 ? D3D11_BIND_SHADER_RESOURCE : flags;
}

static D3D11_FILTER toD3DFilter(const SamplerDesc& desc) {
    if (desc.enableAnisotropy) {
        return desc.enableCompare ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
    }

    const bool minLinear = desc.minFilter == FilterMode::Linear;
    const bool magLinear = desc.magFilter == FilterMode::Linear;
    const bool mipLinear = desc.mipmapMode == MipmapMode::Linear;
    if (desc.enableCompare) {
        if (minLinear && magLinear && mipLinear) return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        if (minLinear && magLinear) return D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        if (minLinear && mipLinear) return D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        if (magLinear && mipLinear) return D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR;
        if (minLinear) return D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT;
        if (magLinear) return D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT;
        if (mipLinear) return D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR;
        return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    }

    if (minLinear && magLinear && mipLinear) return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (minLinear && magLinear) return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (minLinear && mipLinear) return D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    if (magLinear && mipLinear) return D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
    if (minLinear) return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    if (magLinear) return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    if (mipLinear) return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    return D3D11_FILTER_MIN_MAG_MIP_POINT;
}

static D3D11_TEXTURE_ADDRESS_MODE toD3DAddressMode(AddressMode mode) {
    switch (mode) {
    case AddressMode::Repeat:         return D3D11_TEXTURE_ADDRESS_WRAP;
    case AddressMode::MirroredRepeat: return D3D11_TEXTURE_ADDRESS_MIRROR;
    case AddressMode::ClampToEdge:    return D3D11_TEXTURE_ADDRESS_CLAMP;
    case AddressMode::ClampToBorder:  return D3D11_TEXTURE_ADDRESS_BORDER;
    }
    return D3D11_TEXTURE_ADDRESS_WRAP;
}

static D3D11_COMPARISON_FUNC toD3DCompare(CompareOp op) {
    switch (op) {
    case CompareOp::Never:          return D3D11_COMPARISON_NEVER;
    case CompareOp::Less:           return D3D11_COMPARISON_LESS;
    case CompareOp::Equal:          return D3D11_COMPARISON_EQUAL;
    case CompareOp::LessOrEqual:    return D3D11_COMPARISON_LESS_EQUAL;
    case CompareOp::Greater:        return D3D11_COMPARISON_GREATER;
    case CompareOp::NotEqual:       return D3D11_COMPARISON_NOT_EQUAL;
    case CompareOp::GreaterOrEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case CompareOp::Always:         return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_ALWAYS;
}

static DXGI_FORMAT toD3DVertexFormat(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float32:   return DXGI_FORMAT_R32_FLOAT;
    case VertexFormat::Float32x2: return DXGI_FORMAT_R32G32_FLOAT;
    case VertexFormat::Float32x3: return DXGI_FORMAT_R32G32B32_FLOAT;
    case VertexFormat::Float32x4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VertexFormat::UInt32:    return DXGI_FORMAT_R32_UINT;
    case VertexFormat::UInt32x2:  return DXGI_FORMAT_R32G32_UINT;
    case VertexFormat::UInt32x3:  return DXGI_FORMAT_R32G32B32_UINT;
    case VertexFormat::UInt32x4:  return DXGI_FORMAT_R32G32B32A32_UINT;
    case VertexFormat::SInt32:    return DXGI_FORMAT_R32_SINT;
    case VertexFormat::SInt32x2:  return DXGI_FORMAT_R32G32_SINT;
    case VertexFormat::SInt32x3:  return DXGI_FORMAT_R32G32B32_SINT;
    case VertexFormat::SInt32x4:  return DXGI_FORMAT_R32G32B32A32_SINT;
    case VertexFormat::UNorm8x4:  return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::SNorm8x4:  return DXGI_FORMAT_R8G8B8A8_SNORM;
    case VertexFormat::UInt16x2:  return DXGI_FORMAT_R16G16_UINT;
    case VertexFormat::UInt16x4:  return DXGI_FORMAT_R16G16B16A16_UINT;
    case VertexFormat::SInt16x2:  return DXGI_FORMAT_R16G16_SINT;
    case VertexFormat::SInt16x4:  return DXGI_FORMAT_R16G16B16A16_SINT;
    case VertexFormat::UNorm16x2: return DXGI_FORMAT_R16G16_UNORM;
    case VertexFormat::UNorm16x4: return DXGI_FORMAT_R16G16B16A16_UNORM;
    case VertexFormat::SNorm16x2: return DXGI_FORMAT_R16G16_SNORM;
    case VertexFormat::SNorm16x4: return DXGI_FORMAT_R16G16B16A16_SNORM;
    }
    return DXGI_FORMAT_UNKNOWN;
}

static D3D11_PRIMITIVE_TOPOLOGY toD3DTopology(const InputAssemblyState& state) {
    switch (state.topology) {
    case PrimitiveTopology::PointList:     return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveTopology::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::LineStrip:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case PrimitiveTopology::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::PatchList: {
        const u32 points = std::clamp(state.patchControlPoints == 0 ? 3u : state.patchControlPoints, 1u, 32u);
        return static_cast<D3D11_PRIMITIVE_TOPOLOGY>(D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + points - 1);
    }
    }
    return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

static D3D11_STENCIL_OP toD3DStencilOp(StencilOp op) {
    switch (op) {
    case StencilOp::Keep:           return D3D11_STENCIL_OP_KEEP;
    case StencilOp::Zero:           return D3D11_STENCIL_OP_ZERO;
    case StencilOp::Replace:        return D3D11_STENCIL_OP_REPLACE;
    case StencilOp::IncrementClamp: return D3D11_STENCIL_OP_INCR_SAT;
    case StencilOp::DecrementClamp: return D3D11_STENCIL_OP_DECR_SAT;
    case StencilOp::Invert:         return D3D11_STENCIL_OP_INVERT;
    case StencilOp::IncrementWrap:  return D3D11_STENCIL_OP_INCR;
    case StencilOp::DecrementWrap:  return D3D11_STENCIL_OP_DECR;
    }
    return D3D11_STENCIL_OP_KEEP;
}

static D3D11_BLEND toD3DBlend(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:                     return D3D11_BLEND_ZERO;
    case BlendFactor::One:                      return D3D11_BLEND_ONE;
    case BlendFactor::SourceColor:              return D3D11_BLEND_SRC_COLOR;
    case BlendFactor::OneMinusSourceColor:      return D3D11_BLEND_INV_SRC_COLOR;
    case BlendFactor::DestinationColor:         return D3D11_BLEND_DEST_COLOR;
    case BlendFactor::OneMinusDestinationColor: return D3D11_BLEND_INV_DEST_COLOR;
    case BlendFactor::SourceAlpha:              return D3D11_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha:      return D3D11_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestinationAlpha:         return D3D11_BLEND_DEST_ALPHA;
    case BlendFactor::OneMinusDestinationAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
    case BlendFactor::ConstantColor:
    case BlendFactor::ConstantAlpha:            return D3D11_BLEND_BLEND_FACTOR;
    case BlendFactor::OneMinusConstantColor:
    case BlendFactor::OneMinusConstantAlpha:    return D3D11_BLEND_INV_BLEND_FACTOR;
    }
    return D3D11_BLEND_ONE;
}

static D3D11_BLEND_OP toD3DBlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add:             return D3D11_BLEND_OP_ADD;
    case BlendOp::Subtract:        return D3D11_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:             return D3D11_BLEND_OP_MIN;
    case BlendOp::Max:             return D3D11_BLEND_OP_MAX;
    }
    return D3D11_BLEND_OP_ADD;
}

static UINT8 toD3DColorWriteMask(ColorWriteMask mask) {
    UINT8 flags = 0;
    if (hasAny(mask, ColorWriteMask::R)) flags |= D3D11_COLOR_WRITE_ENABLE_RED;
    if (hasAny(mask, ColorWriteMask::G)) flags |= D3D11_COLOR_WRITE_ENABLE_GREEN;
    if (hasAny(mask, ColorWriteMask::B)) flags |= D3D11_COLOR_WRITE_ENABLE_BLUE;
    if (hasAny(mask, ColorWriteMask::A)) flags |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
    return flags;
}

static UINT formatBytesPerBlock(Format format) {
    switch (format) {
    case Format::R8_UNorm:
    case Format::R8_SNorm:
    case Format::R8_UInt:
    case Format::R8_SInt:
        return 1;
    case Format::RG8_UNorm:
    case Format::RG8_SNorm:
    case Format::RG8_UInt:
    case Format::RG8_SInt:
    case Format::R16_UNorm:
    case Format::R16_SNorm:
    case Format::R16_UInt:
    case Format::R16_SInt:
    case Format::R16_Float:
    case Format::D16_UNorm:
        return 2;
    case Format::RGBA8_UNorm:
    case Format::RGBA8_SNorm:
    case Format::RGBA8_UInt:
    case Format::RGBA8_SInt:
    case Format::RGBA8_SRGB:
    case Format::BGRA8_UNorm:
    case Format::BGRA8_SRGB:
    case Format::RG16_UNorm:
    case Format::RG16_SNorm:
    case Format::RG16_UInt:
    case Format::RG16_SInt:
    case Format::RG16_Float:
    case Format::R32_UInt:
    case Format::R32_SInt:
    case Format::R32_Float:
    case Format::RGB10A2_UNorm:
    case Format::R11G11B10_Float:
    case Format::D24_UNorm:
    case Format::D24_UNorm_S8_UInt:
    case Format::D32_Float:
        return 4;
    case Format::RGBA16_UNorm:
    case Format::RGBA16_SNorm:
    case Format::RGBA16_UInt:
    case Format::RGBA16_SInt:
    case Format::RGBA16_Float:
    case Format::RG32_UInt:
    case Format::RG32_SInt:
    case Format::RG32_Float:
    case Format::D32_Float_S8_UInt:
    case Format::BC1RGBA_UNorm:
    case Format::BC1RGBA_SRGB:
    case Format::BC5RG_UNorm:
    case Format::BC5RG_SNorm:
        return 8;
    case Format::RGB32_UInt:
    case Format::RGB32_SInt:
    case Format::RGB32_Float:
        return 12;
    case Format::RGBA32_UInt:
    case Format::RGBA32_SInt:
    case Format::RGBA32_Float:
    case Format::BC3RGBA_UNorm:
    case Format::BC3RGBA_SRGB:
    case Format::BC7RGBA_UNorm:
    case Format::BC7RGBA_SRGB:
        return 16;
    default:
        return 4;
    }
}

static bool isBlockCompressed(Format format) {
    return format == Format::BC1RGBA_UNorm ||
           format == Format::BC1RGBA_SRGB ||
           format == Format::BC3RGBA_UNorm ||
           format == Format::BC3RGBA_SRGB ||
           format == Format::BC5RG_UNorm ||
           format == Format::BC5RG_SNorm ||
           format == Format::BC7RGBA_UNorm ||
           format == Format::BC7RGBA_SRGB;
}

static UINT rowPitchForFormat(Format format, u32 width) {
    if (isBlockCompressed(format)) {
        return std::max(1u, (width + 3u) / 4u) * formatBytesPerBlock(format);
    }
    return width * formatBytesPerBlock(format);
}

static std::string defaultProfileForStage(ShaderStage stage) {
    switch (stage) {
    case ShaderStage::Vertex:         return "vs_5_0";
    case ShaderStage::TessControl:    return "hs_5_0";
    case ShaderStage::TessEvaluation: return "ds_5_0";
    case ShaderStage::Geometry:       return "gs_5_0";
    case ShaderStage::Fragment:       return "ps_5_0";
    case ShaderStage::Compute:        return "cs_5_0";
    default:                          return {};
    }
}

static D3D11_SHADER_RESOURCE_VIEW_DESC makeTextureSrvDesc(const TextureDesc& texture, const TextureViewDesc& view, Format viewFormat) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = toSrvFormat(viewFormat);
    switch (view.dimension) {
    case TextureViewDimension::View1D:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
        desc.Texture1D.MostDetailedMip = view.baseMipLevel;
        desc.Texture1D.MipLevels = view.mipLevelCount;
        break;
    case TextureViewDimension::View1DArray:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
        desc.Texture1DArray.MostDetailedMip = view.baseMipLevel;
        desc.Texture1DArray.MipLevels = view.mipLevelCount;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
        break;
    case TextureViewDimension::View2D:
        desc.ViewDimension = texture.samples == SampleCount::Count1 ? D3D11_SRV_DIMENSION_TEXTURE2D : D3D11_SRV_DIMENSION_TEXTURE2DMS;
        desc.Texture2D.MostDetailedMip = view.baseMipLevel;
        desc.Texture2D.MipLevels = view.mipLevelCount;
        break;
    case TextureViewDimension::View2DArray:
        desc.ViewDimension = texture.samples == SampleCount::Count1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
        desc.Texture2DArray.MostDetailedMip = view.baseMipLevel;
        desc.Texture2DArray.MipLevels = view.mipLevelCount;
        desc.Texture2DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DArray.ArraySize = view.arrayLayerCount;
        break;
    case TextureViewDimension::View3D:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MostDetailedMip = view.baseMipLevel;
        desc.Texture3D.MipLevels = view.mipLevelCount;
        break;
    case TextureViewDimension::Cube:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MostDetailedMip = view.baseMipLevel;
        desc.TextureCube.MipLevels = view.mipLevelCount;
        break;
    case TextureViewDimension::CubeArray:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        desc.TextureCubeArray.MostDetailedMip = view.baseMipLevel;
        desc.TextureCubeArray.MipLevels = view.mipLevelCount;
        desc.TextureCubeArray.First2DArrayFace = view.baseArrayLayer;
        desc.TextureCubeArray.NumCubes = std::max(1u, view.arrayLayerCount / 6u);
        break;
    }
    return desc;
}

static D3D11_RENDER_TARGET_VIEW_DESC makeRtvDesc(const TextureDesc& texture, const TextureViewDesc& view, Format viewFormat) {
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = toDxgiFormat(viewFormat);
    if (texture.dimension == TextureDimension::Texture3D) {
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = view.baseMipLevel;
        desc.Texture3D.FirstWSlice = view.baseArrayLayer;
        desc.Texture3D.WSize = view.arrayLayerCount;
    } else if (texture.dimension == TextureDimension::Texture1D) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D11_RTV_DIMENSION_TEXTURE1DARRAY : D3D11_RTV_DIMENSION_TEXTURE1D;
        desc.Texture1DArray.MipSlice = view.baseMipLevel;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
    } else if (texture.samples != SampleCount::Count1) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D11_RTV_DIMENSION_TEXTURE2DMS;
        desc.Texture2DMSArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DMSArray.ArraySize = view.arrayLayerCount;
    } else {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D;
        desc.Texture2DArray.MipSlice = view.baseMipLevel;
        desc.Texture2DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DArray.ArraySize = view.arrayLayerCount;
    }
    return desc;
}

static D3D11_DEPTH_STENCIL_VIEW_DESC makeDsvDesc(const TextureDesc& texture, const TextureViewDesc& view, Format viewFormat) {
    D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
    desc.Format = toDsvFormat(viewFormat);
    if (texture.dimension == TextureDimension::Texture1D) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D11_DSV_DIMENSION_TEXTURE1DARRAY : D3D11_DSV_DIMENSION_TEXTURE1D;
        desc.Texture1DArray.MipSlice = view.baseMipLevel;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
    } else if (texture.samples != SampleCount::Count1) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D11_DSV_DIMENSION_TEXTURE2DMS;
        desc.Texture2DMSArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DMSArray.ArraySize = view.arrayLayerCount;
    } else {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D;
        desc.Texture2DArray.MipSlice = view.baseMipLevel;
        desc.Texture2DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DArray.ArraySize = view.arrayLayerCount;
    }
    return desc;
}

struct D3D11Renderer::Impl {
    struct BufferResource {
        BufferDesc desc{};
        ComPtr<ID3D11Buffer> buffer;
    };

    struct TextureResource {
        TextureDesc desc{};
        ComPtr<ID3D11Resource> resource;
        ResourceState currentState = ResourceState::Undefined;
        bool swapchainImage = false;
    };

    struct TextureViewResource {
        TextureViewDesc desc{};
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11DepthStencilView> dsv;
        ComPtr<ID3D11UnorderedAccessView> uav;
    };

    struct SamplerResource {
        SamplerDesc desc{};
        ComPtr<ID3D11SamplerState> sampler;
    };

    struct ShaderResource {
        ShaderDesc desc{};
        std::vector<std::byte> bytecode;
        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11HullShader> hullShader;
        ComPtr<ID3D11DomainShader> domainShader;
        ComPtr<ID3D11GeometryShader> geometryShader;
        ComPtr<ID3D11PixelShader> pixelShader;
        ComPtr<ID3D11ComputeShader> computeShader;
    };

    struct BindGroupLayoutResource {
        BindGroupLayoutDesc desc{};
    };

    struct ResolvedBinding {
        u32 slot = 0;
        BindingType type = BindingType::UniformBuffer;
        ShaderStage visibility = ShaderStage::AllGraphics;
        ComPtr<ID3D11Buffer> buffer;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;
        ComPtr<ID3D11SamplerState> sampler;
    };

    struct BindGroupResource {
        BindGroupDesc desc{};
        std::vector<ResolvedBinding> bindings;
    };

    struct PipelineLayoutResource {
        PipelineLayoutDesc desc{};
    };

    struct PipelineCacheResource {
        PipelineCacheDesc desc{};
    };

    struct PipelineResource {
        bool compute = false;
        D3D11_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        UINT stencilRef = 0;
        std::array<float, 4> blendConstants{0.0F, 0.0F, 0.0F, 0.0F};
        UINT sampleMask = 0xFFFFFFFFu;
        ComPtr<ID3D11InputLayout> inputLayout;
        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11HullShader> hullShader;
        ComPtr<ID3D11DomainShader> domainShader;
        ComPtr<ID3D11GeometryShader> geometryShader;
        ComPtr<ID3D11PixelShader> pixelShader;
        ComPtr<ID3D11ComputeShader> computeShader;
        ComPtr<ID3D11RasterizerState> rasterizerState;
        ComPtr<ID3D11DepthStencilState> depthStencilState;
        ComPtr<ID3D11BlendState> blendState;
    };

    struct QueryPoolResource {
        QueryPoolDesc desc{};
        std::vector<ComPtr<ID3D11Query>> queries;
    };

    struct SemaphoreResource {
        SemaphoreDesc desc{};
        u64 value = 0;
        bool signaled = false;
    };

    struct FenceResource {
        FenceDesc desc{};
        ComPtr<ID3D11Query> eventQuery;
        bool signaled = false;
    };

    struct SwapchainResource {
        SwapchainDesc desc{};
        ComPtr<IDXGISwapChain> swapchain;
        Format format = Format::Undefined;
        Extent2D extent{};
        std::vector<TextureHandle> images;
        std::vector<TextureViewHandle> imageViews;
    };

    D3D11NativeHandles native{};
    D3D11RendererDesc initDesc{};
    RenderCapabilities caps{};

    ComPtr<IDXGIFactory1> factory;
    ComPtr<IDXGIAdapter1> adapter;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

    std::vector<BufferResource> buffers;
    std::vector<TextureResource> textures;
    std::vector<TextureViewResource> textureViews;
    std::vector<SamplerResource> samplers;
    std::vector<ShaderResource> shaders;
    std::vector<BindGroupLayoutResource> bindGroupLayouts;
    std::vector<BindGroupResource> bindGroups;
    std::vector<PipelineLayoutResource> pipelineLayouts;
    std::vector<PipelineCacheResource> pipelineCaches;
    std::vector<PipelineResource> pipelines;
    std::vector<QueryPoolResource> queryPools;
    std::vector<SemaphoreResource> semaphores;
    std::vector<FenceResource> fences;
    std::vector<SwapchainResource> swapchains;

    void refreshNativeHandles() noexcept {
        native.factory = factory.Get();
        native.adapter = adapter.Get();
        native.device = device.Get();
        native.immediateContext = context.Get();
        native.featureLevel = featureLevel;
        native.hwnd = initDesc.surface.hwnd;
    }

    void setDebugName(ID3D11DeviceChild* object, const std::string& name) const noexcept {
        if (object == nullptr || name.empty()) {
            return;
        }
        object->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(name.size()), name.data());
    }

    const BindGroupLayoutEntry* findLayoutEntry(const BindGroupLayoutResource& layout, u32 binding) const {
        const auto it = std::find_if(layout.desc.entries.begin(), layout.desc.entries.end(), [&](const BindGroupLayoutEntry& entry) {
            return entry.binding == binding;
        });
        return it == layout.desc.entries.end() ? nullptr : &*it;
    }
};

static bool isSoftwareAdapter(const DXGI_ADAPTER_DESC1& desc) {
    return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

static ComPtr<IDXGIAdapter1> chooseAdapter(IDXGIFactory1* factory, PowerPreference preference) {
    ComPtr<IDXGIAdapter1> selected;
    SIZE_T selectedMemory = preference == PowerPreference::LowPower ? std::numeric_limits<SIZE_T>::max() : 0;

    for (UINT index = 0;; ++index) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (isSoftwareAdapter(desc)) {
            continue;
        }

        if (!selected) {
            selected = adapter;
            selectedMemory = desc.DedicatedVideoMemory;
            if (preference == PowerPreference::Default) {
                break;
            }
            continue;
        }

        if (preference == PowerPreference::HighPerformance && desc.DedicatedVideoMemory > selectedMemory) {
            selected = adapter;
            selectedMemory = desc.DedicatedVideoMemory;
        } else if (preference == PowerPreference::LowPower && desc.DedicatedVideoMemory < selectedMemory) {
            selected = adapter;
            selectedMemory = desc.DedicatedVideoMemory;
        }
    }

    return selected;
}

static bool supportsRequiredFeatures(const RenderCapabilities& caps, RenderFeature required) {
    if (hasAny(required, RenderFeature::Compute) && !caps.supportsCompute) return false;
    if (hasAny(required, RenderFeature::GeometryShader) && !caps.supportsGeometryShader) return false;
    if (hasAny(required, RenderFeature::Tessellation) && !caps.supportsTessellation) return false;
    if (hasAny(required, RenderFeature::SamplerAnisotropy) && !caps.supportsSamplerAnisotropy) return false;
    if (hasAny(required, RenderFeature::SamplerCompare) && !caps.supportsSamplerCompare) return false;
    if (hasAny(required, RenderFeature::TimestampQuery) && !caps.supportsTimestampQuery) return false;
    if (hasAny(required, RenderFeature::OcclusionQuery) && !caps.supportsOcclusionQuery) return false;
    if (hasAny(required, RenderFeature::PipelineStatisticsQuery) && !caps.supportsPipelineStatisticsQuery) return false;
    if (hasAny(required, RenderFeature::IndirectDraw) && !caps.supportsIndirectDraw) return false;
    if (hasAny(required, RenderFeature::TextureCompressionBC) && !caps.supportsTextureCompressionBC) return false;

    const RenderFeature unsupported =
        RenderFeature::MeshShader |
        RenderFeature::RayTracing |
        RenderFeature::Bindless |
        RenderFeature::DrawIndirectCount |
        RenderFeature::DynamicRendering |
        RenderFeature::ConservativeRasterization |
        RenderFeature::TextureCompressionETC2 |
        RenderFeature::TextureCompressionASTC |
        RenderFeature::Multiview;
    return !hasAny(required, unsupported);
}

static RenderCapabilities makeCapabilities(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL featureLevel) {
    RenderCapabilities caps{};
    caps.api = GraphicsApi::Direct3D11;

    DXGI_ADAPTER_DESC1 desc{};
    if (adapter != nullptr) {
        adapter->GetDesc1(&desc);
        caps.adapterName = toUtf8String(desc.Description);
        caps.dedicatedVideoMemory = static_cast<u64>(desc.DedicatedVideoMemory);
        caps.sharedSystemMemory = static_cast<u64>(desc.SharedSystemMemory);
    }

    caps.maxTexture2DSize = D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION;
    caps.maxTexture3DSize = D3D11_REQ_TEXTURE3D_U_V_OR_W_DIMENSION;
    caps.maxTextureCubeSize = D3D11_REQ_TEXTURECUBE_DIMENSION;
    caps.maxTextureArrayLayers = D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
    caps.maxColorAttachments = D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT;
    caps.maxBindGroups = 1;
    caps.maxBindingsPerGroup = D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT;
    caps.maxVertexBuffers = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
    caps.maxVertexAttributes = D3D11_IA_VERTEX_INPUT_STRUCTURE_ELEMENT_COUNT;
    caps.maxPushConstantSize = 0;
    caps.minUniformBufferOffsetAlignment = 16;
    caps.minStorageBufferOffsetAlignment = 4;
    caps.optimalBufferCopyOffsetAlignment = 16;
    caps.optimalBufferCopyRowPitchAlignment = 1;
    caps.maxSamplerAnisotropy = D3D11_REQ_MAXANISOTROPY;

    caps.supportsCompute = featureLevel >= D3D_FEATURE_LEVEL_11_0;
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
        RenderFeature::Compute |
        RenderFeature::GeometryShader |
        RenderFeature::Tessellation |
        RenderFeature::SamplerAnisotropy |
        RenderFeature::SamplerCompare |
        RenderFeature::TimestampQuery |
        RenderFeature::OcclusionQuery |
        RenderFeature::PipelineStatisticsQuery |
        RenderFeature::IndirectDraw |
        RenderFeature::TextureCompressionBC |
        RenderFeature::DebugMarkers;
    return caps;
}

D3D11Renderer::D3D11Renderer()
    : impl_(std::make_unique<Impl>()) {
}

D3D11Renderer::~D3D11Renderer() {
    shutdown();
}

D3D11Renderer::D3D11Renderer(D3D11Renderer&&) noexcept = default;

D3D11Renderer& D3D11Renderer::operator=(D3D11Renderer&&) noexcept = default;

bool D3D11Renderer::initialize(const D3D11RendererDesc& desc, std::string* errorMessage) {
    try {
        if (isInitialized()) {
            shutdown();
        }

        impl_ = std::make_unique<Impl>();
        impl_->initDesc = desc;

        throwIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&impl_->factory)), "CreateDXGIFactory1 failed");
        impl_->adapter = chooseAdapter(impl_->factory.Get(), desc.backend.powerPreference);

        UINT createFlags = 0;
        if (desc.backend.validation != ValidationMode::Disabled) {
            createFlags |= D3D11_CREATE_DEVICE_DEBUG;
        }

        const std::array<D3D_FEATURE_LEVEL, 5> featureLevels = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
            D3D_FEATURE_LEVEL_9_3
        };

        HRESULT hr = E_FAIL;
        if (impl_->adapter) {
            hr = D3D11CreateDevice(
                impl_->adapter.Get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                createFlags,
                featureLevels.data(),
                static_cast<UINT>(featureLevels.size()),
                D3D11_SDK_VERSION,
                &impl_->device,
                &impl_->featureLevel,
                &impl_->context);
        } else if (desc.driverType != D3D_DRIVER_TYPE_UNKNOWN) {
            hr = D3D11CreateDevice(
                nullptr,
                desc.driverType,
                nullptr,
                createFlags,
                featureLevels.data(),
                static_cast<UINT>(featureLevels.size()),
                D3D11_SDK_VERSION,
                &impl_->device,
                &impl_->featureLevel,
                &impl_->context);
        }

        if (FAILED(hr) && desc.allowWarpFallback) {
            hr = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP,
                nullptr,
                createFlags,
                featureLevels.data(),
                static_cast<UINT>(featureLevels.size()),
                D3D11_SDK_VERSION,
                &impl_->device,
                &impl_->featureLevel,
                &impl_->context);
        }
        throwIfFailed(hr, "D3D11CreateDevice failed");

        if (!impl_->adapter) {
            ComPtr<IDXGIDevice> dxgiDevice;
            throwIfFailed(impl_->device.As(&dxgiDevice), "Query IDXGIDevice failed");
            ComPtr<IDXGIAdapter> baseAdapter;
            throwIfFailed(dxgiDevice->GetAdapter(&baseAdapter), "IDXGIDevice::GetAdapter failed");
            throwIfFailed(baseAdapter.As(&impl_->adapter), "Query IDXGIAdapter1 failed");
        }

        impl_->caps = makeCapabilities(impl_->adapter.Get(), impl_->featureLevel);
        if (impl_->featureLevel < desc.minimumFeatureLevel) {
            throw std::runtime_error("D3D11 feature level is below the requested minimum");
        }
        if (!supportsRequiredFeatures(impl_->caps, desc.backend.requiredFeatures)) {
            throw std::runtime_error("D3D11 device does not support all required RenderFeature flags");
        }

        impl_->refreshNativeHandles();
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        shutdown();
        return false;
    }
}

void D3D11Renderer::shutdown() noexcept {
    if (!impl_) {
        return;
    }

    if (impl_->context) {
        impl_->context->ClearState();
        impl_->context->Flush();
    }

    for (u64 i = impl_->swapchains.size(); i > 0; --i) destroy(SwapchainHandle(i));
    for (u64 i = impl_->pipelines.size(); i > 0; --i) destroy(PipelineHandle(i));
    for (u64 i = impl_->pipelineCaches.size(); i > 0; --i) destroy(PipelineCacheHandle(i));
    for (u64 i = impl_->pipelineLayouts.size(); i > 0; --i) destroy(PipelineLayoutHandle(i));
    for (u64 i = impl_->bindGroups.size(); i > 0; --i) destroy(BindGroupHandle(i));
    for (u64 i = impl_->bindGroupLayouts.size(); i > 0; --i) destroy(BindGroupLayoutHandle(i));
    for (u64 i = impl_->queryPools.size(); i > 0; --i) destroy(QueryPoolHandle(i));
    for (u64 i = impl_->semaphores.size(); i > 0; --i) destroy(SemaphoreHandle(i));
    for (u64 i = impl_->fences.size(); i > 0; --i) destroy(FenceHandle(i));
    for (u64 i = impl_->shaders.size(); i > 0; --i) destroy(ShaderHandle(i));
    for (u64 i = impl_->samplers.size(); i > 0; --i) destroy(SamplerHandle(i));
    for (u64 i = impl_->textureViews.size(); i > 0; --i) destroy(TextureViewHandle(i));
    for (u64 i = impl_->textures.size(); i > 0; --i) destroy(TextureHandle(i));
    for (u64 i = impl_->buffers.size(); i > 0; --i) destroy(BufferHandle(i));

    impl_->native = {};
    impl_->context.Reset();
    impl_->device.Reset();
    impl_->adapter.Reset();
    impl_->factory.Reset();
}

bool D3D11Renderer::isInitialized() const noexcept {
    return impl_ != nullptr && impl_->device != nullptr && impl_->context != nullptr;
}

const RenderCapabilities& D3D11Renderer::capabilities() const noexcept {
    return impl_->caps;
}

const D3D11NativeHandles& D3D11Renderer::nativeHandles() const noexcept {
    return impl_->native;
}

BufferHandle D3D11Renderer::createBuffer(const BufferDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }
    if (desc.size == 0) {
        throw std::runtime_error("BufferDesc::size must be greater than zero");
    }

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = static_cast<UINT>(desc.size);
    if (hasAny(desc.usage, BufferUsage::Uniform)) {
        bufferDesc.ByteWidth = (bufferDesc.ByteWidth + 15u) & ~15u;
    }
    if (hasAny(desc.usage, BufferUsage::Storage)) {
        bufferDesc.ByteWidth = (bufferDesc.ByteWidth + 3u) & ~3u;
        bufferDesc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    }
    bufferDesc.Usage = toD3DUsage(desc.memoryUsage, desc.persistentlyMapped);
    bufferDesc.BindFlags = toBufferBindFlags(desc.usage, desc.memoryUsage);
    bufferDesc.CPUAccessFlags = toCpuAccessFlags(desc.memoryUsage, desc.persistentlyMapped);

    Impl::BufferResource resource{};
    resource.desc = desc;
    throwIfFailed(impl_->device->CreateBuffer(&bufferDesc, nullptr, &resource.buffer), "ID3D11Device::CreateBuffer failed");
    impl_->setDebugName(resource.buffer.Get(), desc.debugName);
    return makeRenderHandle<BufferHandle>(impl_->buffers, std::move(resource));
}

TextureHandle D3D11Renderer::createTexture(const TextureDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    const DXGI_FORMAT format = isDepthFormat(desc.format) || hasStencilFormat(desc.format)
        ? toTypelessDepthFormat(desc.format)
        : toDxgiFormat(desc.format);
    if (format == DXGI_FORMAT_UNKNOWN) {
        throw std::runtime_error("TextureDesc::format is not supported by D3D11");
    }

    Impl::TextureResource resource{};
    resource.desc = desc;
    resource.currentState = desc.initialState;

    if (desc.dimension == TextureDimension::Texture1D) {
        D3D11_TEXTURE1D_DESC textureDesc{};
        textureDesc.Width = desc.extent.width;
        textureDesc.MipLevels = desc.mipLevels;
        textureDesc.ArraySize = desc.arrayLayers;
        textureDesc.Format = format;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = toTextureBindFlags(desc.usage, MemoryUsage::GpuOnly);
        textureDesc.MiscFlags = hasAny(desc.flags, TextureCreateFlags::GenerateMips) ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        ComPtr<ID3D11Texture1D> texture;
        throwIfFailed(impl_->device->CreateTexture1D(&textureDesc, nullptr, &texture), "ID3D11Device::CreateTexture1D failed");
        impl_->setDebugName(texture.Get(), desc.debugName);
        resource.resource = texture;
    } else if (desc.dimension == TextureDimension::Texture3D) {
        D3D11_TEXTURE3D_DESC textureDesc{};
        textureDesc.Width = desc.extent.width;
        textureDesc.Height = desc.extent.height;
        textureDesc.Depth = desc.extent.depth;
        textureDesc.MipLevels = desc.mipLevels;
        textureDesc.Format = format;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = toTextureBindFlags(desc.usage, MemoryUsage::GpuOnly);
        textureDesc.MiscFlags = hasAny(desc.flags, TextureCreateFlags::GenerateMips) ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        ComPtr<ID3D11Texture3D> texture;
        throwIfFailed(impl_->device->CreateTexture3D(&textureDesc, nullptr, &texture), "ID3D11Device::CreateTexture3D failed");
        impl_->setDebugName(texture.Get(), desc.debugName);
        resource.resource = texture;
    } else {
        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = desc.extent.width;
        textureDesc.Height = desc.extent.height;
        textureDesc.MipLevels = desc.mipLevels;
        textureDesc.ArraySize = desc.arrayLayers;
        textureDesc.Format = format;
        textureDesc.SampleDesc.Count = toSampleCount(desc.samples);
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = toTextureBindFlags(desc.usage, MemoryUsage::GpuOnly);
        if (hasAny(desc.flags, TextureCreateFlags::CubeCompatible)) textureDesc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
        if (hasAny(desc.flags, TextureCreateFlags::GenerateMips)) textureDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> texture;
        throwIfFailed(impl_->device->CreateTexture2D(&textureDesc, nullptr, &texture), "ID3D11Device::CreateTexture2D failed");
        impl_->setDebugName(texture.Get(), desc.debugName);
        resource.resource = texture;
    }

    return makeRenderHandle<TextureHandle>(impl_->textures, std::move(resource));
}

TextureViewHandle D3D11Renderer::createTextureView(const TextureViewDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    const Impl::TextureResource* texture = getRenderResource(impl_->textures, desc.texture);
    if (texture == nullptr || texture->resource == nullptr) {
        throw std::runtime_error("TextureViewDesc::texture is invalid");
    }

    const Format viewFormat = desc.format == Format::Undefined ? texture->desc.format : desc.format;
    Impl::TextureViewResource resource{};
    resource.desc = desc;

    if (hasAny(texture->desc.usage, TextureUsage::Sampled)) {
        const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = makeTextureSrvDesc(texture->desc, desc, viewFormat);
        throwIfFailed(impl_->device->CreateShaderResourceView(texture->resource.Get(), &srvDesc, &resource.srv), "CreateShaderResourceView failed");
    }

    const bool wantsDepth = desc.aspect == TextureAspect::Depth ||
                            desc.aspect == TextureAspect::Stencil ||
                            desc.aspect == TextureAspect::All ||
                            isDepthFormat(viewFormat) ||
                            hasStencilFormat(viewFormat);
    if (hasAny(texture->desc.usage, TextureUsage::DepthStencilAttachment) && wantsDepth) {
        const D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = makeDsvDesc(texture->desc, desc, viewFormat);
        throwIfFailed(impl_->device->CreateDepthStencilView(texture->resource.Get(), &dsvDesc, &resource.dsv), "CreateDepthStencilView failed");
    }

    if (hasAny(texture->desc.usage, TextureUsage::ColorAttachment) || hasAny(texture->desc.usage, TextureUsage::Present)) {
        const D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = makeRtvDesc(texture->desc, desc, viewFormat);
        throwIfFailed(impl_->device->CreateRenderTargetView(texture->resource.Get(), &rtvDesc, &resource.rtv), "CreateRenderTargetView failed");
    }

    if (hasAny(texture->desc.usage, TextureUsage::Storage)) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = toDxgiFormat(viewFormat);
        if (texture->desc.dimension == TextureDimension::Texture3D) {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
            uavDesc.Texture3D.MipSlice = desc.baseMipLevel;
            uavDesc.Texture3D.FirstWSlice = desc.baseArrayLayer;
            uavDesc.Texture3D.WSize = desc.arrayLayerCount;
        } else if (texture->desc.arrayLayers > 1) {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.MipSlice = desc.baseMipLevel;
            uavDesc.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
            uavDesc.Texture2DArray.ArraySize = desc.arrayLayerCount;
        } else {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = desc.baseMipLevel;
        }
        throwIfFailed(impl_->device->CreateUnorderedAccessView(texture->resource.Get(), &uavDesc, &resource.uav), "CreateUnorderedAccessView failed");
    }

    const TextureViewHandle handle = makeRenderHandle<TextureViewHandle>(impl_->textureViews, std::move(resource));
    Impl::TextureViewResource& stored = impl_->textureViews.back();
    impl_->setDebugName(stored.srv.Get(), desc.debugName + ".SRV");
    impl_->setDebugName(stored.rtv.Get(), desc.debugName + ".RTV");
    impl_->setDebugName(stored.dsv.Get(), desc.debugName + ".DSV");
    impl_->setDebugName(stored.uav.Get(), desc.debugName + ".UAV");
    return handle;
}

SamplerHandle D3D11Renderer::createSampler(const SamplerDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = toD3DFilter(desc);
    samplerDesc.AddressU = toD3DAddressMode(desc.addressU);
    samplerDesc.AddressV = toD3DAddressMode(desc.addressV);
    samplerDesc.AddressW = toD3DAddressMode(desc.addressW);
    samplerDesc.MipLODBias = desc.mipLodBias;
    samplerDesc.MaxAnisotropy = static_cast<UINT>(std::clamp(desc.maxAnisotropy, 1.0F, 16.0F));
    samplerDesc.ComparisonFunc = toD3DCompare(desc.compareOp);
    samplerDesc.MinLOD = desc.minLod;
    samplerDesc.MaxLOD = desc.maxLod;
    switch (desc.borderColor) {
    case BorderColor::TransparentBlack:
        samplerDesc.BorderColor[0] = 0.0F;
        samplerDesc.BorderColor[1] = 0.0F;
        samplerDesc.BorderColor[2] = 0.0F;
        samplerDesc.BorderColor[3] = 0.0F;
        break;
    case BorderColor::OpaqueBlack:
        samplerDesc.BorderColor[3] = 1.0F;
        break;
    case BorderColor::OpaqueWhite:
        samplerDesc.BorderColor[0] = 1.0F;
        samplerDesc.BorderColor[1] = 1.0F;
        samplerDesc.BorderColor[2] = 1.0F;
        samplerDesc.BorderColor[3] = 1.0F;
        break;
    }

    Impl::SamplerResource resource{};
    resource.desc = desc;
    throwIfFailed(impl_->device->CreateSamplerState(&samplerDesc, &resource.sampler), "CreateSamplerState failed");
    impl_->setDebugName(resource.sampler.Get(), desc.debugName);
    return makeRenderHandle<SamplerHandle>(impl_->samplers, std::move(resource));
}

static std::vector<std::byte> compileHlsl(const ShaderDesc& desc) {
    if (!desc.bytecode.empty()) {
        return desc.bytecode;
    }
    if (desc.language != ShaderLanguage::Unknown && desc.language != ShaderLanguage::HLSL) {
        throw std::runtime_error("D3D11 shader modules require HLSL source or compiled DXBC bytecode");
    }

    const std::string profile = desc.compileOptions.targetProfile.empty()
        ? defaultProfileForStage(desc.stage)
        : desc.compileOptions.targetProfile;
    if (profile.empty()) {
        throw std::runtime_error("ShaderDesc::stage is not supported by D3D11");
    }

    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(desc.compileOptions.defines.size() + 1);
    for (const ShaderDefine& define : desc.compileOptions.defines) {
        macros.push_back({define.name.c_str(), define.value.c_str()});
    }
    macros.push_back({nullptr, nullptr});

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    if (desc.compileOptions.enableDebugInfo) flags |= D3DCOMPILE_DEBUG;
    if (desc.compileOptions.optimize) flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
    else flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
    if (desc.compileOptions.treatWarningsAsErrors) flags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = E_FAIL;
    const char* entryPoint = desc.entryPoint.empty() ? "main" : desc.entryPoint.c_str();
    if (!desc.source.empty()) {
        hr = D3DCompile(
            desc.source.data(),
            desc.source.size(),
            desc.debugName.empty() ? nullptr : desc.debugName.c_str(),
            macros.data(),
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint,
            profile.c_str(),
            flags,
            0,
            &bytecode,
            &errors);
    } else if (!desc.filePath.empty()) {
        const std::wstring path = toWideString(desc.filePath);
        hr = D3DCompileFromFile(
            path.c_str(),
            macros.data(),
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint,
            profile.c_str(),
            flags,
            0,
            &bytecode,
            &errors);
    } else {
        throw std::runtime_error("ShaderDesc requires bytecode, source, or filePath");
    }

    if (FAILED(hr)) {
        std::string message = "D3DCompile failed";
        if (errors) {
            message += ": ";
            message.append(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        }
        throw std::runtime_error(message);
    }

    std::vector<std::byte> result(bytecode->GetBufferSize());
    std::memcpy(result.data(), bytecode->GetBufferPointer(), result.size());
    return result;
}

ShaderHandle D3D11Renderer::createShaderModule(const ShaderDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    Impl::ShaderResource resource{};
    resource.desc = desc;
    resource.bytecode = compileHlsl(desc);
    if (resource.bytecode.empty()) {
        throw std::runtime_error("ShaderDesc has no bytecode");
    }

    const void* data = resource.bytecode.data();
    const SIZE_T size = resource.bytecode.size();
    switch (desc.stage) {
    case ShaderStage::Vertex:
        throwIfFailed(impl_->device->CreateVertexShader(data, size, nullptr, &resource.vertexShader), "CreateVertexShader failed");
        impl_->setDebugName(resource.vertexShader.Get(), desc.debugName);
        break;
    case ShaderStage::TessControl:
        throwIfFailed(impl_->device->CreateHullShader(data, size, nullptr, &resource.hullShader), "CreateHullShader failed");
        impl_->setDebugName(resource.hullShader.Get(), desc.debugName);
        break;
    case ShaderStage::TessEvaluation:
        throwIfFailed(impl_->device->CreateDomainShader(data, size, nullptr, &resource.domainShader), "CreateDomainShader failed");
        impl_->setDebugName(resource.domainShader.Get(), desc.debugName);
        break;
    case ShaderStage::Geometry:
        throwIfFailed(impl_->device->CreateGeometryShader(data, size, nullptr, &resource.geometryShader), "CreateGeometryShader failed");
        impl_->setDebugName(resource.geometryShader.Get(), desc.debugName);
        break;
    case ShaderStage::Fragment:
        throwIfFailed(impl_->device->CreatePixelShader(data, size, nullptr, &resource.pixelShader), "CreatePixelShader failed");
        impl_->setDebugName(resource.pixelShader.Get(), desc.debugName);
        break;
    case ShaderStage::Compute:
        throwIfFailed(impl_->device->CreateComputeShader(data, size, nullptr, &resource.computeShader), "CreateComputeShader failed");
        impl_->setDebugName(resource.computeShader.Get(), desc.debugName);
        break;
    default:
        throw std::runtime_error("ShaderDesc::stage is not supported by D3D11");
    }

    return makeRenderHandle<ShaderHandle>(impl_->shaders, std::move(resource));
}

BindGroupLayoutHandle D3D11Renderer::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    Impl::BindGroupLayoutResource resource{};
    resource.desc = desc;
    return makeRenderHandle<BindGroupLayoutHandle>(impl_->bindGroupLayouts, std::move(resource));
}

BindGroupHandle D3D11Renderer::createBindGroup(const BindGroupDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    const Impl::BindGroupLayoutResource* layout = getRenderResource(impl_->bindGroupLayouts, desc.layout);
    if (layout == nullptr) {
        throw std::runtime_error("BindGroupDesc::layout is invalid");
    }

    Impl::BindGroupResource resource{};
    resource.desc = desc;
    resource.bindings.reserve(desc.bindings.size());

    for (const ResourceBinding& binding : desc.bindings) {
        const BindGroupLayoutEntry* layoutEntry = impl_->findLayoutEntry(*layout, binding.binding);
        if (layoutEntry == nullptr) {
            throw std::runtime_error("ResourceBinding has no matching BindGroupLayoutEntry");
        }

        Impl::ResolvedBinding resolved{};
        resolved.slot = binding.binding;
        resolved.type = binding.type;
        resolved.visibility = layoutEntry->visibility;

        if (binding.type == BindingType::UniformBuffer) {
            if (binding.buffer.offset != 0) {
                throw std::runtime_error("D3D11 constant buffer offsets require D3D11.1 and are not implemented");
            }
            const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, binding.buffer.buffer);
            if (buffer == nullptr || !buffer->buffer) {
                throw std::runtime_error("ResourceBinding uniform buffer is invalid");
            }
            resolved.buffer = buffer->buffer;
        } else if (binding.type == BindingType::StorageBuffer) {
            const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, binding.buffer.buffer);
            if (buffer == nullptr || !buffer->buffer) {
                throw std::runtime_error("ResourceBinding storage buffer is invalid");
            }
            const u64 rangeSize = binding.buffer.size == WHOLE_SIZE
                ? buffer->desc.size - binding.buffer.offset
                : binding.buffer.size;
            if ((binding.buffer.offset % 4) != 0 || (rangeSize % 4) != 0) {
                throw std::runtime_error("D3D11 raw buffer views require 4-byte aligned offset and size");
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            srvDesc.BufferEx.FirstElement = static_cast<UINT>(binding.buffer.offset / 4);
            srvDesc.BufferEx.NumElements = static_cast<UINT>(rangeSize / 4);
            srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            throwIfFailed(impl_->device->CreateShaderResourceView(buffer->buffer.Get(), &srvDesc, &resolved.srv), "Create buffer SRV failed");

            if (layoutEntry->writable) {
                D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
                uavDesc.Buffer.FirstElement = static_cast<UINT>(binding.buffer.offset / 4);
                uavDesc.Buffer.NumElements = static_cast<UINT>(rangeSize / 4);
                uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
                throwIfFailed(impl_->device->CreateUnorderedAccessView(buffer->buffer.Get(), &uavDesc, &resolved.uav), "Create buffer UAV failed");
            }
        } else if (binding.type == BindingType::SampledTexture || binding.type == BindingType::StorageTexture || binding.type == BindingType::CombinedTextureSampler) {
            const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, binding.texture.view);
            if (view == nullptr) {
                throw std::runtime_error("ResourceBinding texture view is invalid");
            }
            resolved.srv = view->srv;
            resolved.uav = view->uav;
            if (binding.type == BindingType::CombinedTextureSampler) {
                const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
                if (sampler == nullptr || !sampler->sampler) {
                    throw std::runtime_error("CombinedTextureSampler requires a valid sampler");
                }
                resolved.sampler = sampler->sampler;
            }
        } else if (binding.type == BindingType::Sampler) {
            const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
            if (sampler == nullptr || !sampler->sampler) {
                throw std::runtime_error("ResourceBinding sampler is invalid");
            }
            resolved.sampler = sampler->sampler;
        } else if (binding.type == BindingType::PushConstant) {
            continue;
        } else {
            throw std::runtime_error("ResourceBinding type is not supported by D3D11");
        }

        resource.bindings.push_back(std::move(resolved));
    }

    return makeRenderHandle<BindGroupHandle>(impl_->bindGroups, std::move(resource));
}

PipelineLayoutHandle D3D11Renderer::createPipelineLayout(const PipelineLayoutDesc& desc) {
    for (BindGroupLayoutHandle handle : desc.bindGroupLayouts) {
        if (getRenderResource(impl_->bindGroupLayouts, handle) == nullptr) {
            throw std::runtime_error("PipelineLayoutDesc contains an invalid bind group layout");
        }
    }
    Impl::PipelineLayoutResource resource{};
    resource.desc = desc;
    return makeRenderHandle<PipelineLayoutHandle>(impl_->pipelineLayouts, std::move(resource));
}

PipelineCacheHandle D3D11Renderer::createPipelineCache(const PipelineCacheDesc& desc) {
    Impl::PipelineCacheResource resource{};
    resource.desc = desc;
    return makeRenderHandle<PipelineCacheHandle>(impl_->pipelineCaches, std::move(resource));
}

static D3D11_DEPTH_STENCILOP_DESC toD3DStencilFace(const StencilFaceState& state) {
    D3D11_DEPTH_STENCILOP_DESC desc{};
    desc.StencilFailOp = toD3DStencilOp(state.failOp);
    desc.StencilDepthFailOp = toD3DStencilOp(state.depthFailOp);
    desc.StencilPassOp = toD3DStencilOp(state.passOp);
    desc.StencilFunc = toD3DCompare(state.compareOp);
    return desc;
}

PipelineHandle D3D11Renderer::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }
    if (getRenderResource(impl_->pipelineLayouts, desc.layout) == nullptr) {
        throw std::runtime_error("GraphicsPipelineDesc::layout is invalid");
    }

    Impl::PipelineResource resource{};
    resource.compute = false;
    resource.topology = toD3DTopology(desc.inputAssembly);
    resource.stencilRef = desc.depthStencil.front.reference;
    resource.blendConstants = desc.blend.blendConstants;
    resource.sampleMask = static_cast<UINT>(desc.multisample.sampleMask & 0xFFFFFFFFu);

    std::vector<ShaderHandle> temporaryShaders;
    ShaderHandle vertexShaderHandle{};

    try {
        for (const ShaderDesc& shaderDesc : desc.shaders) {
            ShaderHandle handle = createShaderModule(shaderDesc);
            temporaryShaders.push_back(handle);
            const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, handle);
            switch (shaderDesc.stage) {
            case ShaderStage::Vertex:
                resource.vertexShader = shader->vertexShader;
                vertexShaderHandle = handle;
                break;
            case ShaderStage::TessControl:
                resource.hullShader = shader->hullShader;
                break;
            case ShaderStage::TessEvaluation:
                resource.domainShader = shader->domainShader;
                break;
            case ShaderStage::Geometry:
                resource.geometryShader = shader->geometryShader;
                break;
            case ShaderStage::Fragment:
                resource.pixelShader = shader->pixelShader;
                break;
            default:
                throw std::runtime_error("Graphics pipeline contains a non-graphics shader stage");
            }
        }

        const Impl::ShaderResource* vertexShader = getRenderResource(impl_->shaders, vertexShaderHandle);
        if (vertexShader == nullptr || !resource.vertexShader) {
            throw std::runtime_error("GraphicsPipelineDesc requires a vertex shader");
        }

        size_t vertexAttributeCount = 0;
        for (const VertexBufferLayoutDesc& vertexBuffer : desc.vertexBuffers) {
            vertexAttributeCount += vertexBuffer.attributes.size();
        }

        std::vector<std::string> semanticNames;
        semanticNames.reserve(vertexAttributeCount);
        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        elements.reserve(vertexAttributeCount);
        for (const VertexBufferLayoutDesc& vertexBuffer : desc.vertexBuffers) {
            for (const VertexAttributeDesc& attribute : vertexBuffer.attributes) {
                semanticNames.push_back(attribute.semanticName.empty() ? "TEXCOORD" : attribute.semanticName);
                D3D11_INPUT_ELEMENT_DESC element{};
                element.SemanticName = semanticNames.back().c_str();
                element.SemanticIndex = attribute.semanticName.empty() ? attribute.location : attribute.semanticIndex;
                element.Format = toD3DVertexFormat(attribute.format);
                element.InputSlot = attribute.binding;
                element.AlignedByteOffset = static_cast<UINT>(attribute.offset);
                element.InputSlotClass = vertexBuffer.inputRate == VertexInputRate::PerInstance
                    ? D3D11_INPUT_PER_INSTANCE_DATA
                    : D3D11_INPUT_PER_VERTEX_DATA;
                element.InstanceDataStepRate = vertexBuffer.inputRate == VertexInputRate::PerInstance ? vertexBuffer.stepRate : 0;
                elements.push_back(element);
            }
        }

        if (!elements.empty()) {
            throwIfFailed(
                impl_->device->CreateInputLayout(
                    elements.data(),
                    static_cast<UINT>(elements.size()),
                    vertexShader->bytecode.data(),
                    vertexShader->bytecode.size(),
                    &resource.inputLayout),
                "CreateInputLayout failed");
        }

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = desc.raster.polygonMode == PolygonMode::Line ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        switch (desc.raster.cullMode) {
        case CullMode::None: rasterDesc.CullMode = D3D11_CULL_NONE; break;
        case CullMode::Front: rasterDesc.CullMode = D3D11_CULL_FRONT; break;
        case CullMode::Back:
        case CullMode::FrontAndBack: rasterDesc.CullMode = D3D11_CULL_BACK; break;
        }
        rasterDesc.FrontCounterClockwise = desc.raster.frontFace == FrontFace::CounterClockwise;
        rasterDesc.DepthBias = static_cast<INT>(desc.raster.depthBiasConstantFactor);
        rasterDesc.DepthBiasClamp = desc.raster.depthBiasClamp;
        rasterDesc.SlopeScaledDepthBias = desc.raster.depthBiasSlopeFactor;
        rasterDesc.DepthClipEnable = !desc.raster.depthClampEnable;
        rasterDesc.ScissorEnable = true;
        rasterDesc.MultisampleEnable = desc.multisample.samples != SampleCount::Count1;
        rasterDesc.AntialiasedLineEnable = false;
        throwIfFailed(impl_->device->CreateRasterizerState(&rasterDesc, &resource.rasterizerState), "CreateRasterizerState failed");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = desc.depthStencil.depthTestEnable;
        depthDesc.DepthWriteMask = desc.depthStencil.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc = toD3DCompare(desc.depthStencil.depthCompareOp);
        depthDesc.StencilEnable = desc.depthStencil.stencilTestEnable;
        depthDesc.StencilReadMask = static_cast<UINT8>(desc.depthStencil.front.compareMask & 0xFFu);
        depthDesc.StencilWriteMask = static_cast<UINT8>(desc.depthStencil.front.writeMask & 0xFFu);
        depthDesc.FrontFace = toD3DStencilFace(desc.depthStencil.front);
        depthDesc.BackFace = toD3DStencilFace(desc.depthStencil.back);
        throwIfFailed(impl_->device->CreateDepthStencilState(&depthDesc, &resource.depthStencilState), "CreateDepthStencilState failed");

        D3D11_BLEND_DESC blendDesc{};
        blendDesc.AlphaToCoverageEnable = desc.multisample.alphaToCoverageEnable;
        blendDesc.IndependentBlendEnable = desc.blend.attachments.size() > 1;
        const size_t attachmentCount = std::max<size_t>(1, std::min<size_t>(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, desc.blend.attachments.empty() ? desc.colorFormats.size() : desc.blend.attachments.size()));
        for (size_t i = 0; i < attachmentCount; ++i) {
            const ColorBlendAttachmentState src = desc.blend.attachments.empty() ? ColorBlendAttachmentState{} : desc.blend.attachments[i];
            D3D11_RENDER_TARGET_BLEND_DESC& target = blendDesc.RenderTarget[i];
            target.BlendEnable = src.blendEnable;
            target.SrcBlend = toD3DBlend(src.sourceColor);
            target.DestBlend = toD3DBlend(src.destinationColor);
            target.BlendOp = toD3DBlendOp(src.colorOp);
            target.SrcBlendAlpha = toD3DBlend(src.sourceAlpha);
            target.DestBlendAlpha = toD3DBlend(src.destinationAlpha);
            target.BlendOpAlpha = toD3DBlendOp(src.alphaOp);
            target.RenderTargetWriteMask = toD3DColorWriteMask(src.writeMask);
        }
        for (size_t i = attachmentCount; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        }
        throwIfFailed(impl_->device->CreateBlendState(&blendDesc, &resource.blendState), "CreateBlendState failed");

        impl_->setDebugName(resource.inputLayout.Get(), desc.debugName + ".InputLayout");
        impl_->setDebugName(resource.rasterizerState.Get(), desc.debugName + ".Rasterizer");
        impl_->setDebugName(resource.depthStencilState.Get(), desc.debugName + ".DepthStencil");
        impl_->setDebugName(resource.blendState.Get(), desc.debugName + ".Blend");
    } catch (...) {
        for (ShaderHandle handle : temporaryShaders) {
            destroy(handle);
        }
        throw;
    }

    for (ShaderHandle handle : temporaryShaders) {
        destroy(handle);
    }
    return makeRenderHandle<PipelineHandle>(impl_->pipelines, std::move(resource));
}

PipelineHandle D3D11Renderer::createComputePipeline(const ComputePipelineDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }
    if (getRenderResource(impl_->pipelineLayouts, desc.layout) == nullptr) {
        throw std::runtime_error("ComputePipelineDesc::layout is invalid");
    }

    ShaderHandle shaderHandle = createShaderModule(desc.shader);
    const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, shaderHandle);
    if (shader == nullptr || !shader->computeShader) {
        destroy(shaderHandle);
        throw std::runtime_error("ComputePipelineDesc requires a compute shader");
    }

    Impl::PipelineResource resource{};
    resource.compute = true;
    resource.computeShader = shader->computeShader;
    destroy(shaderHandle);
    return makeRenderHandle<PipelineHandle>(impl_->pipelines, std::move(resource));
}

QueryPoolHandle D3D11Renderer::createQueryPool(const QueryPoolDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    Impl::QueryPoolResource resource{};
    resource.desc = desc;
    resource.queries.reserve(desc.queryCount);

    D3D11_QUERY_DESC queryDesc{};
    switch (desc.type) {
    case QueryType::Timestamp: queryDesc.Query = D3D11_QUERY_TIMESTAMP; break;
    case QueryType::Occlusion: queryDesc.Query = D3D11_QUERY_OCCLUSION; break;
    case QueryType::PipelineStatistics: queryDesc.Query = D3D11_QUERY_PIPELINE_STATISTICS; break;
    }

    for (u32 i = 0; i < desc.queryCount; ++i) {
        ComPtr<ID3D11Query> query;
        throwIfFailed(impl_->device->CreateQuery(&queryDesc, &query), "CreateQuery failed");
        resource.queries.push_back(query);
    }
    return makeRenderHandle<QueryPoolHandle>(impl_->queryPools, std::move(resource));
}

SemaphoreHandle D3D11Renderer::createSemaphore(const SemaphoreDesc& desc) {
    Impl::SemaphoreResource resource{};
    resource.desc = desc;
    resource.value = desc.initialValue;
    resource.signaled = desc.type == SemaphoreType::Timeline && desc.initialValue > 0;
    return makeRenderHandle<SemaphoreHandle>(impl_->semaphores, std::move(resource));
}

FenceHandle D3D11Renderer::createFence(const FenceDesc& desc) {
    Impl::FenceResource resource{};
    resource.desc = desc;
    resource.signaled = desc.signaled;
    return makeRenderHandle<FenceHandle>(impl_->fences, std::move(resource));
}

SwapchainHandle D3D11Renderer::createSwapchain(const SwapchainDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }
    if (impl_->initDesc.surface.hwnd == nullptr) {
        throw std::runtime_error("D3D11 swapchain creation requires D3D11SurfaceDesc::hwnd");
    }

    DXGI_SWAP_CHAIN_DESC swapDesc{};
    swapDesc.BufferDesc.Width = desc.extent.width;
    swapDesc.BufferDesc.Height = desc.extent.height;
    swapDesc.BufferDesc.Format = toSwapchainFormat(desc.preferredFormat);
    swapDesc.BufferDesc.RefreshRate.Numerator = 0;
    swapDesc.BufferDesc.RefreshRate.Denominator = 1;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_SHADER_INPUT;
    swapDesc.BufferCount = std::max(1u, desc.imageCount);
    swapDesc.OutputWindow = impl_->initDesc.surface.hwnd;
    swapDesc.Windowed = !desc.fullscreen;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    swapDesc.Flags = desc.fullscreen ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;

    Impl::SwapchainResource resource{};
    resource.desc = desc;
    throwIfFailed(impl_->factory->CreateSwapChain(impl_->device.Get(), &swapDesc, &resource.swapchain), "IDXGIFactory::CreateSwapChain failed");
    impl_->factory->MakeWindowAssociation(impl_->initDesc.surface.hwnd, DXGI_MWA_NO_ALT_ENTER);
    resource.format = fromDxgiFormat(swapDesc.BufferDesc.Format);
    resource.extent = desc.extent;

    ComPtr<ID3D11Texture2D> backBuffer;
    throwIfFailed(resource.swapchain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "IDXGISwapChain::GetBuffer failed");

    TextureDesc textureDesc{};
    textureDesc.debugName = desc.debugName + ".BackBuffer";
    textureDesc.dimension = TextureDimension::Texture2D;
    textureDesc.extent = {desc.extent.width, desc.extent.height, 1};
    textureDesc.arrayLayers = 1;
    textureDesc.mipLevels = 1;
    textureDesc.format = resource.format == Format::Undefined ? Format::BGRA8_UNorm : resource.format;
    textureDesc.samples = SampleCount::Count1;
    textureDesc.usage = TextureUsage::ColorAttachment | TextureUsage::Sampled | TextureUsage::Present;
    textureDesc.initialState = ResourceState::Present;

    Impl::TextureResource textureResource{};
    textureResource.desc = textureDesc;
    textureResource.resource = backBuffer;
    textureResource.currentState = ResourceState::Present;
    textureResource.swapchainImage = true;
    TextureHandle textureHandle = makeRenderHandle<TextureHandle>(impl_->textures, std::move(textureResource));

    TextureViewDesc viewDesc{};
    viewDesc.debugName = desc.debugName + ".BackBufferView";
    viewDesc.texture = textureHandle;
    viewDesc.dimension = TextureViewDimension::View2D;
    viewDesc.format = textureDesc.format;
    viewDesc.aspect = TextureAspect::Color;

    Impl::TextureViewResource viewResource{};
    viewResource.desc = viewDesc;
    throwIfFailed(impl_->device->CreateRenderTargetView(backBuffer.Get(), nullptr, &viewResource.rtv), "Create backbuffer RTV failed");
    throwIfFailed(impl_->device->CreateShaderResourceView(backBuffer.Get(), nullptr, &viewResource.srv), "Create backbuffer SRV failed");
    TextureViewHandle viewHandle = makeRenderHandle<TextureViewHandle>(impl_->textureViews, std::move(viewResource));

    resource.images = {textureHandle};
    resource.imageViews = {viewHandle};
    return makeRenderHandle<SwapchainHandle>(impl_->swapchains, std::move(resource));
}

std::vector<TextureHandle> D3D11Renderer::getSwapchainImages(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->images;
    }
    return {};
}

std::vector<TextureViewHandle> D3D11Renderer::getSwapchainImageViews(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->imageViews;
    }
    return {};
}

Format D3D11Renderer::getSwapchainFormat(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->format;
    }
    return Format::Undefined;
}

Extent2D D3D11Renderer::getSwapchainExtent(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->extent;
    }
    return {};
}

static bool stageVisible(ShaderStage visibility, ShaderStage stage) {
    if (visibility == ShaderStage::All) {
        return true;
    }
    if (visibility == ShaderStage::AllGraphics) {
        return stage == ShaderStage::Vertex ||
               stage == ShaderStage::TessControl ||
               stage == ShaderStage::TessEvaluation ||
               stage == ShaderStage::Geometry ||
               stage == ShaderStage::Fragment;
    }
    return hasAny(visibility, stage);
}

static void setConstantBufferForStages(ID3D11DeviceContext* context, ShaderStage visibility, UINT slot, ID3D11Buffer* buffer) {
    if (stageVisible(visibility, ShaderStage::Vertex)) context->VSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, ShaderStage::TessControl)) context->HSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, ShaderStage::TessEvaluation)) context->DSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, ShaderStage::Geometry)) context->GSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, ShaderStage::Fragment)) context->PSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, ShaderStage::Compute)) context->CSSetConstantBuffers(slot, 1, &buffer);
}

static void setSrvForStages(ID3D11DeviceContext* context, ShaderStage visibility, UINT slot, ID3D11ShaderResourceView* srv) {
    if (stageVisible(visibility, ShaderStage::Vertex)) context->VSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, ShaderStage::TessControl)) context->HSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, ShaderStage::TessEvaluation)) context->DSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, ShaderStage::Geometry)) context->GSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, ShaderStage::Fragment)) context->PSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, ShaderStage::Compute)) context->CSSetShaderResources(slot, 1, &srv);
}

static void setSamplerForStages(ID3D11DeviceContext* context, ShaderStage visibility, UINT slot, ID3D11SamplerState* sampler) {
    if (stageVisible(visibility, ShaderStage::Vertex)) context->VSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, ShaderStage::TessControl)) context->HSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, ShaderStage::TessEvaluation)) context->DSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, ShaderStage::Geometry)) context->GSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, ShaderStage::Fragment)) context->PSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, ShaderStage::Compute)) context->CSSetSamplers(slot, 1, &sampler);
}

template <typename BindGroupResourceT>
static void applyBindGroup(ID3D11DeviceContext* context, const BindGroupResourceT& bindGroup, bool compute) {
    for (const auto& binding : bindGroup.bindings) {
        ID3D11Buffer* buffer = binding.buffer.Get();
        ID3D11ShaderResourceView* srv = binding.srv.Get();
        ID3D11SamplerState* sampler = binding.sampler.Get();
        ID3D11UnorderedAccessView* uav = binding.uav.Get();

        switch (binding.type) {
        case BindingType::UniformBuffer:
            setConstantBufferForStages(context, binding.visibility, binding.slot, buffer);
            break;
        case BindingType::StorageBuffer:
        case BindingType::SampledTexture:
        case BindingType::CombinedTextureSampler:
            if (srv != nullptr) {
                setSrvForStages(context, binding.visibility, binding.slot, srv);
            }
            if (sampler != nullptr) {
                setSamplerForStages(context, binding.visibility, binding.slot, sampler);
            }
            break;
        case BindingType::StorageTexture:
            if (compute && uav != nullptr) {
                UINT initialCount = 0;
                context->CSSetUnorderedAccessViews(binding.slot, 1, &uav, &initialCount);
            } else if (srv != nullptr) {
                setSrvForStages(context, binding.visibility, binding.slot, srv);
            }
            break;
        case BindingType::Sampler:
            if (sampler != nullptr) {
                setSamplerForStages(context, binding.visibility, binding.slot, sampler);
            }
            break;
        default:
            break;
        }
    }
}

template <typename PipelineResourceT>
static void applyPipeline(ID3D11DeviceContext* context, const PipelineResourceT& pipeline) {
    if (pipeline.compute) {
        context->CSSetShader(pipeline.computeShader.Get(), nullptr, 0);
        return;
    }

    context->IASetPrimitiveTopology(pipeline.topology);
    context->IASetInputLayout(pipeline.inputLayout.Get());
    context->VSSetShader(pipeline.vertexShader.Get(), nullptr, 0);
    context->HSSetShader(pipeline.hullShader.Get(), nullptr, 0);
    context->DSSetShader(pipeline.domainShader.Get(), nullptr, 0);
    context->GSSetShader(pipeline.geometryShader.Get(), nullptr, 0);
    context->PSSetShader(pipeline.pixelShader.Get(), nullptr, 0);
    context->RSSetState(pipeline.rasterizerState.Get());
    context->OMSetDepthStencilState(pipeline.depthStencilState.Get(), pipeline.stencilRef);
    context->OMSetBlendState(pipeline.blendState.Get(), pipeline.blendConstants.data(), pipeline.sampleMask);
}

bool D3D11Renderer::acquireNextImage(
    SwapchainHandle swapchain,
    SemaphoreHandle signalSemaphore,
    FenceHandle signalFence,
    u32* imageIndex,
    std::string* errorMessage) {
    try {
        if (getRenderResource(impl_->swapchains, swapchain) == nullptr) {
            throw std::runtime_error("acquireNextImage swapchain is invalid");
        }
        if (imageIndex != nullptr) {
            *imageIndex = 0;
        }
        if (Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, signalSemaphore)) {
            semaphore->signaled = true;
            if (semaphore->desc.type == SemaphoreType::Timeline) {
                ++semaphore->value;
            }
        }
        if (Impl::FenceResource* fence = getRenderResource(impl_->fences, signalFence)) {
            fence->signaled = true;
        }
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool D3D11Renderer::submit(const QueueSubmitDesc& desc, std::string* errorMessage) {
    try {
        for (const QueueWaitDesc& wait : desc.waits) {
            const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, wait.semaphore);
            if (semaphore == nullptr) {
                throw std::runtime_error("QueueSubmitDesc contains an invalid wait semaphore");
            }
            if (semaphore->desc.type == SemaphoreType::Timeline && semaphore->value < wait.value) {
                throw std::runtime_error("D3D11 timeline semaphore wait value has not been reached");
            }
        }

        impl_->context->Flush();

        for (const QueueSignalDesc& signal : desc.signals) {
            Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, signal.semaphore);
            if (semaphore == nullptr) {
                throw std::runtime_error("QueueSubmitDesc contains an invalid signal semaphore");
            }
            semaphore->signaled = true;
            semaphore->value = semaphore->desc.type == SemaphoreType::Timeline ? signal.value : semaphore->value + 1;
        }

        if (Impl::FenceResource* fence = getRenderResource(impl_->fences, desc.fence)) {
            D3D11_QUERY_DESC queryDesc{};
            queryDesc.Query = D3D11_QUERY_EVENT;
            throwIfFailed(impl_->device->CreateQuery(&queryDesc, &fence->eventQuery), "Create fence event query failed");
            impl_->context->End(fence->eventQuery.Get());
            fence->signaled = true;
        }
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool D3D11Renderer::present(const PresentDesc& desc, std::string* errorMessage) {
    try {
        Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, desc.swapchain);
        if (swapchain == nullptr || !swapchain->swapchain) {
            throw std::runtime_error("PresentDesc::swapchain is invalid");
        }

        const UINT syncInterval = desc.presentMode == PresentMode::Immediate || desc.allowTearing ? 0u : 1u;
        throwIfFailed(swapchain->swapchain->Present(syncInterval, 0), "IDXGISwapChain::Present failed");
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool D3D11Renderer::recordAndSubmitFrame(const FramePacket& packet, std::string* errorMessage) {
    try {
        for (const BufferUploadDesc& upload : packet.uploads.buffers) {
            if (upload.data.empty()) {
                continue;
            }
            Impl::BufferResource* buffer = getRenderResource(impl_->buffers, upload.destination);
            if (buffer == nullptr || !buffer->buffer) {
                throw std::runtime_error("FramePacket buffer upload destination is invalid");
            }

            if (buffer->desc.memoryUsage == MemoryUsage::CpuToGpu || buffer->desc.persistentlyMapped) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                throwIfFailed(impl_->context->Map(buffer->buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map buffer upload failed");
                std::memcpy(static_cast<std::byte*>(mapped.pData) + upload.destinationOffset, upload.data.data(), upload.data.size());
                impl_->context->Unmap(buffer->buffer.Get(), 0);
            } else {
                D3D11_BOX box{};
                box.left = static_cast<UINT>(upload.destinationOffset);
                box.right = static_cast<UINT>(upload.destinationOffset + upload.data.size());
                box.top = 0;
                box.bottom = 1;
                box.front = 0;
                box.back = 1;
                impl_->context->UpdateSubresource(buffer->buffer.Get(), 0, &box, upload.data.data(), 0, 0);
            }
        }

        for (const TextureUploadDesc& upload : packet.uploads.textures) {
            if (upload.data.empty()) {
                continue;
            }
            Impl::TextureResource* texture = getRenderResource(impl_->textures, upload.destination);
            if (texture == nullptr || !texture->resource) {
                throw std::runtime_error("FramePacket texture upload destination is invalid");
            }
            const UINT rowPitch = static_cast<UINT>(upload.bytesPerRow == 0 ? rowPitchForFormat(texture->desc.format, upload.extent.width) : upload.bytesPerRow);
            const UINT rows = static_cast<UINT>(upload.rowsPerImage == 0 ? upload.extent.height : upload.rowsPerImage);
            D3D11_BOX box{};
            box.left = static_cast<UINT>(upload.offset.x);
            box.top = static_cast<UINT>(upload.offset.y);
            box.front = static_cast<UINT>(upload.offset.z);
            box.right = box.left + upload.extent.width;
            box.bottom = box.top + upload.extent.height;
            box.back = box.front + upload.extent.depth;
            const UINT subresource = D3D11CalcSubresource(upload.mipLevel, upload.arrayLayer, texture->desc.mipLevels);
            impl_->context->UpdateSubresource(texture->resource.Get(), subresource, &box, upload.data.data(), rowPitch, rowPitch * rows);
        }

        std::unordered_map<std::string, TextureHandle> textureResources;
        for (const RenderGraphTextureDesc& texture : packet.graph.textures) {
            if (texture.imported && texture.externalHandle) {
                textureResources[texture.name] = texture.externalHandle;
            }
        }

        const auto textureForName = [&](const std::string& name) -> TextureHandle {
            const auto it = textureResources.find(name);
            return it == textureResources.end() ? TextureHandle{} : it->second;
        };

        const auto findViewForTexture = [&](TextureHandle texture, TextureAspect aspect) -> TextureViewHandle {
            for (u64 index = 0; index < impl_->textureViews.size(); ++index) {
                const Impl::TextureViewResource& view = impl_->textureViews[static_cast<size_t>(index)];
                if (view.desc.texture == texture &&
                    (aspect == TextureAspect::All || view.desc.aspect == aspect || view.desc.aspect == TextureAspect::All)) {
                    return TextureViewHandle(index + 1);
                }
            }
            return {};
        };

        const auto findWorkload = [&](const std::string& passName) -> const RenderPassWorkload* {
            const auto it = std::find_if(packet.workloads.begin(), packet.workloads.end(), [&](const RenderPassWorkload& workload) {
                return workload.passName == passName;
            });
            return it == packet.workloads.end() ? nullptr : &*it;
        };

        const auto bindDrawResources = [&](const std::vector<BindGroupHandle>& bindGroups, bool compute) {
            for (BindGroupHandle bindGroupHandle : bindGroups) {
                const Impl::BindGroupResource* bindGroup = getRenderResource(impl_->bindGroups, bindGroupHandle);
                if (bindGroup == nullptr) {
                    throw std::runtime_error("Draw/dispatch bind group is invalid");
                }
                applyBindGroup(impl_->context.Get(), *bindGroup, compute);
            }
        };

        const auto bindVertexStreams = [&](const std::vector<VertexStream>& streams) {
            for (const VertexStream& stream : streams) {
                const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, stream.buffer);
                if (buffer == nullptr || !buffer->buffer) {
                    throw std::runtime_error("VertexStream buffer is invalid");
                }
                ID3D11Buffer* nativeBuffer = buffer->buffer.Get();
                UINT stride = static_cast<UINT>(stream.stride);
                UINT offset = static_cast<UINT>(stream.offset);
                impl_->context->IASetVertexBuffers(stream.binding, 1, &nativeBuffer, &stride, &offset);
            }
        };

        const auto recordDraw = [&](const DrawCommand& draw) {
            const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, draw.pipeline);
            if (pipeline == nullptr || pipeline->compute) {
                throw std::runtime_error("DrawCommand pipeline is invalid");
            }
            applyPipeline(impl_->context.Get(), *pipeline);
            bindDrawResources(draw.bindGroups, false);
            bindVertexStreams(draw.vertexStreams);
            impl_->context->DrawInstanced(draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance);
        };

        const auto recordIndexedDraw = [&](const DrawIndexedCommand& draw) {
            const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, draw.pipeline);
            if (pipeline == nullptr || pipeline->compute) {
                throw std::runtime_error("DrawIndexedCommand pipeline is invalid");
            }
            applyPipeline(impl_->context.Get(), *pipeline);
            bindDrawResources(draw.bindGroups, false);
            bindVertexStreams(draw.vertexStreams);

            const Impl::BufferResource* indexBuffer = getRenderResource(impl_->buffers, draw.indexStream.buffer);
            if (indexBuffer == nullptr || !indexBuffer->buffer) {
                throw std::runtime_error("DrawIndexedCommand index buffer is invalid");
            }
            const DXGI_FORMAT indexFormat = draw.indexStream.indexType == IndexType::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            impl_->context->IASetIndexBuffer(indexBuffer->buffer.Get(), indexFormat, static_cast<UINT>(draw.indexStream.offset));
            impl_->context->DrawIndexedInstanced(
                draw.indexCount,
                draw.instanceCount,
                draw.firstIndex,
                draw.vertexOffsetElements,
                draw.firstInstance);
        };

        const auto recordDispatch = [&](const DispatchCommand& dispatch) {
            const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, dispatch.pipeline);
            if (pipeline == nullptr || !pipeline->compute) {
                throw std::runtime_error("DispatchCommand pipeline is invalid");
            }
            applyPipeline(impl_->context.Get(), *pipeline);
            bindDrawResources(dispatch.bindGroups, true);
            impl_->context->Dispatch(dispatch.groupCountX, dispatch.groupCountY, dispatch.groupCountZ);
        };

        for (const RenderGraphPassDesc& pass : packet.graph.passes) {
            const RenderPassWorkload* workload = findWorkload(pass.name);
            if (workload == nullptr) {
                continue;
            }

            std::vector<ID3D11RenderTargetView*> rtvs;
            rtvs.reserve(pass.colorAttachments.size());
            for (const RenderGraphAttachmentDesc& attachment : pass.colorAttachments) {
                const TextureViewHandle viewHandle = findViewForTexture(textureForName(attachment.resourceName), TextureAspect::Color);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || !view->rtv) {
                    throw std::runtime_error("RenderGraph color attachment has no D3D11 RTV");
                }
                ID3D11RenderTargetView* rtv = view->rtv.Get();
                rtvs.push_back(rtv);
                if (attachment.loadOp == LoadOp::Clear) {
                    const std::array<float, 4> clear = {
                        attachment.clearValue.color.r,
                        attachment.clearValue.color.g,
                        attachment.clearValue.color.b,
                        attachment.clearValue.color.a
                    };
                    impl_->context->ClearRenderTargetView(rtv, clear.data());
                }
            }

            ID3D11DepthStencilView* dsv = nullptr;
            if (pass.depthStencilAttachment.has_value()) {
                const RenderGraphAttachmentDesc& attachment = *pass.depthStencilAttachment;
                const TextureViewHandle viewHandle = findViewForTexture(textureForName(attachment.resourceName), TextureAspect::Depth);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || !view->dsv) {
                    throw std::runtime_error("RenderGraph depth attachment has no D3D11 DSV");
                }
                dsv = view->dsv.Get();
                if (attachment.loadOp == LoadOp::Clear) {
                    impl_->context->ClearDepthStencilView(
                        dsv,
                        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                        attachment.clearValue.depthStencil.depth,
                        static_cast<UINT8>(attachment.clearValue.depthStencil.stencil));
                }
            }

            if (!rtvs.empty() || dsv != nullptr) {
                impl_->context->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.empty() ? nullptr : rtvs.data(), dsv);
            }

            const Viewport viewport = workload->viewport.width == 0.0F || workload->viewport.height == 0.0F
                ? packet.settings.viewport
                : workload->viewport;
            D3D11_VIEWPORT d3dViewport{};
            d3dViewport.TopLeftX = viewport.x;
            d3dViewport.TopLeftY = viewport.y;
            d3dViewport.Width = viewport.width;
            d3dViewport.Height = viewport.height;
            d3dViewport.MinDepth = viewport.minDepth;
            d3dViewport.MaxDepth = viewport.maxDepth;
            impl_->context->RSSetViewports(1, &d3dViewport);

            const Rect2D scissor = workload->scissor.extent.width == 0 || workload->scissor.extent.height == 0
                ? packet.settings.scissor
                : workload->scissor;
            D3D11_RECT d3dScissor{};
            d3dScissor.left = scissor.offset.x;
            d3dScissor.top = scissor.offset.y;
            d3dScissor.right = scissor.offset.x + static_cast<LONG>(scissor.extent.width);
            d3dScissor.bottom = scissor.offset.y + static_cast<LONG>(scissor.extent.height);
            impl_->context->RSSetScissorRects(1, &d3dScissor);

            for (const DrawCommand& draw : workload->draws) {
                recordDraw(draw);
            }
            for (const DrawIndexedCommand& draw : workload->indexedDraws) {
                recordIndexedDraw(draw);
            }
            for (const DispatchCommand& dispatch : workload->dispatches) {
                recordDispatch(dispatch);
            }
        }

        for (const QueueSubmitDesc& submitDesc : packet.submissions) {
            if (!submit(submitDesc, errorMessage)) {
                return false;
            }
        }

        if (packet.present.has_value()) {
            return present(*packet.present, errorMessage);
        }
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool D3D11Renderer::submitFrame(const FramePacket& packet, std::string* errorMessage) {
    if (!packet.workloads.empty()) {
        return recordAndSubmitFrame(packet, errorMessage);
    }
    for (const QueueSubmitDesc& submitDesc : packet.submissions) {
        if (!submit(submitDesc, errorMessage)) {
            return false;
        }
    }
    if (packet.present.has_value()) {
        return present(*packet.present, errorMessage);
    }
    return true;
}

void D3D11Renderer::waitIdle() const noexcept {
    if (!isInitialized()) {
        return;
    }

    D3D11_QUERY_DESC queryDesc{};
    queryDesc.Query = D3D11_QUERY_EVENT;
    ComPtr<ID3D11Query> query;
    if (FAILED(impl_->device->CreateQuery(&queryDesc, &query))) {
        impl_->context->Flush();
        return;
    }
    impl_->context->End(query.Get());
    BOOL done = FALSE;
    while (impl_->context->GetData(query.Get(), &done, sizeof(done), 0) == S_FALSE) {
        Sleep(0);
    }
}

void D3D11Renderer::destroy(BufferHandle handle) noexcept {
    if (Impl::BufferResource* resource = getRenderResource(impl_->buffers, handle)) {
        resource->buffer.Reset();
    }
}

void D3D11Renderer::destroy(TextureHandle handle) noexcept {
    if (Impl::TextureResource* resource = getRenderResource(impl_->textures, handle)) {
        resource->resource.Reset();
    }
}

void D3D11Renderer::destroy(TextureViewHandle handle) noexcept {
    if (Impl::TextureViewResource* resource = getRenderResource(impl_->textureViews, handle)) {
        resource->srv.Reset();
        resource->rtv.Reset();
        resource->dsv.Reset();
        resource->uav.Reset();
    }
}

void D3D11Renderer::destroy(SamplerHandle handle) noexcept {
    if (Impl::SamplerResource* resource = getRenderResource(impl_->samplers, handle)) {
        resource->sampler.Reset();
    }
}

void D3D11Renderer::destroy(ShaderHandle handle) noexcept {
    if (Impl::ShaderResource* resource = getRenderResource(impl_->shaders, handle)) {
        resource->vertexShader.Reset();
        resource->hullShader.Reset();
        resource->domainShader.Reset();
        resource->geometryShader.Reset();
        resource->pixelShader.Reset();
        resource->computeShader.Reset();
        resource->bytecode.clear();
    }
}

void D3D11Renderer::destroy(BindGroupLayoutHandle handle) noexcept {
    if (Impl::BindGroupLayoutResource* resource = getRenderResource(impl_->bindGroupLayouts, handle)) {
        resource->desc = {};
    }
}

void D3D11Renderer::destroy(BindGroupHandle handle) noexcept {
    if (Impl::BindGroupResource* resource = getRenderResource(impl_->bindGroups, handle)) {
        resource->bindings.clear();
        resource->desc = {};
    }
}

void D3D11Renderer::destroy(PipelineLayoutHandle handle) noexcept {
    if (Impl::PipelineLayoutResource* resource = getRenderResource(impl_->pipelineLayouts, handle)) {
        resource->desc = {};
    }
}

void D3D11Renderer::destroy(PipelineCacheHandle handle) noexcept {
    if (Impl::PipelineCacheResource* resource = getRenderResource(impl_->pipelineCaches, handle)) {
        resource->desc = {};
    }
}

void D3D11Renderer::destroy(PipelineHandle handle) noexcept {
    if (Impl::PipelineResource* resource = getRenderResource(impl_->pipelines, handle)) {
        resource->inputLayout.Reset();
        resource->vertexShader.Reset();
        resource->hullShader.Reset();
        resource->domainShader.Reset();
        resource->geometryShader.Reset();
        resource->pixelShader.Reset();
        resource->computeShader.Reset();
        resource->rasterizerState.Reset();
        resource->depthStencilState.Reset();
        resource->blendState.Reset();
    }
}

void D3D11Renderer::destroy(QueryPoolHandle handle) noexcept {
    if (Impl::QueryPoolResource* resource = getRenderResource(impl_->queryPools, handle)) {
        resource->queries.clear();
        resource->desc = {};
    }
}

void D3D11Renderer::destroy(SemaphoreHandle handle) noexcept {
    if (Impl::SemaphoreResource* resource = getRenderResource(impl_->semaphores, handle)) {
        resource->desc = {};
        resource->value = 0;
        resource->signaled = false;
    }
}

void D3D11Renderer::destroy(FenceHandle handle) noexcept {
    if (Impl::FenceResource* resource = getRenderResource(impl_->fences, handle)) {
        resource->eventQuery.Reset();
        resource->desc = {};
        resource->signaled = false;
    }
}

void D3D11Renderer::destroy(SwapchainHandle handle) noexcept {
    if (Impl::SwapchainResource* resource = getRenderResource(impl_->swapchains, handle)) {
        for (TextureViewHandle view : resource->imageViews) {
            destroy(view);
        }
        for (TextureHandle image : resource->images) {
            destroy(image);
        }
        resource->imageViews.clear();
        resource->images.clear();
        resource->swapchain.Reset();
        resource->desc = {};
        resource->format = Format::Undefined;
        resource->extent = {};
    }
}
