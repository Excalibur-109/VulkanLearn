#pragma once

#include "RHID3D11.hpp"

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

// 学习导读：
// 这个文件是统一渲染抽象到 Direct3D 11 的落地层。和 Vulkan 不同，D3D11 是偏“状态机”的
// immediate context API：创建资源和状态对象后，绘制时把 input layout、shader、RTV/DSV、
// constant buffer、SRV、sampler 等绑定到 context，然后调用 Draw/Dispatch。
//
// 因为 D3D11 没有 Vulkan 那种 descriptor set、显式 image layout 和 queue Submit 模型，
// 本后端会把 RHIDefinitions.hpp 的 BindSet/Pipeline/RHIFramePacket 翻译成 D3D11 的
// COM 对象和 context 状态设置，尽量保持上层接口和 Vulkan 后端一致。

// D3D11 资源句柄同样使用 1-based index；0 是无效句柄。真实 COM 对象保存在 Impl 的
// vector 中，公共 API 只传递轻量 RHIHandle。
template <typename HandleT, typename ResourceT>
static HandleT makeRenderHandle(std::vector<ResourceT>& resources, ResourceT&& resource) {
    resources.push_back(std::move(resource));
    return HandleT(static_cast<u64>(resources.size()));
}

// 根据引擎句柄查找后端资源；越界或空句柄返回 nullptr，调用方再决定报错还是忽略。
template <typename ResourceT, typename HandleT>
static ResourceT* getRenderResource(std::vector<ResourceT>& resources, HandleT handle) {
    if (!handle || handle.value == 0 || handle.value > resources.size()) {
        return nullptr;
    }
    return &resources[static_cast<size_t>(handle.value - 1)];
}

// const 版本资源查找。
template <typename ResourceT, typename HandleT>
static const ResourceT* getRenderResource(const std::vector<ResourceT>& resources, HandleT handle) {
    if (!handle || handle.value == 0 || handle.value > resources.size()) {
        return nullptr;
    }
    return &resources[static_cast<size_t>(handle.value - 1)];
}

// DirectX API 通常用 HRESULT 表达错误；这里集中转换成异常，让 Initialize/create* 函数
// 可以统一用 try/catch 填 errorMessage。
static void throwIfFailed(HRESULT hr, const char* message) {
    if (FAILED(hr)) {
        throw std::runtime_error(message);
    }
}

// Windows/D3D 文件 API 常用 UTF-16 路径；引擎层统一用 UTF-8 string，所以需要边界转换。
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

// DXGI adapter 名称是 wchar_t，这里转回 UTF-8 供 RHICapabilities 使用。
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

// 统一 RHIFormat 到 DXGI_FORMAT 的映射层。上层资源描述不直接暴露 DXGI 枚举，便于同一份
// RHIDefinitions.hpp 同时驱动 Vulkan 和 D3D11。
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

// D3D11 深度纹理常用 typeless 资源格式创建，再用 DSV/SRV 选择具体解释方式：
// - DSV 解释成深度/模板格式用于深度测试；
// - SRV 解释成 R 通道格式用于 shader 采样阴影图等数据。
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

// 深度纹理作为 shader resource 读取时不能直接用 D24/D32 这类 DSV 格式，需要改成可采样的
// color-like 格式；普通 color texture 则直接沿用 toDxgiFormat。
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

// 深度模板视图需要 DSV 专用格式，和创建资源用的 typeless 格式不同。
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

// Swapchain 只支持 DXGI 可呈现格式；这里把引擎偏好的格式收敛到 DXGI 能接受的后备缓冲格式。
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

// DXGI swapchain 本体通常必须使用 UNORM，但同一资源可以通过 SRGB RTV 解释。
// 因此公共层看到的是请求的 SRGB 视图格式，而不是被 DXGI 收敛后的物理格式。
static RHIFormat fromSwapchainFormat(
    RHIFormat preferredFormat,
    DXGI_FORMAT physicalFormat) {
    if ((preferredFormat == RHIFormat::RGBA8_SRGB &&
         physicalFormat == DXGI_FORMAT_R8G8B8A8_UNORM) ||
        (preferredFormat == RHIFormat::BGRA8_SRGB &&
         physicalFormat == DXGI_FORMAT_B8G8R8A8_UNORM)) {
        return preferredFormat;
    }
    return fromDxgiFormat(physicalFormat);
}

static UINT toSampleCount(RHISampleCount samples) {
    return static_cast<UINT>(samples);
}

static D3D11_USAGE toD3DUsage(RHIMemoryUsage usage, bool persistentlyMapped) {
    if (persistentlyMapped || usage == RHIMemoryUsage::CpuToGpu) {
        return D3D11_USAGE_DYNAMIC;
    }
    if (usage == RHIMemoryUsage::GpuToCpu || usage == RHIMemoryUsage::CpuOnly) {
        return D3D11_USAGE_STAGING;
    }
    return D3D11_USAGE_DEFAULT;
}

static UINT toCpuAccessFlags(RHIMemoryUsage usage, bool persistentlyMapped) {
    UINT flags = 0;
    if (persistentlyMapped || usage == RHIMemoryUsage::CpuToGpu || usage == RHIMemoryUsage::CpuOnly) {
        flags |= D3D11_CPU_ACCESS_WRITE;
    }
    if (usage == RHIMemoryUsage::GpuToCpu || usage == RHIMemoryUsage::CpuOnly) {
        flags |= D3D11_CPU_ACCESS_READ;
    }
    return flags;
}

static UINT toBufferBindFlags(RHIBufferUsage usage, RHIMemoryUsage memoryUsage) {
    if (memoryUsage == RHIMemoryUsage::GpuToCpu || memoryUsage == RHIMemoryUsage::CpuOnly) {
        return 0;
    }

    UINT flags = 0;
    if (RHIHasAny(usage, RHIBufferUsage::Vertex))  flags |= D3D11_BIND_VERTEX_BUFFER;
    if (RHIHasAny(usage, RHIBufferUsage::Index))   flags |= D3D11_BIND_INDEX_BUFFER;
    if (RHIHasAny(usage, RHIBufferUsage::Uniform)) flags |= D3D11_BIND_CONSTANT_BUFFER;
    if (RHIHasAny(usage, RHIBufferUsage::Storage)) flags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

static UINT toTextureBindFlags(RHITextureUsage usage, RHIMemoryUsage memoryUsage) {
    if (memoryUsage == RHIMemoryUsage::GpuToCpu || memoryUsage == RHIMemoryUsage::CpuOnly) {
        return 0;
    }

    UINT flags = 0;
    if (RHIHasAny(usage, RHITextureUsage::Sampled))                                                  flags |= D3D11_BIND_SHADER_RESOURCE;
    if (RHIHasAny(usage, RHITextureUsage::Storage))                                                  flags |= D3D11_BIND_UNORDERED_ACCESS;
    if (RHIHasAny(usage, RHITextureUsage::ColorAttachment) || RHIHasAny(usage, RHITextureUsage::Present))  flags |= D3D11_BIND_RENDER_TARGET;
    if (RHIHasAny(usage, RHITextureUsage::DepthStencilAttachment))                                   flags |= D3D11_BIND_DEPTH_STENCIL;
    return flags == 0 ? D3D11_BIND_SHADER_RESOURCE : flags;
}

static D3D11_FILTER toD3DFilter(const RHISamplerDesc& desc) {
    if (desc.enableAnisotropy) {
        return desc.enableCompare ? D3D11_FILTER_COMPARISON_ANISOTROPIC : D3D11_FILTER_ANISOTROPIC;
    }

    const bool minLinear = desc.minFilter == RHIFilterMode::Linear;
    const bool magLinear = desc.magFilter == RHIFilterMode::Linear;
    const bool mipLinear = desc.mipmapMode == RHIMipmapMode::Linear;
    if (desc.enableCompare) {
        if (minLinear && magLinear && mipLinear) return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
        if (minLinear && magLinear)              return D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        if (minLinear && mipLinear)              return D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
        if (magLinear && mipLinear)              return D3D11_FILTER_COMPARISON_MIN_POINT_MAG_MIP_LINEAR;
        if (minLinear)                           return D3D11_FILTER_COMPARISON_MIN_LINEAR_MAG_MIP_POINT;
        if (magLinear)                           return D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT;
        if (mipLinear)                           return D3D11_FILTER_COMPARISON_MIN_MAG_POINT_MIP_LINEAR;
        return D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT;
    }

    if (minLinear && magLinear && mipLinear) return D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    if (minLinear && magLinear)              return D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    if (minLinear && mipLinear)              return D3D11_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
    if (magLinear && mipLinear)              return D3D11_FILTER_MIN_POINT_MAG_MIP_LINEAR;
    if (minLinear)                           return D3D11_FILTER_MIN_LINEAR_MAG_MIP_POINT;
    if (magLinear)                           return D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
    if (mipLinear)                           return D3D11_FILTER_MIN_MAG_POINT_MIP_LINEAR;
    return D3D11_FILTER_MIN_MAG_MIP_POINT;
}

static D3D11_TEXTURE_ADDRESS_MODE toD3DAddressMode(RHIAddressMode mode) {
    switch (mode) {
    case RHIAddressMode::Repeat:         return D3D11_TEXTURE_ADDRESS_WRAP;
    case RHIAddressMode::MirroredRepeat: return D3D11_TEXTURE_ADDRESS_MIRROR;
    case RHIAddressMode::ClampToEdge:    return D3D11_TEXTURE_ADDRESS_CLAMP;
    case RHIAddressMode::ClampToBorder:  return D3D11_TEXTURE_ADDRESS_BORDER;
    }
    return D3D11_TEXTURE_ADDRESS_WRAP;
}

static D3D11_COMPARISON_FUNC toD3DCompare(RHICompareOp op) {
    switch (op) {
    case RHICompareOp::Never:          return D3D11_COMPARISON_NEVER;
    case RHICompareOp::Less:           return D3D11_COMPARISON_LESS;
    case RHICompareOp::Equal:          return D3D11_COMPARISON_EQUAL;
    case RHICompareOp::LessOrEqual:    return D3D11_COMPARISON_LESS_EQUAL;
    case RHICompareOp::Greater:        return D3D11_COMPARISON_GREATER;
    case RHICompareOp::NotEqual:       return D3D11_COMPARISON_NOT_EQUAL;
    case RHICompareOp::GreaterOrEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case RHICompareOp::Always:         return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_ALWAYS;
}

static DXGI_FORMAT toD3DVertexFormat(RHIVertexFormat format) {
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

static D3D11_PRIMITIVE_TOPOLOGY toD3DTopology(const RHIInputAssemblyState& state) {
    switch (state.topology) {
    case RHIPrimitiveTopology::PointList:     return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    case RHIPrimitiveTopology::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    case RHIPrimitiveTopology::LineStrip:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case RHIPrimitiveTopology::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case RHIPrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case RHIPrimitiveTopology::PatchList: {
        const u32 points = std::clamp(state.patchControlPoints == 0 ? 3u : state.patchControlPoints, 1u, 32u);
        return static_cast<D3D11_PRIMITIVE_TOPOLOGY>(D3D11_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + points - 1);
    }
    }
    return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

static D3D11_STENCIL_OP toD3DStencilOp(RHIStencilOp op) {
    switch (op) {
    case RHIStencilOp::Keep:           return D3D11_STENCIL_OP_KEEP;
    case RHIStencilOp::Zero:           return D3D11_STENCIL_OP_ZERO;
    case RHIStencilOp::Replace:        return D3D11_STENCIL_OP_REPLACE;
    case RHIStencilOp::IncrementClamp: return D3D11_STENCIL_OP_INCR_SAT;
    case RHIStencilOp::DecrementClamp: return D3D11_STENCIL_OP_DECR_SAT;
    case RHIStencilOp::Invert:         return D3D11_STENCIL_OP_INVERT;
    case RHIStencilOp::IncrementWrap:  return D3D11_STENCIL_OP_INCR;
    case RHIStencilOp::DecrementWrap:  return D3D11_STENCIL_OP_DECR;
    }
    return D3D11_STENCIL_OP_KEEP;
}

static D3D11_BLEND toD3DBlend(RHIBlendFactor factor) {
    switch (factor) {
    case RHIBlendFactor::Zero:                     return D3D11_BLEND_ZERO;
    case RHIBlendFactor::One:                      return D3D11_BLEND_ONE;
    case RHIBlendFactor::SourceColor:              return D3D11_BLEND_SRC_COLOR;
    case RHIBlendFactor::OneMinusSourceColor:      return D3D11_BLEND_INV_SRC_COLOR;
    case RHIBlendFactor::DestinationColor:         return D3D11_BLEND_DEST_COLOR;
    case RHIBlendFactor::OneMinusDestinationColor: return D3D11_BLEND_INV_DEST_COLOR;
    case RHIBlendFactor::SourceAlpha:              return D3D11_BLEND_SRC_ALPHA;
    case RHIBlendFactor::OneMinusSourceAlpha:      return D3D11_BLEND_INV_SRC_ALPHA;
    case RHIBlendFactor::DestinationAlpha:         return D3D11_BLEND_DEST_ALPHA;
    case RHIBlendFactor::OneMinusDestinationAlpha: return D3D11_BLEND_INV_DEST_ALPHA;
    case RHIBlendFactor::ConstantColor:
    case RHIBlendFactor::ConstantAlpha:            return D3D11_BLEND_BLEND_FACTOR;
    case RHIBlendFactor::OneMinusConstantColor:
    case RHIBlendFactor::OneMinusConstantAlpha:    return D3D11_BLEND_INV_BLEND_FACTOR;
    }
    return D3D11_BLEND_ONE;
}

static D3D11_BLEND_OP toD3DBlendOp(RHIBlendOp op) {
    switch (op) {
    case RHIBlendOp::Add:             return D3D11_BLEND_OP_ADD;
    case RHIBlendOp::Subtract:        return D3D11_BLEND_OP_SUBTRACT;
    case RHIBlendOp::ReverseSubtract: return D3D11_BLEND_OP_REV_SUBTRACT;
    case RHIBlendOp::Min:             return D3D11_BLEND_OP_MIN;
    case RHIBlendOp::Max:             return D3D11_BLEND_OP_MAX;
    }
    return D3D11_BLEND_OP_ADD;
}

static UINT8 toD3DColorWriteMask(RHIColorWriteMask mask) {
    UINT8 flags = 0;
    if (RHIHasAny(mask, RHIColorWriteMask::R)) flags |= D3D11_COLOR_WRITE_ENABLE_RED;
    if (RHIHasAny(mask, RHIColorWriteMask::G)) flags |= D3D11_COLOR_WRITE_ENABLE_GREEN;
    if (RHIHasAny(mask, RHIColorWriteMask::B)) flags |= D3D11_COLOR_WRITE_ENABLE_BLUE;
    if (RHIHasAny(mask, RHIColorWriteMask::A)) flags |= D3D11_COLOR_WRITE_ENABLE_ALPHA;
    return flags;
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

// 压缩纹理按 4x4 block 存储，上传时 row pitch 不是 width * pixelBytes，而是 block 数乘
// block 大小。这里封装 pitch 计算，避免调用 UpdateSubresource 时行跨度错误。
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

// RHIShaderDesc 可以不显式写 target profile；D3D11 需要按 stage 编译到 vs/ps/cs 等 profile。
static std::string defaultProfileForStage(RHIShaderStage stage) {
    switch (stage) {
    case RHIShaderStage::Vertex:         return "vs_5_0";
    case RHIShaderStage::TessControl:    return "hs_5_0";
    case RHIShaderStage::TessEvaluation: return "ds_5_0";
    case RHIShaderStage::Geometry:       return "gs_5_0";
    case RHIShaderStage::Fragment:       return "ps_5_0";
    case RHIShaderStage::Compute:        return "cs_5_0";
    default:                          return {};
    }
}

// RHITextureViewDesc 是引擎的统一“看这张纹理的哪一部分”的描述。D3D11 会根据资源维度、
// array/mip 范围、MSAA 情况生成 SRV/RTV/DSV 描述，真正绑定到 shader 或 output merger。
static D3D11_SHADER_RESOURCE_VIEW_DESC makeTextureSrvDesc(const RHITextureDesc& texture, const RHITextureViewDesc& view, RHIFormat viewFormat) {
    D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = toSrvFormat(viewFormat);
    switch (view.dimension) {
    case RHITextureViewDimension::View1D:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1D;
        desc.Texture1D.MostDetailedMip = view.baseMipLevel;
        desc.Texture1D.MipLevels = view.mipLevelCount;
        break;
    case RHITextureViewDimension::View1DArray:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE1DARRAY;
        desc.Texture1DArray.MostDetailedMip = view.baseMipLevel;
        desc.Texture1DArray.MipLevels = view.mipLevelCount;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
        break;
    case RHITextureViewDimension::View2D:
        desc.ViewDimension = texture.samples == RHISampleCount::Count1 ? D3D11_SRV_DIMENSION_TEXTURE2D : D3D11_SRV_DIMENSION_TEXTURE2DMS;
        desc.Texture2D.MostDetailedMip = view.baseMipLevel;
        desc.Texture2D.MipLevels = view.mipLevelCount;
        break;
    case RHITextureViewDimension::View2DArray:
        desc.ViewDimension = texture.samples == RHISampleCount::Count1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY;
        desc.Texture2DArray.MostDetailedMip = view.baseMipLevel;
        desc.Texture2DArray.MipLevels = view.mipLevelCount;
        desc.Texture2DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DArray.ArraySize = view.arrayLayerCount;
        break;
    case RHITextureViewDimension::View3D:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MostDetailedMip = view.baseMipLevel;
        desc.Texture3D.MipLevels = view.mipLevelCount;
        break;
    case RHITextureViewDimension::Cube:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MostDetailedMip = view.baseMipLevel;
        desc.TextureCube.MipLevels = view.mipLevelCount;
        break;
    case RHITextureViewDimension::CubeArray:
        desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBEARRAY;
        desc.TextureCubeArray.MostDetailedMip = view.baseMipLevel;
        desc.TextureCubeArray.MipLevels = view.mipLevelCount;
        desc.TextureCubeArray.First2DArrayFace = view.baseArrayLayer;
        desc.TextureCubeArray.NumCubes = std::max(1u, view.arrayLayerCount / 6u);
        break;
    }
    return desc;
}

static D3D11_RENDER_TARGET_VIEW_DESC makeRtvDesc(const RHITextureDesc& texture, const RHITextureViewDesc& view, RHIFormat viewFormat) {
    D3D11_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = toDxgiFormat(viewFormat);
    if (texture.dimension == RHITextureDimension::Texture3D) {
        desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = view.baseMipLevel;
        desc.Texture3D.FirstWSlice = view.baseArrayLayer;
        desc.Texture3D.WSize = view.arrayLayerCount;
    } else if (texture.dimension == RHITextureDimension::Texture1D) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D11_RTV_DIMENSION_TEXTURE1DARRAY : D3D11_RTV_DIMENSION_TEXTURE1D;
        desc.Texture1DArray.MipSlice = view.baseMipLevel;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
    } else if (texture.samples != RHISampleCount::Count1) {
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

static D3D11_DEPTH_STENCIL_VIEW_DESC makeDsvDesc(const RHITextureDesc& texture, const RHITextureViewDesc& view, RHIFormat viewFormat) {
    D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
    desc.Format = toDsvFormat(viewFormat);
    if (texture.dimension == RHITextureDimension::Texture1D) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D11_DSV_DIMENSION_TEXTURE1DARRAY : D3D11_DSV_DIMENSION_TEXTURE1D;
        desc.Texture1DArray.MipSlice = view.baseMipLevel;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
    } else if (texture.samples != RHISampleCount::Count1) {
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

// Impl 是 RHID3D11 的后端状态仓库。
// D3D11 对象都是 COM 对象，这里用 ComPtr 管生命周期；公共 RHIHandle 只保存 1-based index。
// 注意 BindSetLayout/PipelineLayout 在 D3D11 中没有原生等价物，它们主要作为统一抽象的
// 描述和校验数据存在，真正的绑定发生在 applyBindSet/applyPipeline 里。
struct RHID3D11::Impl {
    struct BufferResource {
        RHIBufferDesc desc{};
        ComPtr<ID3D11Buffer> buffer;
        // D3D11 constant buffer 不允许 UpdateSubresource 使用 D3D11_BOX，也不能只更新
        // 一部分字节。shadow 保留完整原生内容，小范围 RHI upload 先写入 shadow，
        // 再一次性提交整个 constant buffer。
        std::vector<std::byte> uploadShadow;
        RHIResourceState currentState = RHIResourceState::Common;
    };

    struct TextureResource {
        RHITextureDesc desc{};
        ComPtr<ID3D11Resource> resource;
        RHIResourceState currentState = RHIResourceState::Undefined;
        bool swapchainImage = false;
    };

    struct TextureViewResource {
        RHITextureViewDesc desc{};
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11RenderTargetView> rtv;
        ComPtr<ID3D11DepthStencilView> dsv;
        ComPtr<ID3D11UnorderedAccessView> uav;
    };

    struct SamplerResource {
        RHISamplerDesc desc{};
        ComPtr<ID3D11SamplerState> sampler;
    };

    struct ShaderResource {
        RHIShaderDesc desc{};
        std::vector<std::byte> bytecode;
        ComPtr<ID3D11VertexShader> vertexShader;
        ComPtr<ID3D11HullShader> hullShader;
        ComPtr<ID3D11DomainShader> domainShader;
        ComPtr<ID3D11GeometryShader> geometryShader;
        ComPtr<ID3D11PixelShader> pixelShader;
        ComPtr<ID3D11ComputeShader> computeShader;
    };

    struct BindSetLayoutResource {
        RHIBindSetLayoutDesc desc{};
    };

    struct ResolvedBinding {
        u32 slot = 0;
        RHIBindingType type = RHIBindingType::UniformBuffer;
        RHIShaderStage visibility = RHIShaderStage::AllGraphics;
        ComPtr<ID3D11Buffer> buffer;
        ComPtr<ID3D11ShaderResourceView> srv;
        ComPtr<ID3D11UnorderedAccessView> uav;
        ComPtr<ID3D11SamplerState> sampler;
    };

    struct BindSetResource {
        RHIBindSetDesc desc{};
        std::vector<ResolvedBinding> bindings;
    };

    struct PipelineLayoutResource {
        RHIPipelineLayoutDesc desc{};
    };

    struct PipelineCacheResource {
        RHIPipelineCacheDesc desc{};
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
        RHIQueryPoolDesc desc{};
        std::vector<ComPtr<ID3D11Query>> queries;
    };

    struct GPUWaitGPUSignalResource {
        RHIGPUWaitGPUSignalDesc desc{};
        u64 value = 0;
        bool signaled = false;
    };

    struct CPUWaitGPUSignalResource {
        RHICPUWaitGPUSignalDesc desc{};
        ComPtr<ID3D11Query> eventQuery;
        bool signaled = false;
    };

    struct SwapchainResource {
        RHISwapchainDesc desc{};
        ComPtr<IDXGISwapChain> swapchain;
        RHIFormat format = RHIFormat::Undefined;
        RHIExtent2D extent{};
        std::vector<RHITexture> images;
        std::vector<RHITextureView> imageViews;
    };

    RHID3D11NativeHandles native{};
    RHID3D11Desc initDesc{};
    RHICapabilities caps{};

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

    const RHIBindSetLayoutEntry* findLayoutEntry(const BindSetLayoutResource& layout, u32 binding) const {
        const auto it = std::find_if(layout.desc.entries.begin(), layout.desc.entries.end(), [&](const RHIBindSetLayoutEntry& entry) {
            return entry.binding == binding;
        });
        return it == layout.desc.entries.end() ? nullptr : &*it;
    }
};

// WARP/software adapter 一般只作为 fallback，不参与默认硬件选择。
static bool isSoftwareAdapter(const DXGI_ADAPTER_DESC1& desc) {
    return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

// DXGI adapter 选择策略：
// - Default 取第一个非软件 adapter；
// - HighPerformance 倾向显存更大的 adapter；
// - LowPower 倾向显存更小/通常更省电的 adapter。
static ComPtr<IDXGIAdapter1> chooseAdapter(IDXGIFactory1* factory, RHIPowerPreference preference) {
    ComPtr<IDXGIAdapter1> selected;
    SIZE_T selectedMemory = preference == RHIPowerPreference::LowPower ? std::numeric_limits<SIZE_T>::max() : 0;

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
            if (preference == RHIPowerPreference::Default) {
                break;
            }
            continue;
        }

        if (preference == RHIPowerPreference::HighPerformance && desc.DedicatedVideoMemory > selectedMemory) {
            selected = adapter;
            selectedMemory = desc.DedicatedVideoMemory;
        } else if (preference == RHIPowerPreference::LowPower && desc.DedicatedVideoMemory < selectedMemory) {
            selected = adapter;
            selectedMemory = desc.DedicatedVideoMemory;
        }
    }

    return selected;
}

// D3D11 的 feature set 相对固定，但统一 RHIRenderFeature 里包含 Vulkan/D3D12 风格能力。
// 这里既检查 caps 是否支持，也明确排除本后端没有实现或 API 本身不适合表达的能力。
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

// 把 adapter/feature level 整理成引擎统一的 RHICapabilities。
// 这些能力会被上层用于选择渲染路径，也会被 requiredFeatures 校验。
static RHICapabilities makeCapabilities(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL featureLevel) {
    RHICapabilities caps{};
    caps.api = RHIGraphicsAPI::Direct3D11;

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
    caps.maxBindSets = 1;
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

// D3D11 private 片段放所有“后端内部语言”：
// - 引擎句柄到 vector 槽位的 1-based handle 管理；
// - RHIFormat/RHIVertexFormat/Sampler/Blend/Stencil 等跨 API enum 到 DXGI/D3D11 enum 的转换；
// - Impl 中保存的 COM 资源结构；
// - DXGI adapter 选择和 RHICapabilities 生成。
// 读这里时重点看“RHIDefinitions.hpp 的抽象字段，最终落到哪个 D3D11 原生类型”。

} // namespace rhi










