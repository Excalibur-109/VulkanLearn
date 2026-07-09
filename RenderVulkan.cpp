#include "RenderVulkan.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

/// Vulkan 资源句柄使用 1-based index；0 保持为无效句柄。
template <typename HandleT, typename ResourceT>
static HandleT makeRenderHandle(std::vector<ResourceT>& resources, ResourceT&& resource) {
    resources.push_back(std::move(resource));
    return HandleT(static_cast<u64>(resources.size()));
}

/// 根据引擎句柄查找资源；无效句柄或越界时返回 nullptr。
template <typename ResourceT, typename HandleT>
static ResourceT* getRenderResource(std::vector<ResourceT>& resources, HandleT handle) {
    if (!handle || handle.value == 0 || handle.value > resources.size()) {
        return nullptr;
    }
    return &resources[static_cast<size_t>(handle.value - 1)];
}

/// const 版本资源查找。
template <typename ResourceT, typename HandleT>
static const ResourceT* getRenderResource(const std::vector<ResourceT>& resources, HandleT handle) {
    if (!handle || handle.value == 0 || handle.value > resources.size()) {
        return nullptr;
    }
    return &resources[static_cast<size_t>(handle.value - 1)];
}

/// Vulkan 调试回调只输出到标准错误流；引擎层可以之后替换成日志系统。
static VKAPI_ATTR VkBool32 VKAPI_CALL vulkanDebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*) {
    if (callbackData != nullptr && callbackData->pMessage != nullptr) {
        fprintf(stderr, "Vulkan validation: %s\n", callbackData->pMessage);
    }
    return VK_FALSE;
}

/// 读取 shader bytecode 文件；Vulkan shader module 期望 SPIR-V 字节码。
static std::vector<std::byte> readBinaryFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("无法打开 shader 文件: " + path);
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }

    std::vector<std::byte> data(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

static VkFormat toVkFormat(Format format) {
    switch (format) {
    case Format::Undefined:         return VK_FORMAT_UNDEFINED;
    case Format::R8_UNorm:          return VK_FORMAT_R8_UNORM;
    case Format::R8_SNorm:          return VK_FORMAT_R8_SNORM;
    case Format::R8_UInt:           return VK_FORMAT_R8_UINT;
    case Format::R8_SInt:           return VK_FORMAT_R8_SINT;
    case Format::RG8_UNorm:         return VK_FORMAT_R8G8_UNORM;
    case Format::RG8_SNorm:         return VK_FORMAT_R8G8_SNORM;
    case Format::RG8_UInt:          return VK_FORMAT_R8G8_UINT;
    case Format::RG8_SInt:          return VK_FORMAT_R8G8_SINT;
    case Format::RGBA8_UNorm:       return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_SNorm:       return VK_FORMAT_R8G8B8A8_SNORM;
    case Format::RGBA8_UInt:        return VK_FORMAT_R8G8B8A8_UINT;
    case Format::RGBA8_SInt:        return VK_FORMAT_R8G8B8A8_SINT;
    case Format::RGBA8_SRGB:        return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8_UNorm:       return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8_SRGB:        return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::R16_UNorm:         return VK_FORMAT_R16_UNORM;
    case Format::R16_SNorm:         return VK_FORMAT_R16_SNORM;
    case Format::R16_UInt:          return VK_FORMAT_R16_UINT;
    case Format::R16_SInt:          return VK_FORMAT_R16_SINT;
    case Format::R16_Float:         return VK_FORMAT_R16_SFLOAT;
    case Format::RG16_UNorm:        return VK_FORMAT_R16G16_UNORM;
    case Format::RG16_SNorm:        return VK_FORMAT_R16G16_SNORM;
    case Format::RG16_UInt:         return VK_FORMAT_R16G16_UINT;
    case Format::RG16_SInt:         return VK_FORMAT_R16G16_SINT;
    case Format::RG16_Float:        return VK_FORMAT_R16G16_SFLOAT;
    case Format::RGBA16_UNorm:      return VK_FORMAT_R16G16B16A16_UNORM;
    case Format::RGBA16_SNorm:      return VK_FORMAT_R16G16B16A16_SNORM;
    case Format::RGBA16_UInt:       return VK_FORMAT_R16G16B16A16_UINT;
    case Format::RGBA16_SInt:       return VK_FORMAT_R16G16B16A16_SINT;
    case Format::RGBA16_Float:      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::R32_UInt:          return VK_FORMAT_R32_UINT;
    case Format::R32_SInt:          return VK_FORMAT_R32_SINT;
    case Format::R32_Float:         return VK_FORMAT_R32_SFLOAT;
    case Format::RG32_UInt:         return VK_FORMAT_R32G32_UINT;
    case Format::RG32_SInt:         return VK_FORMAT_R32G32_SINT;
    case Format::RG32_Float:        return VK_FORMAT_R32G32_SFLOAT;
    case Format::RGB32_UInt:        return VK_FORMAT_R32G32B32_UINT;
    case Format::RGB32_SInt:        return VK_FORMAT_R32G32B32_SINT;
    case Format::RGB32_Float:       return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::RGBA32_UInt:       return VK_FORMAT_R32G32B32A32_UINT;
    case Format::RGBA32_SInt:       return VK_FORMAT_R32G32B32A32_SINT;
    case Format::RGBA32_Float:      return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::RGB10A2_UNorm:     return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    case Format::R11G11B10_Float:   return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
    case Format::D16_UNorm:         return VK_FORMAT_D16_UNORM;
    case Format::D24_UNorm:         return VK_FORMAT_X8_D24_UNORM_PACK32;
    case Format::S8_UInt:           return VK_FORMAT_S8_UINT;
    case Format::D24_UNorm_S8_UInt: return VK_FORMAT_D24_UNORM_S8_UINT;
    case Format::D32_Float:         return VK_FORMAT_D32_SFLOAT;
    case Format::D32_Float_S8_UInt: return VK_FORMAT_D32_SFLOAT_S8_UINT;
    case Format::BC1RGBA_UNorm:     return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC1RGBA_SRGB:      return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case Format::BC3RGBA_UNorm:     return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC3RGBA_SRGB:      return VK_FORMAT_BC3_SRGB_BLOCK;
    case Format::BC5RG_UNorm:       return VK_FORMAT_BC5_UNORM_BLOCK;
    case Format::BC5RG_SNorm:       return VK_FORMAT_BC5_SNORM_BLOCK;
    case Format::BC7RGBA_UNorm:     return VK_FORMAT_BC7_UNORM_BLOCK;
    case Format::BC7RGBA_SRGB:      return VK_FORMAT_BC7_SRGB_BLOCK;
    case Format::ETC2RGB8_UNorm:    return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
    case Format::ETC2RGB8_SRGB:     return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
    case Format::ETC2RGBA8_UNorm:   return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
    case Format::ETC2RGBA8_SRGB:    return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
    case Format::ASTC4x4_UNorm:     return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    case Format::ASTC4x4_SRGB:      return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
    case Format::ASTC8x8_UNorm:     return VK_FORMAT_ASTC_8x8_UNORM_BLOCK;
    case Format::ASTC8x8_SRGB:      return VK_FORMAT_ASTC_8x8_SRGB_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

/// 把 swapchain 实际选到的 Vulkan format 转回通用 Format，保证上层拿到的 texture 描述与真实后备缓冲一致。
static Format fromVkFormat(VkFormat format) {
    switch (format) {
    case VK_FORMAT_R8_UNORM:                  return Format::R8_UNorm;
    case VK_FORMAT_R8_SNORM:                  return Format::R8_SNorm;
    case VK_FORMAT_R8_UINT:                   return Format::R8_UInt;
    case VK_FORMAT_R8_SINT:                   return Format::R8_SInt;
    case VK_FORMAT_R8G8_UNORM:                return Format::RG8_UNorm;
    case VK_FORMAT_R8G8_SNORM:                return Format::RG8_SNorm;
    case VK_FORMAT_R8G8_UINT:                 return Format::RG8_UInt;
    case VK_FORMAT_R8G8_SINT:                 return Format::RG8_SInt;
    case VK_FORMAT_R8G8B8A8_UNORM:            return Format::RGBA8_UNorm;
    case VK_FORMAT_R8G8B8A8_SNORM:            return Format::RGBA8_SNorm;
    case VK_FORMAT_R8G8B8A8_UINT:             return Format::RGBA8_UInt;
    case VK_FORMAT_R8G8B8A8_SINT:             return Format::RGBA8_SInt;
    case VK_FORMAT_R8G8B8A8_SRGB:             return Format::RGBA8_SRGB;
    case VK_FORMAT_B8G8R8A8_UNORM:            return Format::BGRA8_UNorm;
    case VK_FORMAT_B8G8R8A8_SRGB:             return Format::BGRA8_SRGB;
    case VK_FORMAT_R16_UNORM:                 return Format::R16_UNorm;
    case VK_FORMAT_R16_SNORM:                 return Format::R16_SNorm;
    case VK_FORMAT_R16_UINT:                  return Format::R16_UInt;
    case VK_FORMAT_R16_SINT:                  return Format::R16_SInt;
    case VK_FORMAT_R16_SFLOAT:                return Format::R16_Float;
    case VK_FORMAT_R16G16_UNORM:              return Format::RG16_UNorm;
    case VK_FORMAT_R16G16_SNORM:              return Format::RG16_SNorm;
    case VK_FORMAT_R16G16_UINT:               return Format::RG16_UInt;
    case VK_FORMAT_R16G16_SINT:               return Format::RG16_SInt;
    case VK_FORMAT_R16G16_SFLOAT:             return Format::RG16_Float;
    case VK_FORMAT_R16G16B16A16_UNORM:        return Format::RGBA16_UNorm;
    case VK_FORMAT_R16G16B16A16_SNORM:        return Format::RGBA16_SNorm;
    case VK_FORMAT_R16G16B16A16_UINT:         return Format::RGBA16_UInt;
    case VK_FORMAT_R16G16B16A16_SINT:         return Format::RGBA16_SInt;
    case VK_FORMAT_R16G16B16A16_SFLOAT:       return Format::RGBA16_Float;
    case VK_FORMAT_R32_UINT:                  return Format::R32_UInt;
    case VK_FORMAT_R32_SINT:                  return Format::R32_SInt;
    case VK_FORMAT_R32_SFLOAT:                return Format::R32_Float;
    case VK_FORMAT_R32G32_UINT:               return Format::RG32_UInt;
    case VK_FORMAT_R32G32_SINT:               return Format::RG32_SInt;
    case VK_FORMAT_R32G32_SFLOAT:             return Format::RG32_Float;
    case VK_FORMAT_R32G32B32_UINT:            return Format::RGB32_UInt;
    case VK_FORMAT_R32G32B32_SINT:            return Format::RGB32_SInt;
    case VK_FORMAT_R32G32B32_SFLOAT:          return Format::RGB32_Float;
    case VK_FORMAT_R32G32B32A32_UINT:         return Format::RGBA32_UInt;
    case VK_FORMAT_R32G32B32A32_SINT:         return Format::RGBA32_SInt;
    case VK_FORMAT_R32G32B32A32_SFLOAT:       return Format::RGBA32_Float;
    case VK_FORMAT_A2B10G10R10_UNORM_PACK32:  return Format::RGB10A2_UNorm;
    case VK_FORMAT_B10G11R11_UFLOAT_PACK32:   return Format::R11G11B10_Float;
    case VK_FORMAT_D16_UNORM:                 return Format::D16_UNorm;
    case VK_FORMAT_X8_D24_UNORM_PACK32:       return Format::D24_UNorm;
    case VK_FORMAT_S8_UINT:                   return Format::S8_UInt;
    case VK_FORMAT_D24_UNORM_S8_UINT:         return Format::D24_UNorm_S8_UInt;
    case VK_FORMAT_D32_SFLOAT:                return Format::D32_Float;
    case VK_FORMAT_D32_SFLOAT_S8_UINT:        return Format::D32_Float_S8_UInt;
    case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:      return Format::BC1RGBA_UNorm;
    case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:       return Format::BC1RGBA_SRGB;
    case VK_FORMAT_BC3_UNORM_BLOCK:           return Format::BC3RGBA_UNorm;
    case VK_FORMAT_BC3_SRGB_BLOCK:            return Format::BC3RGBA_SRGB;
    case VK_FORMAT_BC5_UNORM_BLOCK:           return Format::BC5RG_UNorm;
    case VK_FORMAT_BC5_SNORM_BLOCK:           return Format::BC5RG_SNorm;
    case VK_FORMAT_BC7_UNORM_BLOCK:           return Format::BC7RGBA_UNorm;
    case VK_FORMAT_BC7_SRGB_BLOCK:            return Format::BC7RGBA_SRGB;
    case VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK:   return Format::ETC2RGB8_UNorm;
    case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:    return Format::ETC2RGB8_SRGB;
    case VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK: return Format::ETC2RGBA8_UNorm;
    case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:  return Format::ETC2RGBA8_SRGB;
    case VK_FORMAT_ASTC_4x4_UNORM_BLOCK:      return Format::ASTC4x4_UNorm;
    case VK_FORMAT_ASTC_4x4_SRGB_BLOCK:       return Format::ASTC4x4_SRGB;
    case VK_FORMAT_ASTC_8x8_UNORM_BLOCK:      return Format::ASTC8x8_UNorm;
    case VK_FORMAT_ASTC_8x8_SRGB_BLOCK:       return Format::ASTC8x8_SRGB;
    default:                                  return Format::Undefined;
    }
}

static VkSampleCountFlagBits toVkSampleCount(SampleCount samples) {
    switch (samples) {
    case SampleCount::Count1:  return VK_SAMPLE_COUNT_1_BIT;
    case SampleCount::Count2:  return VK_SAMPLE_COUNT_2_BIT;
    case SampleCount::Count4:  return VK_SAMPLE_COUNT_4_BIT;
    case SampleCount::Count8:  return VK_SAMPLE_COUNT_8_BIT;
    case SampleCount::Count16: return VK_SAMPLE_COUNT_16_BIT;
    case SampleCount::Count32: return VK_SAMPLE_COUNT_32_BIT;
    case SampleCount::Count64: return VK_SAMPLE_COUNT_64_BIT;
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

static VkBufferUsageFlags toVkBufferUsage(BufferUsage usage) {
    VkBufferUsageFlags flags = 0;
    if (hasAny(usage, BufferUsage::TransferSource)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (hasAny(usage, BufferUsage::TransferDestination)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (hasAny(usage, BufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::Storage)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::Indirect)) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::ShaderDeviceAddress)) flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    return flags == 0 ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : flags;
}

static VkImageUsageFlags toVkImageUsage(TextureUsage usage) {
    VkImageUsageFlags flags = 0;
    if (hasAny(usage, TextureUsage::TransferSource)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (hasAny(usage, TextureUsage::TransferDestination)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasAny(usage, TextureUsage::Sampled)) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (hasAny(usage, TextureUsage::Storage)) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (hasAny(usage, TextureUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (hasAny(usage, TextureUsage::DepthStencilAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (hasAny(usage, TextureUsage::Transient)) flags |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    if (hasAny(usage, TextureUsage::Present)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    return flags == 0 ? VK_IMAGE_USAGE_SAMPLED_BIT : flags;
}

static VkImageCreateFlags toVkImageCreateFlags(TextureCreateFlags flags) {
    VkImageCreateFlags vkFlags = 0;
    if (hasAny(flags, TextureCreateFlags::CubeCompatible)) vkFlags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    if (hasAny(flags, TextureCreateFlags::MutableFormat)) vkFlags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    if (hasAny(flags, TextureCreateFlags::SparseBinding)) vkFlags |= VK_IMAGE_CREATE_SPARSE_BINDING_BIT;
    return vkFlags;
}

static VkMemoryPropertyFlags toVkMemoryProperties(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::GpuOnly:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    case MemoryUsage::CpuToGpu:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case MemoryUsage::GpuToCpu:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    case MemoryUsage::CpuOnly:
        return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}

static VkImageType toVkImageType(TextureDimension dimension) {
    switch (dimension) {
    case TextureDimension::Texture1D: return VK_IMAGE_TYPE_1D;
    case TextureDimension::Texture2D: return VK_IMAGE_TYPE_2D;
    case TextureDimension::Texture3D: return VK_IMAGE_TYPE_3D;
    }
    return VK_IMAGE_TYPE_2D;
}

static VkImageViewType toVkImageViewType(TextureViewDimension dimension) {
    switch (dimension) {
    case TextureViewDimension::View1D:      return VK_IMAGE_VIEW_TYPE_1D;
    case TextureViewDimension::View1DArray: return VK_IMAGE_VIEW_TYPE_1D_ARRAY;
    case TextureViewDimension::View2D:      return VK_IMAGE_VIEW_TYPE_2D;
    case TextureViewDimension::View2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureViewDimension::View3D:      return VK_IMAGE_VIEW_TYPE_3D;
    case TextureViewDimension::Cube:        return VK_IMAGE_VIEW_TYPE_CUBE;
    case TextureViewDimension::CubeArray:   return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}

static VkImageAspectFlags toVkImageAspect(TextureAspect aspect, Format format) {
    if (aspect == TextureAspect::All || (aspect == TextureAspect::Color && isDepthFormat(format))) {
        VkImageAspectFlags inferred = 0;
        if (isDepthFormat(format)) inferred |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilFormat(format)) inferred |= VK_IMAGE_ASPECT_STENCIL_BIT;
        return inferred == 0 ? VK_IMAGE_ASPECT_COLOR_BIT : inferred;
    }

    VkImageAspectFlags flags = 0;
    if (hasAny(aspect, TextureAspect::Color)) flags |= VK_IMAGE_ASPECT_COLOR_BIT;
    if (hasAny(aspect, TextureAspect::Depth)) flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (hasAny(aspect, TextureAspect::Stencil)) flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if (hasAny(aspect, TextureAspect::Plane0)) flags |= VK_IMAGE_ASPECT_PLANE_0_BIT;
    if (hasAny(aspect, TextureAspect::Plane1)) flags |= VK_IMAGE_ASPECT_PLANE_1_BIT;
    if (hasAny(aspect, TextureAspect::Plane2)) flags |= VK_IMAGE_ASPECT_PLANE_2_BIT;
    return flags == 0 ? VK_IMAGE_ASPECT_COLOR_BIT : flags;
}

static VkFilter toVkFilter(FilterMode filter) {
    return filter == FilterMode::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

static VkSamplerMipmapMode toVkMipmapMode(MipmapMode mode) {
    return mode == MipmapMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

static VkSamplerAddressMode toVkAddressMode(AddressMode mode) {
    switch (mode) {
    case AddressMode::Repeat:         return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case AddressMode::MirroredRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case AddressMode::ClampToEdge:    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case AddressMode::ClampToBorder:  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_REPEAT;
}

static VkBorderColor toVkBorderColor(BorderColor color) {
    switch (color) {
    case BorderColor::TransparentBlack: return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case BorderColor::OpaqueBlack:      return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    case BorderColor::OpaqueWhite:      return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    }
    return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
}

static VkCompareOp toVkCompareOp(CompareOp op) {
    switch (op) {
    case CompareOp::Never:          return VK_COMPARE_OP_NEVER;
    case CompareOp::Less:           return VK_COMPARE_OP_LESS;
    case CompareOp::Equal:          return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessOrEqual:    return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater:        return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual:       return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always:         return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_ALWAYS;
}

static VkShaderStageFlags toVkShaderStages(ShaderStage stages) {
    VkShaderStageFlags flags = 0;
    if (hasAny(stages, ShaderStage::Vertex)) flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (hasAny(stages, ShaderStage::TessControl)) flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (hasAny(stages, ShaderStage::TessEvaluation)) flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if (hasAny(stages, ShaderStage::Geometry)) flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (hasAny(stages, ShaderStage::Fragment)) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (hasAny(stages, ShaderStage::Compute)) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (hasAny(stages, ShaderStage::Task)) flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
    if (hasAny(stages, ShaderStage::Mesh)) flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
    return flags;
}

static VkShaderStageFlagBits toVkSingleShaderStage(ShaderStage stage) {
    const VkShaderStageFlags flags = toVkShaderStages(stage);
    if ((flags & (flags - 1)) != 0 || flags == 0) {
        throw std::runtime_error("ShaderDesc::stage 必须是单个 shader stage");
    }
    return static_cast<VkShaderStageFlagBits>(flags);
}

static VkDescriptorType toVkDescriptorType(BindingType type) {
    switch (type) {
    case BindingType::UniformBuffer:          return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::StorageBuffer:          return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case BindingType::SampledTexture:         return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    case BindingType::StorageTexture:         return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    case BindingType::Sampler:                return VK_DESCRIPTOR_TYPE_SAMPLER;
    case BindingType::CombinedTextureSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case BindingType::PushConstant:           return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case BindingType::AccelerationStructure:  return VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

static VkFormat toVkVertexFormat(VertexFormat format) {
    switch (format) {
    case VertexFormat::Float32:   return VK_FORMAT_R32_SFLOAT;
    case VertexFormat::Float32x2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexFormat::Float32x3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexFormat::Float32x4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexFormat::UInt32:    return VK_FORMAT_R32_UINT;
    case VertexFormat::UInt32x2:  return VK_FORMAT_R32G32_UINT;
    case VertexFormat::UInt32x3:  return VK_FORMAT_R32G32B32_UINT;
    case VertexFormat::UInt32x4:  return VK_FORMAT_R32G32B32A32_UINT;
    case VertexFormat::SInt32:    return VK_FORMAT_R32_SINT;
    case VertexFormat::SInt32x2:  return VK_FORMAT_R32G32_SINT;
    case VertexFormat::SInt32x3:  return VK_FORMAT_R32G32B32_SINT;
    case VertexFormat::SInt32x4:  return VK_FORMAT_R32G32B32A32_SINT;
    case VertexFormat::UNorm8x4:  return VK_FORMAT_R8G8B8A8_UNORM;
    case VertexFormat::SNorm8x4:  return VK_FORMAT_R8G8B8A8_SNORM;
    case VertexFormat::UInt16x2:  return VK_FORMAT_R16G16_UINT;
    case VertexFormat::UInt16x4:  return VK_FORMAT_R16G16B16A16_UINT;
    case VertexFormat::SInt16x2:  return VK_FORMAT_R16G16_SINT;
    case VertexFormat::SInt16x4:  return VK_FORMAT_R16G16B16A16_SINT;
    case VertexFormat::UNorm16x2: return VK_FORMAT_R16G16_UNORM;
    case VertexFormat::UNorm16x4: return VK_FORMAT_R16G16B16A16_UNORM;
    case VertexFormat::SNorm16x2: return VK_FORMAT_R16G16_SNORM;
    case VertexFormat::SNorm16x4: return VK_FORMAT_R16G16B16A16_SNORM;
    }
    return VK_FORMAT_UNDEFINED;
}

static VkPrimitiveTopology toVkPrimitiveTopology(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::PatchList:     return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

static VkPolygonMode toVkPolygonMode(PolygonMode mode) {
    switch (mode) {
    case PolygonMode::Fill:  return VK_POLYGON_MODE_FILL;
    case PolygonMode::Line:  return VK_POLYGON_MODE_LINE;
    case PolygonMode::Point: return VK_POLYGON_MODE_POINT;
    }
    return VK_POLYGON_MODE_FILL;
}

static VkCullModeFlags toVkCullMode(CullMode mode) {
    switch (mode) {
    case CullMode::None:         return VK_CULL_MODE_NONE;
    case CullMode::Front:        return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back:         return VK_CULL_MODE_BACK_BIT;
    case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
    }
    return VK_CULL_MODE_BACK_BIT;
}

static VkFrontFace toVkFrontFace(FrontFace face) {
    return face == FrontFace::Clockwise ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
}

static VkStencilOp toVkStencilOp(StencilOp op) {
    switch (op) {
    case StencilOp::Keep:           return VK_STENCIL_OP_KEEP;
    case StencilOp::Zero:           return VK_STENCIL_OP_ZERO;
    case StencilOp::Replace:        return VK_STENCIL_OP_REPLACE;
    case StencilOp::IncrementClamp: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
    case StencilOp::DecrementClamp: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
    case StencilOp::Invert:         return VK_STENCIL_OP_INVERT;
    case StencilOp::IncrementWrap:  return VK_STENCIL_OP_INCREMENT_AND_WRAP;
    case StencilOp::DecrementWrap:  return VK_STENCIL_OP_DECREMENT_AND_WRAP;
    }
    return VK_STENCIL_OP_KEEP;
}

static VkBlendFactor toVkBlendFactor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:                     return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One:                      return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SourceColor:              return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::OneMinusSourceColor:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
    case BlendFactor::DestinationColor:         return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::OneMinusDestinationColor: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
    case BlendFactor::SourceAlpha:              return VK_BLEND_FACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha:      return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DestinationAlpha:         return VK_BLEND_FACTOR_DST_ALPHA;
    case BlendFactor::OneMinusDestinationAlpha: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
    case BlendFactor::ConstantColor:            return VK_BLEND_FACTOR_CONSTANT_COLOR;
    case BlendFactor::OneMinusConstantColor:    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
    case BlendFactor::ConstantAlpha:            return VK_BLEND_FACTOR_CONSTANT_ALPHA;
    case BlendFactor::OneMinusConstantAlpha:    return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
    }
    return VK_BLEND_FACTOR_ONE;
}

static VkBlendOp toVkBlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add:             return VK_BLEND_OP_ADD;
    case BlendOp::Subtract:        return VK_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min:             return VK_BLEND_OP_MIN;
    case BlendOp::Max:             return VK_BLEND_OP_MAX;
    }
    return VK_BLEND_OP_ADD;
}

static VkColorComponentFlags toVkColorWriteMask(ColorWriteMask mask) {
    VkColorComponentFlags flags = 0;
    if (hasAny(mask, ColorWriteMask::R)) flags |= VK_COLOR_COMPONENT_R_BIT;
    if (hasAny(mask, ColorWriteMask::G)) flags |= VK_COLOR_COMPONENT_G_BIT;
    if (hasAny(mask, ColorWriteMask::B)) flags |= VK_COLOR_COMPONENT_B_BIT;
    if (hasAny(mask, ColorWriteMask::A)) flags |= VK_COLOR_COMPONENT_A_BIT;
    return flags;
}

static VkDynamicState toVkDynamicState(DynamicState state) {
    switch (state) {
    case DynamicState::Viewport:         return VK_DYNAMIC_STATE_VIEWPORT;
    case DynamicState::Scissor:          return VK_DYNAMIC_STATE_SCISSOR;
    case DynamicState::LineWidth:        return VK_DYNAMIC_STATE_LINE_WIDTH;
    case DynamicState::DepthBias:        return VK_DYNAMIC_STATE_DEPTH_BIAS;
    case DynamicState::BlendConstants:   return VK_DYNAMIC_STATE_BLEND_CONSTANTS;
    case DynamicState::StencilReference: return VK_DYNAMIC_STATE_STENCIL_REFERENCE;
    }
    return VK_DYNAMIC_STATE_VIEWPORT;
}

static VkAttachmentLoadOp toVkLoadOp(LoadOp loadOp) {
    switch (loadOp) {
    case LoadOp::Load:     return VK_ATTACHMENT_LOAD_OP_LOAD;
    case LoadOp::Clear:    return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
}

static VkAttachmentStoreOp toVkStoreOp(StoreOp storeOp) {
    switch (storeOp) {
    case StoreOp::Store:    return VK_ATTACHMENT_STORE_OP_STORE;
    case StoreOp::DontCare: return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    }
    return VK_ATTACHMENT_STORE_OP_DONT_CARE;
}

static VkPresentModeKHR toVkPresentMode(PresentMode mode) {
    switch (mode) {
    case PresentMode::Immediate:   return VK_PRESENT_MODE_IMMEDIATE_KHR;
    case PresentMode::Mailbox:     return VK_PRESENT_MODE_MAILBOX_KHR;
    case PresentMode::FIFO:        return VK_PRESENT_MODE_FIFO_KHR;
    case PresentMode::FIFORelaxed: return VK_PRESENT_MODE_FIFO_RELAXED_KHR;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkColorSpaceKHR toVkColorSpace(ColorSpace colorSpace) {
    switch (colorSpace) {
    case ColorSpace::SRGBNonlinear:      return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    case ColorSpace::DisplayP3Nonlinear: return VK_COLOR_SPACE_DISPLAY_P3_NONLINEAR_EXT;
    case ColorSpace::ExtendedSRGBLinear: return VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
    case ColorSpace::HDR10ST2084:        return VK_COLOR_SPACE_HDR10_ST2084_EXT;
    case ColorSpace::HDR10HLG:           return VK_COLOR_SPACE_HDR10_HLG_EXT;
    }
    return VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
}

static VkSurfaceTransformFlagBitsKHR toVkSurfaceTransform(SurfaceTransform transform) {
    switch (transform) {
    case SurfaceTransform::Identity:                  return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    case SurfaceTransform::Rotate90:                  return VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR;
    case SurfaceTransform::Rotate180:                 return VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR;
    case SurfaceTransform::Rotate270:                 return VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    case SurfaceTransform::HorizontalMirror:          return VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR;
    case SurfaceTransform::HorizontalMirrorRotate90:  return VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR;
    case SurfaceTransform::HorizontalMirrorRotate180: return VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR;
    case SurfaceTransform::HorizontalMirrorRotate270: return VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR;
    case SurfaceTransform::Inherit:                   return VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR;
    }
    return VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
}

static VkCompositeAlphaFlagBitsKHR toVkCompositeAlpha(CompositeAlphaMode mode) {
    switch (mode) {
    case CompositeAlphaMode::Opaque:         return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    case CompositeAlphaMode::PreMultiplied:  return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    case CompositeAlphaMode::PostMultiplied: return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    case CompositeAlphaMode::Inherit:        return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
    }
    return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
}

static VkPipelineStageFlags toVkPipelineStages(PipelineStage stages) {
    VkPipelineStageFlags flags = 0;
    if (hasAny(stages, PipelineStage::TopOfPipe)) flags |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (hasAny(stages, PipelineStage::DrawIndirect)) flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    if (hasAny(stages, PipelineStage::VertexInput)) flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (hasAny(stages, PipelineStage::VertexShader)) flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (hasAny(stages, PipelineStage::TessControlShader)) flags |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
    if (hasAny(stages, PipelineStage::TessEvaluationShader)) flags |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    if (hasAny(stages, PipelineStage::GeometryShader)) flags |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    if (hasAny(stages, PipelineStage::FragmentShader)) flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (hasAny(stages, PipelineStage::EarlyFragmentTests)) flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    if (hasAny(stages, PipelineStage::LateFragmentTests)) flags |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    if (hasAny(stages, PipelineStage::ColorAttachmentOutput)) flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (hasAny(stages, PipelineStage::ComputeShader)) flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (hasAny(stages, PipelineStage::Transfer)) flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (hasAny(stages, PipelineStage::BottomOfPipe)) flags |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (hasAny(stages, PipelineStage::Host)) flags |= VK_PIPELINE_STAGE_HOST_BIT;
    if (hasAny(stages, PipelineStage::AllGraphics)) flags |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    if (hasAny(stages, PipelineStage::AllCommands)) flags |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    return flags == 0 ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : flags;
}

[[maybe_unused]] static VkAccessFlags toVkAccessFlags(AccessFlags access) {
    VkAccessFlags flags = 0;
    if (hasAny(access, AccessFlags::IndirectCommandRead)) flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    if (hasAny(access, AccessFlags::IndexRead)) flags |= VK_ACCESS_INDEX_READ_BIT;
    if (hasAny(access, AccessFlags::VertexAttributeRead)) flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (hasAny(access, AccessFlags::UniformRead)) flags |= VK_ACCESS_UNIFORM_READ_BIT;
    if (hasAny(access, AccessFlags::InputAttachmentRead)) flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    if (hasAny(access, AccessFlags::ShaderRead)) flags |= VK_ACCESS_SHADER_READ_BIT;
    if (hasAny(access, AccessFlags::ShaderWrite)) flags |= VK_ACCESS_SHADER_WRITE_BIT;
    if (hasAny(access, AccessFlags::ColorAttachmentRead)) flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    if (hasAny(access, AccessFlags::ColorAttachmentWrite)) flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (hasAny(access, AccessFlags::DepthStencilRead)) flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (hasAny(access, AccessFlags::DepthStencilWrite)) flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (hasAny(access, AccessFlags::TransferRead)) flags |= VK_ACCESS_TRANSFER_READ_BIT;
    if (hasAny(access, AccessFlags::TransferWrite)) flags |= VK_ACCESS_TRANSFER_WRITE_BIT;
    if (hasAny(access, AccessFlags::HostRead)) flags |= VK_ACCESS_HOST_READ_BIT;
    if (hasAny(access, AccessFlags::HostWrite)) flags |= VK_ACCESS_HOST_WRITE_BIT;
    if (hasAny(access, AccessFlags::MemoryRead)) flags |= VK_ACCESS_MEMORY_READ_BIT;
    if (hasAny(access, AccessFlags::MemoryWrite)) flags |= VK_ACCESS_MEMORY_WRITE_BIT;
    return flags;
}

[[maybe_unused]] static VkImageLayout toVkImageLayout(ResourceState state) {
    switch (state) {
    case ResourceState::Undefined:          return VK_IMAGE_LAYOUT_UNDEFINED;
    case ResourceState::CopySource:         return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::CopyDestination:    return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ResourceState::ShaderRead:         return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case ResourceState::ShaderWrite:        return VK_IMAGE_LAYOUT_GENERAL;
    case ResourceState::RenderTarget:       return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case ResourceState::DepthRead:          return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    case ResourceState::DepthWrite:         return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    case ResourceState::ResolveSource:      return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case ResourceState::ResolveDestination: return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case ResourceState::Present:            return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    default:                                return VK_IMAGE_LAYOUT_GENERAL;
    }
}

[[maybe_unused]] static VkAccessFlags accessFromResourceState(ResourceState state) {
    switch (state) {
    case ResourceState::CopySource:       return VK_ACCESS_TRANSFER_READ_BIT;
    case ResourceState::CopyDestination:  return VK_ACCESS_TRANSFER_WRITE_BIT;
    case ResourceState::VertexBuffer:     return VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    case ResourceState::IndexBuffer:      return VK_ACCESS_INDEX_READ_BIT;
    case ResourceState::ConstantBuffer:   return VK_ACCESS_UNIFORM_READ_BIT;
    case ResourceState::ShaderRead:       return VK_ACCESS_SHADER_READ_BIT;
    case ResourceState::ShaderWrite:      return VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    case ResourceState::RenderTarget:     return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    case ResourceState::DepthRead:        return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    case ResourceState::DepthWrite:       return VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    case ResourceState::IndirectArgument: return VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    default:                              return 0;
    }
}

static VkDeviceSize toVkWholeSize(u64 size) {
    return size == WHOLE_SIZE ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(size);
}

/// 记录每类 Vulkan 队列对应的 queue family index。
struct VulkanQueueFamilies {
    u32 graphics = INVALID_INDEX; ///< 图形队列族，必须支持 VK_QUEUE_GRAPHICS_BIT。
    u32 compute = INVALID_INDEX; ///< 计算队列族，优先选择独立 compute 队列。
    u32 transfer = INVALID_INDEX; ///< 传输队列族，优先选择独立 transfer 队列。
    u32 present = INVALID_INDEX; ///< 呈现队列族，需要 surface 支持。
};

struct VulkanRenderer::Impl {
    struct BufferResource {
        BufferDesc desc{};
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };

    struct TextureResource {
        TextureDesc desc{};
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        ResourceState currentState = ResourceState::Undefined;
        bool ownsImage = true;
    };

    struct TextureViewResource {
        TextureViewDesc desc{};
        VkImageView view = VK_NULL_HANDLE;
    };

    struct SamplerResource {
        SamplerDesc desc{};
        VkSampler sampler = VK_NULL_HANDLE;
    };

    struct ShaderResource {
        ShaderDesc desc{};
        VkShaderModule module = VK_NULL_HANDLE;
    };

    struct BindGroupLayoutResource {
        BindGroupLayoutDesc desc{};
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    };

    struct BindGroupResource {
        BindGroupDesc desc{};
        VkDescriptorSet set = VK_NULL_HANDLE;
    };

    struct PipelineLayoutResource {
        PipelineLayoutDesc desc{};
        VkPipelineLayout layout = VK_NULL_HANDLE;
    };

    struct PipelineResource {
        VkPipeline pipeline = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    };

    struct PipelineCacheResource {
        PipelineCacheDesc desc{};
        VkPipelineCache cache = VK_NULL_HANDLE;
    };

    struct QueryPoolResource {
        QueryPoolDesc desc{};
        VkQueryPool pool = VK_NULL_HANDLE;
    };

    struct SemaphoreResource {
        SemaphoreDesc desc{};
        VkSemaphore semaphore = VK_NULL_HANDLE;
    };

    struct FenceResource {
        FenceDesc desc{};
        VkFence fence = VK_NULL_HANDLE;
    };

    struct SwapchainResource {
        SwapchainDesc desc{};
        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkExtent2D extent{};
        std::vector<TextureHandle> images;
        std::vector<TextureViewHandle> imageViews;
    };

    VulkanNativeHandles native{};
    RenderCapabilities caps{};
    VulkanRendererDesc initDesc{};
    VulkanQueueFamilies queueFamilies{};

    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    PFN_vkSetDebugUtilsObjectNameEXT setDebugUtilsObjectName = nullptr;
    VkCommandPool graphicsCommandPool = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    bool ownsSurface = false;
    bool supportsTimelineSemaphore = false;

    std::vector<BufferResource> buffers;
    std::vector<TextureResource> textures;
    std::vector<TextureViewResource> textureViews;
    std::vector<SamplerResource> samplers;
    std::vector<ShaderResource> shaders;
    std::vector<BindGroupLayoutResource> bindGroupLayouts;
    std::vector<BindGroupResource> bindGroups;
    std::vector<PipelineLayoutResource> pipelineLayouts;
    std::vector<PipelineResource> pipelines;
    std::vector<PipelineCacheResource> pipelineCaches;
    std::vector<QueryPoolResource> queryPools;
    std::vector<SemaphoreResource> semaphores;
    std::vector<FenceResource> fences;
    std::vector<SwapchainResource> swapchains;

    void setObjectName(VkObjectType type, u64 object, const std::string& name) const {
        if (setDebugUtilsObjectName == nullptr || object == 0 || name.empty()) {
            return;
        }
        VkDebugUtilsObjectNameInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
        info.objectType = type;
        info.objectHandle = object;
        info.pObjectName = name.c_str();
        setDebugUtilsObjectName(native.device, &info);
    }

    u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const {
        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(native.physicalDevice, &memoryProperties);
        for (u32 index = 0; index < memoryProperties.memoryTypeCount; ++index) {
            const bool typeMatches = (typeBits & (1u << index)) != 0;
            const bool flagsMatch = (memoryProperties.memoryTypes[index].propertyFlags & properties) == properties;
            if (typeMatches && flagsMatch) {
                return index;
            }
        }
        throw std::runtime_error("找不到符合要求的 Vulkan memory type");
    }

    VkQueue queueForType(QueueType type) const {
        switch (type) {
        case QueueType::Graphics: return native.graphicsQueue;
        case QueueType::Compute:  return native.computeQueue;
        case QueueType::Transfer: return native.transferQueue != VK_NULL_HANDLE ? native.transferQueue : native.graphicsQueue;
        case QueueType::Present:  return native.presentQueue != VK_NULL_HANDLE ? native.presentQueue : native.graphicsQueue;
        }
        return native.graphicsQueue;
    }
};

static bool hasLayer(const std::vector<VkLayerProperties>& layers, const char* name) {
    return std::any_of(layers.begin(), layers.end(), [name](const VkLayerProperties& layer) {
        return std::strcmp(layer.layerName, name) == 0;
    });
}

static bool hasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name) {
    return std::any_of(extensions.begin(), extensions.end(), [name](const VkExtensionProperties& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

/// Vulkan 创建信息不应该包含重复扩展名，这里按字符串内容做去重追加。
static void appendUniqueExtension(std::vector<const char*>& extensions, const char* name) {
    const auto exists = std::any_of(extensions.begin(), extensions.end(), [name](const char* existing) {
        return std::strcmp(existing, name) == 0;
    });
    if (!exists) {
        extensions.push_back(name);
    }
}

static std::vector<VkLayerProperties> enumerateInstanceLayers() {
    u32 count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    if (count != 0) {
        vkEnumerateInstanceLayerProperties(&count, layers.data());
    }
    return layers;
}

static std::vector<VkExtensionProperties> enumerateInstanceExtensions() {
    u32 count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    if (count != 0) {
        vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    }
    return extensions;
}

static std::vector<VkExtensionProperties> enumerateDeviceExtensions(VkPhysicalDevice device) {
    u32 count = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    if (count != 0) {
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data());
    }
    return extensions;
}

static VulkanQueueFamilies findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    VulkanQueueFamilies result{};

    u32 count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

    for (u32 index = 0; index < count; ++index) {
        const VkQueueFlags flags = families[index].queueFlags;
        if (result.graphics == INVALID_INDEX && (flags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            result.graphics = index;
        }
        if ((flags & VK_QUEUE_COMPUTE_BIT) != 0) {
            if (result.compute == INVALID_INDEX || (flags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                result.compute = index;
            }
        }
        if ((flags & VK_QUEUE_TRANSFER_BIT) != 0) {
            if (result.transfer == INVALID_INDEX || ((flags & VK_QUEUE_GRAPHICS_BIT) == 0 && (flags & VK_QUEUE_COMPUTE_BIT) == 0)) {
                result.transfer = index;
            }
        }
        if (surface != VK_NULL_HANDLE) {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presentSupported);
            if (presentSupported && result.present == INVALID_INDEX) {
                result.present = index;
            }
        }
    }

    if (result.transfer == INVALID_INDEX) result.transfer = result.graphics;
    if (surface == VK_NULL_HANDLE && result.present == INVALID_INDEX) result.present = result.graphics;
    return result;
}

static bool deviceSupportsRequiredExtensions(
    VkPhysicalDevice device,
    const std::vector<const char*>& requiredExtensions,
    bool needsSwapchain) {
    const std::vector<VkExtensionProperties> available = enumerateDeviceExtensions(device);
    for (const char* extension : requiredExtensions) {
        if (!hasExtension(available, extension)) {
            return false;
        }
    }
    return !needsSwapchain || hasExtension(available, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
}

/// 汇总物理设备的 core/1.2/1.3 feature，后续打分、启用和能力查询都用同一份结果。
struct VulkanDeviceSupport {
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceVulkan12Features features12{};
    VkPhysicalDeviceVulkan13Features features13{};
};

static VulkanDeviceSupport queryVulkanDeviceSupport(VkPhysicalDevice device) {
    VulkanDeviceSupport support{};
    vkGetPhysicalDeviceProperties(device, &support.properties);

    support.features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    support.features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    support.features12.pNext = &support.features13;

    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &support.features12;
    vkGetPhysicalDeviceFeatures2(device, &features2);
    support.features = features2.features;
    support.features12.pNext = nullptr;
    support.features13.pNext = nullptr;
    return support;
}

static bool supportsRequiredRenderFeatures(const VulkanDeviceSupport& support, const VulkanQueueFamilies& queues, RenderFeature required) {
    if (hasAny(required, RenderFeature::Compute) && queues.compute == INVALID_INDEX) return false;
    if (hasAny(required, RenderFeature::SamplerAnisotropy) && support.features.samplerAnisotropy != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::GeometryShader) && support.features.geometryShader != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::Tessellation) && support.features.tessellationShader != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::TimestampQuery) && support.properties.limits.timestampComputeAndGraphics != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::OcclusionQuery) && support.features.occlusionQueryPrecise != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::PipelineStatisticsQuery) && support.features.pipelineStatisticsQuery != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::DrawIndirectCount) && support.features12.drawIndirectCount != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::DynamicRendering) && support.features13.dynamicRendering != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::TextureCompressionBC) && support.features.textureCompressionBC != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::TextureCompressionETC2) && support.features.textureCompressionETC2 != VK_TRUE) return false;
    if (hasAny(required, RenderFeature::TextureCompressionASTC) && support.features.textureCompressionASTC_LDR != VK_TRUE) return false;

    // 这些功能需要额外扩展、feature chain 或资源模型；当前后端尚未实现，不能声明为可用。
    const RenderFeature unsupportedByThisBackend =
        RenderFeature::MeshShader |
        RenderFeature::RayTracing |
        RenderFeature::Bindless |
        RenderFeature::ConservativeRasterization |
        RenderFeature::Multiview;
    return !hasAny(required, unsupportedByThisBackend);
}

static int scorePhysicalDevice(VkPhysicalDevice device, VkSurfaceKHR surface, const VulkanRendererDesc& desc) {
    const VulkanDeviceSupport support = queryVulkanDeviceSupport(device);

    const auto queues = findQueueFamilies(device, surface);
    if (queues.graphics == INVALID_INDEX) {
        return -1;
    }
    if (surface != VK_NULL_HANDLE && queues.present == INVALID_INDEX) {
        return -1;
    }
    if (!deviceSupportsRequiredExtensions(device, desc.requiredDeviceExtensions, surface != VK_NULL_HANDLE)) {
        return -1;
    }

    int score = 0;
    if (support.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
    if (support.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score += 300;
    score += static_cast<int>(support.properties.limits.maxImageDimension2D / 1024);

    if (desc.backend.powerPreference == PowerPreference::LowPower &&
        support.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
        score += 1000;
    }

    if (!supportsRequiredRenderFeatures(support, queues, desc.backend.requiredFeatures)) {
        return -1;
    }

    return score;
}

static VkDebugUtilsMessengerCreateInfoEXT makeDebugMessengerCreateInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = vulkanDebugCallback;
    return info;
}

VulkanRenderer::VulkanRenderer()
    : impl_(std::make_unique<Impl>()) {
}

VulkanRenderer::~VulkanRenderer() {
    shutdown();
}

VulkanRenderer::VulkanRenderer(VulkanRenderer&&) noexcept = default;

VulkanRenderer& VulkanRenderer::operator=(VulkanRenderer&&) noexcept = default;

bool VulkanRenderer::initialize(const VulkanRendererDesc& desc, std::string* errorMessage) {
    try {
        if (isInitialized()) {
            shutdown();
        }

        impl_ = std::make_unique<Impl>();
        impl_->initDesc = desc;
        impl_->native.surface = desc.surface.surface;
        impl_->ownsSurface = desc.surface.ownsSurface;

        const bool wantsValidation = desc.backend.validation != ValidationMode::Disabled;
        const RenderFeature requestedFeatures = desc.backend.optionalFeatures | desc.backend.requiredFeatures;
        const bool wantsDebugUtils = wantsValidation || hasAny(requestedFeatures, RenderFeature::DebugMarkers);
        const auto availableLayers = enumerateInstanceLayers();
        const auto availableExtensions = enumerateInstanceExtensions();

        std::vector<const char*> layers;
        if (wantsValidation && hasLayer(availableLayers, "VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }

        std::vector<const char*> instanceExtensions;
        for (const char* extension : desc.requiredInstanceExtensions) {
            if (!hasExtension(availableExtensions, extension)) {
                throw std::runtime_error(std::string("缺少 Vulkan instance extension: ") + extension);
            }
            appendUniqueExtension(instanceExtensions, extension);
        }
        for (const char* extension : desc.optionalInstanceExtensions) {
            if (hasExtension(availableExtensions, extension)) {
                appendUniqueExtension(instanceExtensions, extension);
            }
        }
        if (wantsDebugUtils && hasExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            appendUniqueExtension(instanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else if (hasAny(desc.backend.requiredFeatures, RenderFeature::DebugMarkers)) {
            throw std::runtime_error("缺少 Vulkan instance extension: VK_EXT_debug_utils");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = desc.backend.applicationName.empty() ? "VulkanLearn" : desc.backend.applicationName.c_str();
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = desc.backend.engineName.c_str();
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = makeDebugMessengerCreateInfo();

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;
        instanceInfo.enabledLayerCount = static_cast<u32>(layers.size());
        instanceInfo.ppEnabledLayerNames = layers.data();
        instanceInfo.enabledExtensionCount = static_cast<u32>(instanceExtensions.size());
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
        if (wantsValidation) {
            instanceInfo.pNext = &debugCreateInfo;
        }

        if (vkCreateInstance(&instanceInfo, nullptr, &impl_->native.instance) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateInstance 失败");
        }

        auto createDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(impl_->native.instance, "vkCreateDebugUtilsMessengerEXT"));
        if (wantsValidation && createDebugMessenger != nullptr) {
            createDebugMessenger(impl_->native.instance, &debugCreateInfo, nullptr, &impl_->debugMessenger);
        }

        if (impl_->native.surface == VK_NULL_HANDLE && desc.surface.createSurface) {
            // GLFW 等窗口库必须等 VkInstance 创建后才能创建 VkSurfaceKHR。
            impl_->native.surface = desc.surface.createSurface(impl_->native.instance);
            if (impl_->native.surface == VK_NULL_HANDLE) {
                throw std::runtime_error("Vulkan surface 工厂返回了空 VkSurfaceKHR");
            }
        }

        u32 physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(impl_->native.instance, &physicalDeviceCount, nullptr);
        if (physicalDeviceCount == 0) {
            throw std::runtime_error("找不到 Vulkan physical device");
        }

        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        vkEnumeratePhysicalDevices(impl_->native.instance, &physicalDeviceCount, physicalDevices.data());

        int bestScore = -1;
        for (VkPhysicalDevice device : physicalDevices) {
            const int score = scorePhysicalDevice(device, impl_->native.surface, desc);
            if (score > bestScore) {
                bestScore = score;
                impl_->native.physicalDevice = device;
            }
        }
        if (impl_->native.physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("没有符合要求的 Vulkan physical device");
        }

        impl_->queueFamilies = findQueueFamilies(impl_->native.physicalDevice, impl_->native.surface);

        std::set<u32> uniqueFamilies = {
            impl_->queueFamilies.graphics,
            impl_->queueFamilies.compute,
            impl_->queueFamilies.transfer,
            impl_->queueFamilies.present
        };
        uniqueFamilies.erase(INVALID_INDEX);

        const float queuePriority = 1.0F;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (u32 family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueInfos.push_back(queueInfo);
        }

        const auto availableDeviceExtensions = enumerateDeviceExtensions(impl_->native.physicalDevice);
        std::vector<const char*> deviceExtensions;
        for (const char* extension : desc.requiredDeviceExtensions) {
            if (!hasExtension(availableDeviceExtensions, extension)) {
                throw std::runtime_error(std::string("缺少 Vulkan device extension: ") + extension);
            }
            appendUniqueExtension(deviceExtensions, extension);
        }
        for (const char* extension : desc.optionalDeviceExtensions) {
            if (hasExtension(availableDeviceExtensions, extension)) {
                appendUniqueExtension(deviceExtensions, extension);
            }
        }
        if (impl_->native.surface != VK_NULL_HANDLE) {
            appendUniqueExtension(deviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        const VulkanDeviceSupport support = queryVulkanDeviceSupport(impl_->native.physicalDevice);

        VkPhysicalDeviceFeatures enabledFeatures{};
        enabledFeatures.samplerAnisotropy = support.features.samplerAnisotropy;
        enabledFeatures.geometryShader = support.features.geometryShader && hasAny(desc.backend.optionalFeatures | desc.backend.requiredFeatures, RenderFeature::GeometryShader);
        enabledFeatures.tessellationShader = support.features.tessellationShader && hasAny(desc.backend.optionalFeatures | desc.backend.requiredFeatures, RenderFeature::Tessellation);

        VkPhysicalDeviceVulkan12Features enabled12{};
        enabled12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        enabled12.timelineSemaphore = support.features12.timelineSemaphore;
        enabled12.drawIndirectCount = support.features12.drawIndirectCount;

        VkPhysicalDeviceVulkan13Features enabled13{};
        enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        enabled13.dynamicRendering = support.features13.dynamicRendering;
        enabled13.synchronization2 = support.features13.synchronization2;
        enabled12.pNext = &enabled13;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = &enabled12;
        deviceInfo.queueCreateInfoCount = static_cast<u32>(queueInfos.size());
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = static_cast<u32>(deviceExtensions.size());
        deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
        deviceInfo.pEnabledFeatures = &enabledFeatures;

        if (vkCreateDevice(impl_->native.physicalDevice, &deviceInfo, nullptr, &impl_->native.device) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDevice 失败");
        }

        vkGetDeviceQueue(impl_->native.device, impl_->queueFamilies.graphics, 0, &impl_->native.graphicsQueue);
        if (impl_->queueFamilies.compute != INVALID_INDEX) {
            vkGetDeviceQueue(impl_->native.device, impl_->queueFamilies.compute, 0, &impl_->native.computeQueue);
        }
        if (impl_->queueFamilies.transfer != INVALID_INDEX) {
            vkGetDeviceQueue(impl_->native.device, impl_->queueFamilies.transfer, 0, &impl_->native.transferQueue);
        }
        if (impl_->queueFamilies.present != INVALID_INDEX) {
            vkGetDeviceQueue(impl_->native.device, impl_->queueFamilies.present, 0, &impl_->native.presentQueue);
        }
        impl_->native.graphicsQueueFamily = impl_->queueFamilies.graphics;
        impl_->native.computeQueueFamily = impl_->queueFamilies.compute;
        impl_->native.transferQueueFamily = impl_->queueFamilies.transfer;
        impl_->native.presentQueueFamily = impl_->queueFamilies.present;

        impl_->setDebugUtilsObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetInstanceProcAddr(impl_->native.instance, "vkSetDebugUtilsObjectNameEXT"));

        VkCommandPoolCreateInfo commandPoolInfo{};
        commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        commandPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = impl_->queueFamilies.graphics;
        if (vkCreateCommandPool(impl_->native.device, &commandPoolInfo, nullptr, &impl_->graphicsCommandPool) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateCommandPool(graphics) 失败");
        }

        std::array<VkDescriptorPoolSize, 6> poolSizes{{
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024},
            {VK_DESCRIPTOR_TYPE_SAMPLER, 2048},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096}
        }};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 4096;
        poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        if (vkCreateDescriptorPool(impl_->native.device, &poolInfo, nullptr, &impl_->descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorPool 失败");
        }

        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(impl_->native.physicalDevice, &memoryProperties);

        impl_->caps.api = GraphicsApi::Vulkan;
        impl_->caps.adapterName = support.properties.deviceName;
        impl_->caps.maxTexture2DSize = support.properties.limits.maxImageDimension2D;
        impl_->caps.maxTexture3DSize = support.properties.limits.maxImageDimension3D;
        impl_->caps.maxTextureCubeSize = support.properties.limits.maxImageDimensionCube;
        impl_->caps.maxTextureArrayLayers = support.properties.limits.maxImageArrayLayers;
        impl_->caps.maxColorAttachments = support.properties.limits.maxColorAttachments;
        impl_->caps.maxBindGroups = support.properties.limits.maxBoundDescriptorSets;
        impl_->caps.maxVertexBuffers = support.properties.limits.maxVertexInputBindings;
        impl_->caps.maxVertexAttributes = support.properties.limits.maxVertexInputAttributes;
        impl_->caps.maxPushConstantSize = support.properties.limits.maxPushConstantsSize;
        impl_->caps.minUniformBufferOffsetAlignment = support.properties.limits.minUniformBufferOffsetAlignment;
        impl_->caps.minStorageBufferOffsetAlignment = support.properties.limits.minStorageBufferOffsetAlignment;
        impl_->caps.optimalBufferCopyOffsetAlignment = support.properties.limits.optimalBufferCopyOffsetAlignment;
        impl_->caps.optimalBufferCopyRowPitchAlignment = support.properties.limits.optimalBufferCopyRowPitchAlignment;
        impl_->caps.maxSamplerAnisotropy = support.properties.limits.maxSamplerAnisotropy;
        impl_->caps.supportsCompute = impl_->queueFamilies.compute != INVALID_INDEX;
        impl_->caps.supportsGeometryShader = support.features.geometryShader == VK_TRUE;
        impl_->caps.supportsTessellation = support.features.tessellationShader == VK_TRUE;
        impl_->caps.supportsSamplerAnisotropy = support.features.samplerAnisotropy == VK_TRUE;
        impl_->caps.supportsSamplerCompare = true;
        impl_->caps.supportsTimestampQuery = support.properties.limits.timestampComputeAndGraphics == VK_TRUE;
        impl_->caps.supportsOcclusionQuery = support.features.occlusionQueryPrecise == VK_TRUE;
        impl_->caps.supportsPipelineStatisticsQuery = support.features.pipelineStatisticsQuery == VK_TRUE;
        impl_->caps.supportsIndirectDraw = true;
        impl_->caps.supportsDrawIndirectCount = support.features12.drawIndirectCount == VK_TRUE;
        impl_->caps.supportsDynamicRendering = support.features13.dynamicRendering == VK_TRUE;
        impl_->caps.supportsDebugMarkers = impl_->setDebugUtilsObjectName != nullptr;
        impl_->caps.supportsTextureCompressionBC = support.features.textureCompressionBC == VK_TRUE;
        impl_->caps.supportsTextureCompressionETC2 = support.features.textureCompressionETC2 == VK_TRUE;
        impl_->caps.supportsTextureCompressionASTC = support.features.textureCompressionASTC_LDR == VK_TRUE;
        impl_->supportsTimelineSemaphore = support.features12.timelineSemaphore == VK_TRUE;

        if (impl_->caps.supportsCompute) impl_->caps.features |= RenderFeature::Compute;
        if (impl_->caps.supportsGeometryShader) impl_->caps.features |= RenderFeature::GeometryShader;
        if (impl_->caps.supportsTessellation) impl_->caps.features |= RenderFeature::Tessellation;
        if (impl_->caps.supportsSamplerAnisotropy) impl_->caps.features |= RenderFeature::SamplerAnisotropy;
        if (impl_->caps.supportsTimestampQuery) impl_->caps.features |= RenderFeature::TimestampQuery;
        if (impl_->caps.supportsOcclusionQuery) impl_->caps.features |= RenderFeature::OcclusionQuery;
        if (impl_->caps.supportsPipelineStatisticsQuery) impl_->caps.features |= RenderFeature::PipelineStatisticsQuery;
        if (impl_->caps.supportsDynamicRendering) impl_->caps.features |= RenderFeature::DynamicRendering;
        if (impl_->caps.supportsDebugMarkers) impl_->caps.features |= RenderFeature::DebugMarkers;
        if (impl_->caps.supportsTextureCompressionBC) impl_->caps.features |= RenderFeature::TextureCompressionBC;
        if (impl_->caps.supportsTextureCompressionETC2) impl_->caps.features |= RenderFeature::TextureCompressionETC2;
        if (impl_->caps.supportsTextureCompressionASTC) impl_->caps.features |= RenderFeature::TextureCompressionASTC;

        for (u32 i = 0; i < memoryProperties.memoryHeapCount; ++i) {
            if ((memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                impl_->caps.dedicatedVideoMemory += memoryProperties.memoryHeaps[i].size;
            } else {
                impl_->caps.sharedSystemMemory += memoryProperties.memoryHeaps[i].size;
            }
        }

        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        shutdown();
        return false;
    }
}

void VulkanRenderer::shutdown() noexcept {
    if (!impl_) {
        return;
    }

    if (impl_->native.device == VK_NULL_HANDLE) {
        if (impl_->debugMessenger != VK_NULL_HANDLE) {
            auto destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(impl_->native.instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyDebugMessenger != nullptr) {
                destroyDebugMessenger(impl_->native.instance, impl_->debugMessenger, nullptr);
            }
            impl_->debugMessenger = VK_NULL_HANDLE;
        }

        if (impl_->native.instance != VK_NULL_HANDLE && impl_->ownsSurface && impl_->native.surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(impl_->native.instance, impl_->native.surface, nullptr);
            impl_->native.surface = VK_NULL_HANDLE;
        }

        if (impl_->native.instance != VK_NULL_HANDLE) {
            vkDestroyInstance(impl_->native.instance, nullptr);
            impl_->native.instance = VK_NULL_HANDLE;
        }
        return;
    }

    vkDeviceWaitIdle(impl_->native.device);

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

    if (impl_->graphicsCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(impl_->native.device, impl_->graphicsCommandPool, nullptr);
        impl_->graphicsCommandPool = VK_NULL_HANDLE;
    }

    if (impl_->descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(impl_->native.device, impl_->descriptorPool, nullptr);
        impl_->descriptorPool = VK_NULL_HANDLE;
    }

    vkDestroyDevice(impl_->native.device, nullptr);
    impl_->native.device = VK_NULL_HANDLE;

    if (impl_->debugMessenger != VK_NULL_HANDLE) {
        auto destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(impl_->native.instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyDebugMessenger != nullptr) {
            destroyDebugMessenger(impl_->native.instance, impl_->debugMessenger, nullptr);
        }
        impl_->debugMessenger = VK_NULL_HANDLE;
    }

    if (impl_->ownsSurface && impl_->native.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(impl_->native.instance, impl_->native.surface, nullptr);
        impl_->native.surface = VK_NULL_HANDLE;
    }

    if (impl_->native.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(impl_->native.instance, nullptr);
        impl_->native.instance = VK_NULL_HANDLE;
    }
}

bool VulkanRenderer::isInitialized() const noexcept {
    return impl_ != nullptr && impl_->native.device != VK_NULL_HANDLE;
}

const RenderCapabilities& VulkanRenderer::capabilities() const noexcept {
    return impl_->caps;
}

const VulkanNativeHandles& VulkanRenderer::nativeHandles() const noexcept {
    return impl_->native;
}

BufferHandle VulkanRenderer::createBuffer(const BufferDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("VulkanRenderer 尚未初始化");
    }
    if (desc.size == 0) {
        throw std::runtime_error("BufferDesc::size 必须大于 0");
    }

    Impl::BufferResource resource{};
    resource.desc = desc;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(desc.size);
    bufferInfo.usage = toVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (hasAny(desc.flags, BufferCreateFlags::SparseBinding)) {
        bufferInfo.flags |= VK_BUFFER_CREATE_SPARSE_BINDING_BIT;
    }

    if (vkCreateBuffer(impl_->native.device, &bufferInfo, nullptr, &resource.buffer) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateBuffer 失败");
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(impl_->native.device, resource.buffer, &requirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = impl_->findMemoryType(requirements.memoryTypeBits, toVkMemoryProperties(desc.memoryUsage));

    if (vkAllocateMemory(impl_->native.device, &allocateInfo, nullptr, &resource.memory) != VK_SUCCESS) {
        vkDestroyBuffer(impl_->native.device, resource.buffer, nullptr);
        throw std::runtime_error("vkAllocateMemory(buffer) 失败");
    }
    vkBindBufferMemory(impl_->native.device, resource.buffer, resource.memory, 0);

    if (desc.persistentlyMapped) {
        vkMapMemory(impl_->native.device, resource.memory, 0, VK_WHOLE_SIZE, 0, &resource.mapped);
    }

    const BufferHandle handle = makeRenderHandle<BufferHandle>(impl_->buffers, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<u64>(impl_->buffers.back().buffer), desc.debugName);
    return handle;
}

TextureHandle VulkanRenderer::createTexture(const TextureDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("VulkanRenderer 尚未初始化");
    }

    Impl::TextureResource resource{};
    resource.desc = desc;
    resource.currentState = desc.initialState;
    resource.ownsImage = true;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = toVkImageCreateFlags(desc.flags);
    imageInfo.imageType = toVkImageType(desc.dimension);
    imageInfo.format = toVkFormat(desc.format);
    imageInfo.extent = {desc.extent.width, desc.extent.height, desc.extent.depth};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.samples = toVkSampleCount(desc.samples);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVkImageUsage(desc.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(impl_->native.device, &imageInfo, nullptr, &resource.image) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage 失败");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(impl_->native.device, resource.image, &requirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = impl_->findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(impl_->native.device, &allocateInfo, nullptr, &resource.memory) != VK_SUCCESS) {
        vkDestroyImage(impl_->native.device, resource.image, nullptr);
        throw std::runtime_error("vkAllocateMemory(image) 失败");
    }
    vkBindImageMemory(impl_->native.device, resource.image, resource.memory, 0);

    const TextureHandle handle = makeRenderHandle<TextureHandle>(impl_->textures, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<u64>(impl_->textures.back().image), desc.debugName);
    return handle;
}

TextureViewHandle VulkanRenderer::createTextureView(const TextureViewDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("VulkanRenderer 尚未初始化");
    }

    const Impl::TextureResource* texture = getRenderResource(impl_->textures, desc.texture);
    if (texture == nullptr || texture->image == VK_NULL_HANDLE) {
        throw std::runtime_error("TextureViewDesc::texture 无效");
    }

    const Format viewFormat = desc.format == Format::Undefined ? texture->desc.format : desc.format;

    Impl::TextureViewResource resource{};
    resource.desc = desc;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->image;
    viewInfo.viewType = toVkImageViewType(desc.dimension);
    viewInfo.format = toVkFormat(viewFormat);
    viewInfo.subresourceRange.aspectMask = toVkImageAspect(desc.aspect, viewFormat);
    viewInfo.subresourceRange.baseMipLevel = desc.baseMipLevel;
    viewInfo.subresourceRange.levelCount = desc.mipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
    viewInfo.subresourceRange.layerCount = desc.arrayLayerCount;

    if (vkCreateImageView(impl_->native.device, &viewInfo, nullptr, &resource.view) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView 失败");
    }

    const TextureViewHandle handle = makeRenderHandle<TextureViewHandle>(impl_->textureViews, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<u64>(impl_->textureViews.back().view), desc.debugName);
    return handle;
}

SamplerHandle VulkanRenderer::createSampler(const SamplerDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("VulkanRenderer 尚未初始化");
    }

    Impl::SamplerResource resource{};
    resource.desc = desc;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = toVkFilter(desc.magFilter);
    samplerInfo.minFilter = toVkFilter(desc.minFilter);
    samplerInfo.mipmapMode = toVkMipmapMode(desc.mipmapMode);
    samplerInfo.addressModeU = toVkAddressMode(desc.addressU);
    samplerInfo.addressModeV = toVkAddressMode(desc.addressV);
    samplerInfo.addressModeW = toVkAddressMode(desc.addressW);
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.anisotropyEnable = desc.enableAnisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = std::min(desc.maxAnisotropy, impl_->caps.maxSamplerAnisotropy);
    samplerInfo.compareEnable = desc.enableCompare ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = toVkCompareOp(desc.compareOp);
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;
    samplerInfo.borderColor = toVkBorderColor(desc.borderColor);

    if (vkCreateSampler(impl_->native.device, &samplerInfo, nullptr, &resource.sampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler 失败");
    }

    const SamplerHandle handle = makeRenderHandle<SamplerHandle>(impl_->samplers, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<u64>(impl_->samplers.back().sampler), desc.debugName);
    return handle;
}

ShaderHandle VulkanRenderer::createShaderModule(const ShaderDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("VulkanRenderer 尚未初始化");
    }

    std::vector<std::byte> bytecode = desc.bytecode;
    if (bytecode.empty() && !desc.filePath.empty()) {
        bytecode = readBinaryFile(desc.filePath);
    }
    if (bytecode.empty() || (bytecode.size() % sizeof(u32)) != 0) {
        throw std::runtime_error("Vulkan shader module 需要非空且 4 字节对齐的 SPIR-V bytecode");
    }

    Impl::ShaderResource resource{};
    resource.desc = desc;

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = bytecode.size();
    moduleInfo.pCode = reinterpret_cast<const u32*>(bytecode.data());

    if (vkCreateShaderModule(impl_->native.device, &moduleInfo, nullptr, &resource.module) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateShaderModule 失败");
    }

    const ShaderHandle handle = makeRenderHandle<ShaderHandle>(impl_->shaders, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<u64>(impl_->shaders.back().module), desc.debugName);
    return handle;
}

BindGroupLayoutHandle VulkanRenderer::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("VulkanRenderer 尚未初始化");
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.entries.size());
    for (const BindGroupLayoutEntry& entry : desc.entries) {
        if (entry.type == BindingType::PushConstant) {
            // Vulkan push constant 属于 pipeline layout，不占用 descriptor set binding。
            continue;
        }
        if (entry.type == BindingType::AccelerationStructure) {
            throw std::runtime_error("AccelerationStructure binding 需要光追资源模型，当前 Vulkan 后端尚未实现");
        }
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = entry.binding;
        binding.descriptorType = toVkDescriptorType(entry.type);
        binding.descriptorCount = std::max(1u, entry.arrayCount);
        binding.stageFlags = toVkShaderStages(entry.visibility);
        bindings.push_back(binding);
    }

    Impl::BindGroupLayoutResource resource{};
    resource.desc = desc;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(impl_->native.device, &layoutInfo, nullptr, &resource.layout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout 失败");
    }

    const BindGroupLayoutHandle handle = makeRenderHandle<BindGroupLayoutHandle>(impl_->bindGroupLayouts, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<u64>(impl_->bindGroupLayouts.back().layout), desc.debugName);
    return handle;
}

BindGroupHandle VulkanRenderer::createBindGroup(const BindGroupDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("VulkanRenderer 尚未初始化");
    }

    const Impl::BindGroupLayoutResource* layout = getRenderResource(impl_->bindGroupLayouts, desc.layout);
    if (layout == nullptr || layout->layout == VK_NULL_HANDLE) {
        throw std::runtime_error("BindGroupDesc::layout 无效");
    }

    Impl::BindGroupResource resource{};
    resource.desc = desc;

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = impl_->descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &layout->layout;

    if (vkAllocateDescriptorSets(impl_->native.device, &allocateInfo, &resource.set) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets 失败");
    }

    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet> writes;
    bufferInfos.reserve(desc.bindings.size());
    imageInfos.reserve(desc.bindings.size());
    writes.reserve(desc.bindings.size());

    for (const ResourceBinding& binding : desc.bindings) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = resource.set;
        write.dstBinding = binding.binding;
        write.dstArrayElement = binding.arrayElement;
        write.descriptorCount = 1;
        write.descriptorType = toVkDescriptorType(binding.type);

        if (binding.type == BindingType::UniformBuffer || binding.type == BindingType::StorageBuffer) {
            const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, binding.buffer.buffer);
            if (buffer == nullptr || buffer->buffer == VK_NULL_HANDLE) {
                throw std::runtime_error("ResourceBinding buffer 无效");
            }
            VkDescriptorBufferInfo info{};
            info.buffer = buffer->buffer;
            info.offset = binding.buffer.offset;
            info.range = toVkWholeSize(binding.buffer.size);
            bufferInfos.push_back(info);
            write.pBufferInfo = &bufferInfos.back();
        } else if (binding.type == BindingType::Sampler) {
            const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
            if (sampler == nullptr || sampler->sampler == VK_NULL_HANDLE) {
                throw std::runtime_error("ResourceBinding sampler 无效");
            }
            VkDescriptorImageInfo info{};
            info.sampler = sampler->sampler;
            imageInfos.push_back(info);
            write.pImageInfo = &imageInfos.back();
        } else if (binding.type == BindingType::SampledTexture ||
                   binding.type == BindingType::StorageTexture ||
                   binding.type == BindingType::CombinedTextureSampler) {
            const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, binding.texture.view);
            if (view == nullptr || view->view == VK_NULL_HANDLE) {
                throw std::runtime_error("ResourceBinding texture view 无效");
            }
            VkDescriptorImageInfo info{};
            info.imageView = view->view;
            info.imageLayout = binding.type == BindingType::StorageTexture ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (binding.type == BindingType::CombinedTextureSampler) {
                const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
                if (sampler == nullptr || sampler->sampler == VK_NULL_HANDLE) {
                    throw std::runtime_error("CombinedTextureSampler 缺少有效 sampler");
                }
                info.sampler = sampler->sampler;
            }
            imageInfos.push_back(info);
            write.pImageInfo = &imageInfos.back();
        } else if (binding.type == BindingType::AccelerationStructure) {
            throw std::runtime_error("AccelerationStructure descriptor update 尚未实现");
        } else {
            continue;
        }

        writes.push_back(write);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(impl_->native.device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    }

    return makeRenderHandle<BindGroupHandle>(impl_->bindGroups, std::move(resource));
}

PipelineLayoutHandle VulkanRenderer::createPipelineLayout(const PipelineLayoutDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("VulkanRenderer 尚未初始化");
    }

    std::vector<VkDescriptorSetLayout> setLayouts;
    setLayouts.reserve(desc.bindGroupLayouts.size());
    for (BindGroupLayoutHandle handle : desc.bindGroupLayouts) {
        const Impl::BindGroupLayoutResource* layout = getRenderResource(impl_->bindGroupLayouts, handle);
        if (layout == nullptr || layout->layout == VK_NULL_HANDLE) {
            throw std::runtime_error("PipelineLayoutDesc 包含无效 bind group layout");
        }
        setLayouts.push_back(layout->layout);
    }

    std::vector<VkPushConstantRange> pushRanges;
    pushRanges.reserve(desc.pushConstants.size());
    for (const PushConstantRange& range : desc.pushConstants) {
        VkPushConstantRange vkRange{};
        vkRange.stageFlags = toVkShaderStages(range.stages);
        vkRange.offset = range.offset;
        vkRange.size = range.size;
        pushRanges.push_back(vkRange);
    }

    Impl::PipelineLayoutResource resource{};
    resource.desc = desc;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<u32>(pushRanges.size());
    layoutInfo.pPushConstantRanges = pushRanges.data();

    if (vkCreatePipelineLayout(impl_->native.device, &layoutInfo, nullptr, &resource.layout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout 失败");
    }

    const PipelineLayoutHandle handle = makeRenderHandle<PipelineLayoutHandle>(impl_->pipelineLayouts, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<u64>(impl_->pipelineLayouts.back().layout), desc.debugName);
    return handle;
}

PipelineCacheHandle VulkanRenderer::createPipelineCache(const PipelineCacheDesc& desc) {
    Impl::PipelineCacheResource resource{};
    resource.desc = desc;

    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = desc.initialData.size();
    cacheInfo.pInitialData = desc.initialData.empty() ? nullptr : desc.initialData.data();

    if (vkCreatePipelineCache(impl_->native.device, &cacheInfo, nullptr, &resource.cache) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineCache 失败");
    }

    const PipelineCacheHandle handle = makeRenderHandle<PipelineCacheHandle>(impl_->pipelineCaches, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_PIPELINE_CACHE, reinterpret_cast<u64>(impl_->pipelineCaches.back().cache), desc.debugName);
    return handle;
}

static VkStencilOpState toVkStencilState(const StencilFaceState& state) {
    VkStencilOpState vkState{};
    vkState.failOp = toVkStencilOp(state.failOp);
    vkState.passOp = toVkStencilOp(state.passOp);
    vkState.depthFailOp = toVkStencilOp(state.depthFailOp);
    vkState.compareOp = toVkCompareOp(state.compareOp);
    vkState.compareMask = state.compareMask;
    vkState.writeMask = state.writeMask;
    vkState.reference = state.reference;
    return vkState;
}

PipelineHandle VulkanRenderer::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (!impl_->caps.supportsDynamicRendering) {
        throw std::runtime_error("当前 Vulkan 图形管线实现需要 dynamic rendering");
    }

    const Impl::PipelineLayoutResource* layout = getRenderResource(impl_->pipelineLayouts, desc.layout);
    if (layout == nullptr || layout->layout == VK_NULL_HANDLE) {
        throw std::runtime_error("GraphicsPipelineDesc::layout 无效");
    }

    std::vector<VkShaderModule> temporaryModules;
    std::vector<ShaderHandle> temporaryShaderHandles;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    temporaryModules.reserve(desc.shaders.size());
    temporaryShaderHandles.reserve(desc.shaders.size());
    shaderStages.reserve(desc.shaders.size());

    const auto destroyTemporaryShaderModules = [&]() noexcept {
        for (ShaderHandle handle : temporaryShaderHandles) {
            destroy(handle);
        }
        temporaryShaderHandles.clear();
        temporaryModules.clear();
    };

    try {
        for (const ShaderDesc& shader : desc.shaders) {
            ShaderHandle shaderHandle = createShaderModule(shader);
            temporaryShaderHandles.push_back(shaderHandle);
            Impl::ShaderResource* shaderResource = getRenderResource(impl_->shaders, shaderHandle);
            temporaryModules.push_back(shaderResource->module);

            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = toVkSingleShaderStage(shader.stage);
            stageInfo.module = shaderResource->module;
            stageInfo.pName = shader.entryPoint.empty() ? "main" : shader.entryPoint.c_str();
            shaderStages.push_back(stageInfo);
        }
    } catch (...) {
        destroyTemporaryShaderModules();
        throw;
    }

    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    for (const VertexBufferLayoutDesc& binding : desc.vertexBuffers) {
        VkVertexInputBindingDescription vkBinding{};
        vkBinding.binding = binding.binding;
        vkBinding.stride = static_cast<u32>(binding.stride);
        vkBinding.inputRate = binding.inputRate == VertexInputRate::PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions.push_back(vkBinding);

        for (const VertexAttributeDesc& attribute : binding.attributes) {
            VkVertexInputAttributeDescription vkAttribute{};
            vkAttribute.location = attribute.location;
            vkAttribute.binding = attribute.binding;
            vkAttribute.format = toVkVertexFormat(attribute.format);
            vkAttribute.offset = static_cast<u32>(attribute.offset);
            attributeDescriptions.push_back(vkAttribute);
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<u32>(bindingDescriptions.size());
    vertexInput.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(attributeDescriptions.size());
    vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVkPrimitiveTopology(desc.inputAssembly.topology);
    inputAssembly.primitiveRestartEnable = desc.inputAssembly.primitiveRestart ? VK_TRUE : VK_FALSE;

    VkPipelineTessellationStateCreateInfo tessellation{};
    tessellation.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellation.patchControlPoints = desc.inputAssembly.patchControlPoints;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = desc.raster.depthClampEnable ? VK_TRUE : VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = toVkPolygonMode(desc.raster.polygonMode);
    raster.cullMode = toVkCullMode(desc.raster.cullMode);
    raster.frontFace = toVkFrontFace(desc.raster.frontFace);
    raster.depthBiasEnable = desc.raster.depthBiasEnable ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = desc.raster.depthBiasConstantFactor;
    raster.depthBiasClamp = desc.raster.depthBiasClamp;
    raster.depthBiasSlopeFactor = desc.raster.depthBiasSlopeFactor;
    raster.lineWidth = desc.raster.lineWidth;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = toVkSampleCount(desc.multisample.samples);
    multisample.sampleShadingEnable = desc.multisample.sampleShadingEnable ? VK_TRUE : VK_FALSE;
    multisample.minSampleShading = desc.multisample.minSampleShading;
    VkSampleMask sampleMask = static_cast<VkSampleMask>(desc.multisample.sampleMask);
    multisample.pSampleMask = &sampleMask;
    multisample.alphaToCoverageEnable = desc.multisample.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
    multisample.alphaToOneEnable = desc.multisample.alphaToOneEnable ? VK_TRUE : VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVkCompareOp(desc.depthStencil.depthCompareOp);
    depthStencil.depthBoundsTestEnable = desc.depthStencil.depthBoundsTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.stencilTestEnable = desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.front = toVkStencilState(desc.depthStencil.front);
    depthStencil.back = toVkStencilState(desc.depthStencil.back);
    depthStencil.minDepthBounds = desc.depthStencil.minDepthBounds;
    depthStencil.maxDepthBounds = desc.depthStencil.maxDepthBounds;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    const size_t attachmentCount = desc.blend.attachments.empty() ? desc.colorFormats.size() : desc.blend.attachments.size();
    blendAttachments.reserve(attachmentCount);
    for (size_t i = 0; i < attachmentCount; ++i) {
        const ColorBlendAttachmentState src = desc.blend.attachments.empty() ? ColorBlendAttachmentState{} : desc.blend.attachments[i];
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = src.blendEnable ? VK_TRUE : VK_FALSE;
        attachment.srcColorBlendFactor = toVkBlendFactor(src.sourceColor);
        attachment.dstColorBlendFactor = toVkBlendFactor(src.destinationColor);
        attachment.colorBlendOp = toVkBlendOp(src.colorOp);
        attachment.srcAlphaBlendFactor = toVkBlendFactor(src.sourceAlpha);
        attachment.dstAlphaBlendFactor = toVkBlendFactor(src.destinationAlpha);
        attachment.alphaBlendOp = toVkBlendOp(src.alphaOp);
        attachment.colorWriteMask = toVkColorWriteMask(src.writeMask);
        blendAttachments.push_back(attachment);
    }

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.logicOpEnable = desc.blend.logicOpEnable ? VK_TRUE : VK_FALSE;
    blend.attachmentCount = static_cast<u32>(blendAttachments.size());
    blend.pAttachments = blendAttachments.data();
    std::copy(desc.blend.blendConstants.begin(), desc.blend.blendConstants.end(), blend.blendConstants);

    std::vector<VkDynamicState> dynamicStates;
    dynamicStates.reserve(desc.dynamicStates.size());
    for (DynamicState state : desc.dynamicStates) {
        dynamicStates.push_back(toVkDynamicState(state));
    }
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<u32>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.colorFormats.size());
    for (Format format : desc.colorFormats) {
        colorFormats.push_back(toVkFormat(format));
    }
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = static_cast<u32>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();
    rendering.depthAttachmentFormat = isDepthFormat(desc.depthStencilFormat) ? toVkFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;
    rendering.stencilAttachmentFormat = hasStencilFormat(desc.depthStencilFormat) ? toVkFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;

    VkPipelineCache cache = VK_NULL_HANDLE;
    if (const Impl::PipelineCacheResource* cacheResource = getRenderResource(impl_->pipelineCaches, desc.cache)) {
        cache = cacheResource->cache;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = static_cast<u32>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = desc.inputAssembly.topology == PrimitiveTopology::PatchList ? &tessellation : nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = layout->layout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = desc.subpass;

    Impl::PipelineResource resource{};
    resource.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    resource.layout = layout->layout;
    if (vkCreateGraphicsPipelines(impl_->native.device, cache, 1, &pipelineInfo, nullptr, &resource.pipeline) != VK_SUCCESS) {
        destroyTemporaryShaderModules();
        throw std::runtime_error("vkCreateGraphicsPipelines 失败");
    }

    destroyTemporaryShaderModules();

    const PipelineHandle handle = makeRenderHandle<PipelineHandle>(impl_->pipelines, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(impl_->pipelines.back().pipeline), desc.debugName);
    return handle;
}

PipelineHandle VulkanRenderer::createComputePipeline(const ComputePipelineDesc& desc) {
    const Impl::PipelineLayoutResource* layout = getRenderResource(impl_->pipelineLayouts, desc.layout);
    if (layout == nullptr || layout->layout == VK_NULL_HANDLE) {
        throw std::runtime_error("ComputePipelineDesc::layout 无效");
    }

    ShaderHandle shaderHandle = createShaderModule(desc.shader);
    Impl::ShaderResource* shader = getRenderResource(impl_->shaders, shaderHandle);

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader->module;
    stage.pName = desc.shader.entryPoint.empty() ? "main" : desc.shader.entryPoint.c_str();

    VkPipelineCache cache = VK_NULL_HANDLE;
    if (const Impl::PipelineCacheResource* cacheResource = getRenderResource(impl_->pipelineCaches, desc.cache)) {
        cache = cacheResource->cache;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = layout->layout;

    Impl::PipelineResource resource{};
    resource.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    resource.layout = layout->layout;
    if (vkCreateComputePipelines(impl_->native.device, cache, 1, &pipelineInfo, nullptr, &resource.pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(impl_->native.device, shader->module, nullptr);
        shader->module = VK_NULL_HANDLE;
        throw std::runtime_error("vkCreateComputePipelines 失败");
    }

    vkDestroyShaderModule(impl_->native.device, shader->module, nullptr);
    shader->module = VK_NULL_HANDLE;

    const PipelineHandle handle = makeRenderHandle<PipelineHandle>(impl_->pipelines, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(impl_->pipelines.back().pipeline), desc.debugName);
    return handle;
}

QueryPoolHandle VulkanRenderer::createQueryPool(const QueryPoolDesc& desc) {
    Impl::QueryPoolResource resource{};
    resource.desc = desc;

    VkQueryPoolCreateInfo queryInfo{};
    queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryInfo.queryCount = desc.queryCount;
    switch (desc.type) {
    case QueryType::Timestamp:
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        break;
    case QueryType::Occlusion:
        queryInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
        break;
    case QueryType::PipelineStatistics:
        queryInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
        queryInfo.pipelineStatistics = 0;
        if (hasAny(desc.statistics, PipelineStatisticFlags::InputAssemblyVertices)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::InputAssemblyPrimitives)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::VertexShaderInvocations)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::GeometryShaderInvocations)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::GeometryShaderPrimitives)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::ClippingInvocations)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::ClippingPrimitives)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::FragmentShaderInvocations)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::TessControlShaderPatches)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::TessEvaluationShaderInvocations)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT;
        if (hasAny(desc.statistics, PipelineStatisticFlags::ComputeShaderInvocations)) queryInfo.pipelineStatistics |= VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
        break;
    }

    if (vkCreateQueryPool(impl_->native.device, &queryInfo, nullptr, &resource.pool) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateQueryPool 失败");
    }

    const QueryPoolHandle handle = makeRenderHandle<QueryPoolHandle>(impl_->queryPools, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_QUERY_POOL, reinterpret_cast<u64>(impl_->queryPools.back().pool), desc.debugName);
    return handle;
}

SemaphoreHandle VulkanRenderer::createSemaphore(const SemaphoreDesc& desc) {
    if (desc.type == SemaphoreType::Timeline && !impl_->supportsTimelineSemaphore) {
        throw std::runtime_error("当前 Vulkan 设备不支持 timeline semaphore");
    }

    Impl::SemaphoreResource resource{};
    resource.desc = desc;

    VkSemaphoreTypeCreateInfo typeInfo{};
    typeInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    typeInfo.semaphoreType = desc.type == SemaphoreType::Timeline ? VK_SEMAPHORE_TYPE_TIMELINE : VK_SEMAPHORE_TYPE_BINARY;
    typeInfo.initialValue = desc.initialValue;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &typeInfo;

    if (vkCreateSemaphore(impl_->native.device, &semaphoreInfo, nullptr, &resource.semaphore) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSemaphore 失败");
    }

    const SemaphoreHandle handle = makeRenderHandle<SemaphoreHandle>(impl_->semaphores, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<u64>(impl_->semaphores.back().semaphore), desc.debugName);
    return handle;
}

FenceHandle VulkanRenderer::createFence(const FenceDesc& desc) {
    Impl::FenceResource resource{};
    resource.desc = desc;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = desc.signaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;

    if (vkCreateFence(impl_->native.device, &fenceInfo, nullptr, &resource.fence) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateFence 失败");
    }

    const FenceHandle handle = makeRenderHandle<FenceHandle>(impl_->fences, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_FENCE, reinterpret_cast<u64>(impl_->fences.back().fence), desc.debugName);
    return handle;
}

SwapchainHandle VulkanRenderer::createSwapchain(const SwapchainDesc& desc) {
    if (impl_->native.surface == VK_NULL_HANDLE) {
        throw std::runtime_error("createSwapchain 需要 initialize 时传入有效 VkSurfaceKHR");
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(impl_->native.physicalDevice, impl_->native.surface, &capabilities);

    u32 formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(impl_->native.physicalDevice, impl_->native.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(impl_->native.physicalDevice, impl_->native.surface, &formatCount, formats.data());

    u32 presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(impl_->native.physicalDevice, impl_->native.surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(impl_->native.physicalDevice, impl_->native.surface, &presentModeCount, presentModes.data());

    VkSurfaceFormatKHR selectedFormat = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
    const VkFormat requestedFormat = toVkFormat(desc.preferredFormat);
    const VkColorSpaceKHR requestedColorSpace = toVkColorSpace(desc.colorSpace);
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == requestedFormat && format.colorSpace == requestedColorSpace) {
            selectedFormat = format;
            break;
        }
    }
    const Format selectedEngineFormat = fromVkFormat(selectedFormat.format);
    const Format swapchainFormat = selectedEngineFormat == Format::Undefined ? desc.preferredFormat : selectedEngineFormat;

    VkPresentModeKHR selectedPresentMode = VK_PRESENT_MODE_FIFO_KHR;
    const VkPresentModeKHR requestedPresentMode = toVkPresentMode(desc.presentMode);
    if (std::find(presentModes.begin(), presentModes.end(), requestedPresentMode) != presentModes.end()) {
        selectedPresentMode = requestedPresentMode;
    }

    VkExtent2D extent{};
    if (capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
        extent = capabilities.currentExtent;
    } else {
        extent.width = std::clamp(desc.extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(desc.extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    u32 imageCount = std::max(desc.imageCount, capabilities.minImageCount);
    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    const VkSurfaceTransformFlagBitsKHR requestedTransform = toVkSurfaceTransform(desc.preTransform);
    const VkSurfaceTransformFlagBitsKHR selectedTransform =
        (capabilities.supportedTransforms & requestedTransform) != 0 ? requestedTransform : capabilities.currentTransform;
    const VkCompositeAlphaFlagBitsKHR requestedAlpha = toVkCompositeAlpha(desc.compositeAlpha);
    VkCompositeAlphaFlagBitsKHR selectedAlpha = requestedAlpha;
    if ((capabilities.supportedCompositeAlpha & selectedAlpha) == 0) {
        if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0) {
            selectedAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        } else if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0) {
            selectedAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        } else if ((capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0) {
            selectedAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        } else {
            selectedAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }
    }

    std::array<u32, 2> queueFamilies = {impl_->queueFamilies.graphics, impl_->queueFamilies.present};

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = impl_->native.surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = selectedFormat.format;
    swapchainInfo.imageColorSpace = selectedFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (impl_->queueFamilies.graphics != impl_->queueFamilies.present) {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapchainInfo.queueFamilyIndexCount = static_cast<u32>(queueFamilies.size());
        swapchainInfo.pQueueFamilyIndices = queueFamilies.data();
    } else {
        swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    swapchainInfo.preTransform = selectedTransform;
    swapchainInfo.compositeAlpha = selectedAlpha;
    swapchainInfo.presentMode = selectedPresentMode;
    swapchainInfo.clipped = VK_TRUE;

    Impl::SwapchainResource resource{};
    resource.desc = desc;
    resource.format = selectedFormat.format;
    resource.extent = extent;
    if (vkCreateSwapchainKHR(impl_->native.device, &swapchainInfo, nullptr, &resource.swapchain) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSwapchainKHR 失败");
    }

    u32 swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(impl_->native.device, resource.swapchain, &swapchainImageCount, nullptr);
    std::vector<VkImage> images(swapchainImageCount);
    vkGetSwapchainImagesKHR(impl_->native.device, resource.swapchain, &swapchainImageCount, images.data());

    for (u32 index = 0; index < swapchainImageCount; ++index) {
        Impl::TextureResource texture{};
        texture.ownsImage = false;
        texture.image = images[index];
        texture.currentState = ResourceState::Undefined;
        texture.desc.debugName = desc.debugName + ".Image" + std::to_string(index);
        texture.desc.dimension = TextureDimension::Texture2D;
        texture.desc.extent = {extent.width, extent.height, 1};
        texture.desc.arrayLayers = 1;
        texture.desc.mipLevels = 1;
        texture.desc.format = swapchainFormat;
        texture.desc.samples = SampleCount::Count1;
        texture.desc.usage = TextureUsage::ColorAttachment | TextureUsage::Present;
        texture.desc.initialState = ResourceState::Present;

        TextureHandle textureHandle = makeRenderHandle<TextureHandle>(impl_->textures, std::move(texture));
        resource.images.push_back(textureHandle);

        TextureViewDesc viewDesc{};
        viewDesc.debugName = desc.debugName + ".ImageView" + std::to_string(index);
        viewDesc.texture = textureHandle;
        viewDesc.dimension = TextureViewDimension::View2D;
        viewDesc.format = swapchainFormat;
        viewDesc.aspect = TextureAspect::Color;
        resource.imageViews.push_back(createTextureView(viewDesc));
    }

    const SwapchainHandle handle = makeRenderHandle<SwapchainHandle>(impl_->swapchains, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_SWAPCHAIN_KHR, reinterpret_cast<u64>(impl_->swapchains.back().swapchain), desc.debugName);
    return handle;
}

std::vector<TextureHandle> VulkanRenderer::getSwapchainImages(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->images;
    }
    return {};
}

std::vector<TextureViewHandle> VulkanRenderer::getSwapchainImageViews(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return swapchain->imageViews;
    }
    return {};
}

Format VulkanRenderer::getSwapchainFormat(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return fromVkFormat(swapchain->format);
    }
    return Format::Undefined;
}

Extent2D VulkanRenderer::getSwapchainExtent(SwapchainHandle handle) const {
    if (const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, handle)) {
        return {swapchain->extent.width, swapchain->extent.height};
    }
    return {};
}

bool VulkanRenderer::acquireNextImage(
    SwapchainHandle swapchainHandle,
    SemaphoreHandle signalSemaphore,
    FenceHandle signalFence,
    u32* imageIndex,
    std::string* errorMessage) {
    const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, swapchainHandle);
    const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, signalSemaphore);
    const Impl::FenceResource* fence = getRenderResource(impl_->fences, signalFence);
    if (swapchain == nullptr || swapchain->swapchain == VK_NULL_HANDLE || imageIndex == nullptr) {
        if (errorMessage != nullptr) *errorMessage = "acquireNextImage 参数无效";
        return false;
    }

    const VkResult result = vkAcquireNextImageKHR(
        impl_->native.device,
        swapchain->swapchain,
        std::numeric_limits<u64>::max(),
        semaphore != nullptr ? semaphore->semaphore : VK_NULL_HANDLE,
        fence != nullptr ? fence->fence : VK_NULL_HANDLE,
        imageIndex);

    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        if (errorMessage != nullptr) *errorMessage = "vkAcquireNextImageKHR 失败";
        return false;
    }
    return true;
}

bool VulkanRenderer::submit(const QueueSubmitDesc& desc, std::string* errorMessage) {
    std::vector<VkSemaphore> waitSemaphores;
    std::vector<VkPipelineStageFlags> waitStages;
    std::vector<VkSemaphore> signalSemaphores;
    std::vector<u64> waitValues;
    std::vector<u64> signalValues;
    bool usesTimelineSemaphore = false;

    for (const QueueWaitDesc& wait : desc.waits) {
        const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, wait.semaphore);
        if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
            if (errorMessage != nullptr) *errorMessage = "QueueSubmitDesc 包含无效 wait semaphore";
            return false;
        }
        usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == SemaphoreType::Timeline;
        waitSemaphores.push_back(semaphore->semaphore);
        waitStages.push_back(toVkPipelineStages(wait.stages));
        waitValues.push_back(wait.value);
    }

    for (const QueueSignalDesc& signal : desc.signals) {
        const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, signal.semaphore);
        if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
            if (errorMessage != nullptr) *errorMessage = "QueueSubmitDesc 包含无效 signal semaphore";
            return false;
        }
        usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == SemaphoreType::Timeline;
        signalSemaphores.push_back(semaphore->semaphore);
        signalValues.push_back(signal.value);
    }

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.waitSemaphoreValueCount = static_cast<u32>(waitValues.size());
    timelineInfo.pWaitSemaphoreValues = waitValues.data();
    timelineInfo.signalSemaphoreValueCount = static_cast<u32>(signalValues.size());
    timelineInfo.pSignalSemaphoreValues = signalValues.data();

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = usesTimelineSemaphore ? &timelineInfo : nullptr;
    submitInfo.waitSemaphoreCount = static_cast<u32>(waitSemaphores.size());
    submitInfo.pWaitSemaphores = waitSemaphores.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.signalSemaphoreCount = static_cast<u32>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    const Impl::FenceResource* fence = getRenderResource(impl_->fences, desc.fence);
    VkQueue queue = impl_->queueForType(desc.queue);
    if (queue == VK_NULL_HANDLE) {
        if (errorMessage != nullptr) *errorMessage = "QueueSubmitDesc 请求的队列类型当前设备不支持";
        return false;
    }

    const VkResult result = vkQueueSubmit(
        queue,
        1,
        &submitInfo,
        fence != nullptr ? fence->fence : VK_NULL_HANDLE);

    if (result != VK_SUCCESS) {
        if (errorMessage != nullptr) *errorMessage = "vkQueueSubmit 失败";
        return false;
    }
    return true;
}

bool VulkanRenderer::present(const PresentDesc& desc, std::string* errorMessage) {
    const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, desc.swapchain);
    if (swapchain == nullptr || swapchain->swapchain == VK_NULL_HANDLE) {
        if (errorMessage != nullptr) *errorMessage = "PresentDesc::swapchain 无效";
        return false;
    }

    std::vector<VkSemaphore> waitSemaphores;
    waitSemaphores.reserve(desc.waitSemaphores.size());
    for (SemaphoreHandle handle : desc.waitSemaphores) {
        const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, handle);
        if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
            if (errorMessage != nullptr) *errorMessage = "PresentDesc 包含无效 wait semaphore";
            return false;
        }
        waitSemaphores.push_back(semaphore->semaphore);
    }

    VkSwapchainKHR vkSwapchain = swapchain->swapchain;
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = static_cast<u32>(waitSemaphores.size());
    presentInfo.pWaitSemaphores = waitSemaphores.data();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &vkSwapchain;
    presentInfo.pImageIndices = &desc.imageIndex;

    const VkResult result = vkQueuePresentKHR(impl_->native.presentQueue, &presentInfo);
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        if (errorMessage != nullptr) *errorMessage = "vkQueuePresentKHR 失败";
        return false;
    }
    return true;
}

bool VulkanRenderer::recordAndSubmitFrame(const FramePacket& packet, std::string* errorMessage) {
    struct StagingResource {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence submitFence = VK_NULL_HANDLE;
    std::vector<StagingResource> stagingResources;

    const auto cleanup = [&]() noexcept {
        if (submitFence != VK_NULL_HANDLE) {
            vkDestroyFence(impl_->native.device, submitFence, nullptr);
        }
        if (commandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(impl_->native.device, impl_->graphicsCommandPool, 1, &commandBuffer);
        }
        for (const StagingResource& staging : stagingResources) {
            if (staging.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(impl_->native.device, staging.buffer, nullptr);
            }
            if (staging.memory != VK_NULL_HANDLE) {
                vkFreeMemory(impl_->native.device, staging.memory, nullptr);
            }
        }
    };

    try {
        if (!isInitialized() || impl_->graphicsCommandPool == VK_NULL_HANDLE) {
            throw std::runtime_error("VulkanRenderer 尚未初始化或缺少 graphics command pool");
        }

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = impl_->graphicsCommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(impl_->native.device, &allocateInfo, &commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateCommandBuffers 失败");
        }

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("vkBeginCommandBuffer 失败");
        }

        const auto bufferDstAccess = [](BufferUsage usage) {
            VkAccessFlags access = 0;
            if (hasAny(usage, BufferUsage::Vertex)) access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            if (hasAny(usage, BufferUsage::Index)) access |= VK_ACCESS_INDEX_READ_BIT;
            if (hasAny(usage, BufferUsage::Uniform)) access |= VK_ACCESS_UNIFORM_READ_BIT;
            if (hasAny(usage, BufferUsage::Storage)) access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            if (hasAny(usage, BufferUsage::Indirect)) access |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            return access == 0 ? VK_ACCESS_MEMORY_READ_BIT : access;
        };

        std::vector<VkBufferMemoryBarrier> uploadBarriers;
        for (const BufferUploadDesc& upload : packet.uploads.buffers) {
            if (upload.data.empty()) {
                continue;
            }

            Impl::BufferResource* destination = getRenderResource(impl_->buffers, upload.destination);
            if (destination == nullptr || destination->buffer == VK_NULL_HANDLE) {
                throw std::runtime_error("FramePacket uploads 包含无效 destination buffer");
            }

            StagingResource staging{};
            VkBufferCreateInfo stagingInfo{};
            stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingInfo.size = static_cast<VkDeviceSize>(upload.data.size());
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(impl_->native.device, &stagingInfo, nullptr, &staging.buffer) != VK_SUCCESS) {
                throw std::runtime_error("vkCreateBuffer(staging) 失败");
            }

            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(impl_->native.device, staging.buffer, &requirements);
            VkMemoryAllocateInfo memoryInfo{};
            memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memoryInfo.allocationSize = requirements.size;
            memoryInfo.memoryTypeIndex = impl_->findMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (vkAllocateMemory(impl_->native.device, &memoryInfo, nullptr, &staging.memory) != VK_SUCCESS) {
                throw std::runtime_error("vkAllocateMemory(staging) 失败");
            }
            vkBindBufferMemory(impl_->native.device, staging.buffer, staging.memory, 0);

            void* mapped = nullptr;
            vkMapMemory(impl_->native.device, staging.memory, 0, stagingInfo.size, 0, &mapped);
            std::memcpy(mapped, upload.data.data(), upload.data.size());
            vkUnmapMemory(impl_->native.device, staging.memory);

            VkBufferCopy copy{};
            copy.srcOffset = 0;
            copy.dstOffset = static_cast<VkDeviceSize>(upload.destinationOffset);
            copy.size = static_cast<VkDeviceSize>(upload.data.size());
            vkCmdCopyBuffer(commandBuffer, staging.buffer, destination->buffer, 1, &copy);
            stagingResources.push_back(staging);

            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = bufferDstAccess(destination->desc.usage);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = destination->buffer;
            barrier.offset = upload.destinationOffset;
            barrier.size = upload.data.size();
            uploadBarriers.push_back(barrier);
        }

        if (!uploadBarriers.empty()) {
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0,
                0,
                nullptr,
                static_cast<u32>(uploadBarriers.size()),
                uploadBarriers.data(),
                0,
                nullptr);
        }

        std::unordered_map<std::string, TextureHandle> textureResources;
        textureResources.reserve(packet.graph.textures.size());
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
                if (view.view != VK_NULL_HANDLE && view.desc.texture == texture &&
                    (aspect == TextureAspect::All || view.desc.aspect == aspect || view.desc.aspect == TextureAspect::All)) {
                    return TextureViewHandle(index + 1);
                }
            }
            return {};
        };

        const auto transitionTexture = [&](TextureHandle handle, ResourceState after) {
            Impl::TextureResource* texture = getRenderResource(impl_->textures, handle);
            if (texture == nullptr || texture->image == VK_NULL_HANDLE || texture->currentState == after) {
                return;
            }

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = accessFromResourceState(texture->currentState);
            barrier.dstAccessMask = accessFromResourceState(after);
            barrier.oldLayout = toVkImageLayout(texture->currentState);
            barrier.newLayout = toVkImageLayout(after);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture->image;
            barrier.subresourceRange.aspectMask = toVkImageAspect(TextureAspect::All, texture->desc.format);
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = texture->desc.mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = texture->desc.arrayLayers;

            const VkPipelineStageFlags sourceStage =
                texture->currentState == ResourceState::Undefined ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            vkCmdPipelineBarrier(
                commandBuffer,
                sourceStage,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);
            texture->currentState = after;
        };

        const auto findWorkload = [&](const std::string& passName) -> const RenderPassWorkload* {
            const auto it = std::find_if(packet.workloads.begin(), packet.workloads.end(), [&](const RenderPassWorkload& workload) {
                return workload.passName == passName;
            });
            return it == packet.workloads.end() ? nullptr : &*it;
        };

        const auto vkClearColor = [](const ClearColor& color) {
            VkClearValue value{};
            value.color.float32[0] = color.r;
            value.color.float32[1] = color.g;
            value.color.float32[2] = color.b;
            value.color.float32[3] = color.a;
            return value;
        };

        const auto vkClearDepthStencil = [](const ClearDepthStencil& clear) {
            VkClearValue value{};
            value.depthStencil.depth = clear.depth;
            value.depthStencil.stencil = clear.stencil;
            return value;
        };

        for (const RenderGraphPassDesc& pass : packet.graph.passes) {
            for (const RenderGraphResourceRef& read : pass.reads) {
                if (read.type == RenderGraphResourceType::Texture || read.type == RenderGraphResourceType::SwapchainImage) {
                    transitionTexture(textureForName(read.name), read.state);
                }
            }
            for (const RenderGraphResourceRef& write : pass.writes) {
                if (write.type == RenderGraphResourceType::Texture || write.type == RenderGraphResourceType::SwapchainImage) {
                    transitionTexture(textureForName(write.name), write.state);
                }
            }

            const RenderPassWorkload* workload = findWorkload(pass.name);
            if (workload == nullptr || (pass.colorAttachments.empty() && !pass.depthStencilAttachment.has_value())) {
                continue;
            }

            std::vector<VkRenderingAttachmentInfo> colorAttachments;
            std::vector<VkClearValue> colorClearValues;
            colorAttachments.reserve(pass.colorAttachments.size());
            colorClearValues.reserve(pass.colorAttachments.size());
            for (const RenderGraphAttachmentDesc& attachment : pass.colorAttachments) {
                const TextureHandle texture = textureForName(attachment.resourceName);
                const TextureViewHandle viewHandle = findViewForTexture(texture, TextureAspect::Color);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || view->view == VK_NULL_HANDLE) {
                    throw std::runtime_error("RenderGraph color attachment 缺少有效 texture view");
                }

                colorClearValues.push_back(vkClearColor(attachment.clearValue.color));
                VkRenderingAttachmentInfo colorAttachment{};
                colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                colorAttachment.imageView = view->view;
                colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttachment.loadOp = toVkLoadOp(attachment.loadOp);
                colorAttachment.storeOp = toVkStoreOp(attachment.storeOp);
                colorAttachment.clearValue = colorClearValues.back();
                colorAttachments.push_back(colorAttachment);
            }

            VkRenderingAttachmentInfo depthAttachment{};
            VkClearValue depthClear{};
            if (pass.depthStencilAttachment.has_value()) {
                const RenderGraphAttachmentDesc& attachment = *pass.depthStencilAttachment;
                const TextureHandle texture = textureForName(attachment.resourceName);
                const TextureViewHandle viewHandle = findViewForTexture(texture, TextureAspect::Depth);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || view->view == VK_NULL_HANDLE) {
                    throw std::runtime_error("RenderGraph depth attachment 缺少有效 texture view");
                }

                depthClear = vkClearDepthStencil(attachment.clearValue.depthStencil);
                depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAttachment.imageView = view->view;
                depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthAttachment.loadOp = toVkLoadOp(attachment.loadOp);
                depthAttachment.storeOp = toVkStoreOp(attachment.storeOp);
                depthAttachment.clearValue = depthClear;
            }

            Rect2D renderArea = workload->scissor.extent.width == 0 || workload->scissor.extent.height == 0
                ? packet.settings.scissor
                : workload->scissor;
            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = {renderArea.offset.x, renderArea.offset.y};
            renderingInfo.renderArea.extent = {renderArea.extent.width, renderArea.extent.height};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = static_cast<u32>(colorAttachments.size());
            renderingInfo.pColorAttachments = colorAttachments.data();
            renderingInfo.pDepthAttachment = pass.depthStencilAttachment.has_value() ? &depthAttachment : nullptr;

            vkCmdBeginRendering(commandBuffer, &renderingInfo);

            Viewport viewport = workload->viewport.width == 0.0F || workload->viewport.height == 0.0F
                ? packet.settings.viewport
                : workload->viewport;
            VkViewport vkViewport{};
            vkViewport.x = viewport.x;
            vkViewport.y = viewport.y;
            vkViewport.width = viewport.width;
            vkViewport.height = viewport.height;
            vkViewport.minDepth = viewport.minDepth;
            vkViewport.maxDepth = viewport.maxDepth;
            vkCmdSetViewport(commandBuffer, 0, 1, &vkViewport);

            VkRect2D vkScissor{};
            vkScissor.offset = {renderArea.offset.x, renderArea.offset.y};
            vkScissor.extent = {renderArea.extent.width, renderArea.extent.height};
            vkCmdSetScissor(commandBuffer, 0, 1, &vkScissor);

            const auto recordDraw = [&](const DrawIndexedCommand& draw) {
                const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, draw.pipeline);
                if (pipeline == nullptr || pipeline->pipeline == VK_NULL_HANDLE) {
                    throw std::runtime_error("DrawIndexedCommand pipeline 无效");
                }
                vkCmdBindPipeline(commandBuffer, pipeline->bindPoint, pipeline->pipeline);

                std::vector<VkDescriptorSet> descriptorSets;
                descriptorSets.reserve(draw.bindGroups.size());
                for (BindGroupHandle bindGroupHandle : draw.bindGroups) {
                    const Impl::BindGroupResource* bindGroup = getRenderResource(impl_->bindGroups, bindGroupHandle);
                    if (bindGroup == nullptr || bindGroup->set == VK_NULL_HANDLE) {
                        throw std::runtime_error("DrawIndexedCommand bind group 无效");
                    }
                    descriptorSets.push_back(bindGroup->set);
                }
                if (!descriptorSets.empty()) {
                    vkCmdBindDescriptorSets(
                        commandBuffer,
                        pipeline->bindPoint,
                        pipeline->layout,
                        0,
                        static_cast<u32>(descriptorSets.size()),
                        descriptorSets.data(),
                        0,
                        nullptr);
                }

                for (const VertexStream& stream : draw.vertexStreams) {
                    const Impl::BufferResource* vertexBuffer = getRenderResource(impl_->buffers, stream.buffer);
                    if (vertexBuffer == nullptr || vertexBuffer->buffer == VK_NULL_HANDLE) {
                        throw std::runtime_error("DrawIndexedCommand vertex buffer 无效");
                    }
                    VkBuffer buffer = vertexBuffer->buffer;
                    VkDeviceSize offset = static_cast<VkDeviceSize>(stream.offset);
                    vkCmdBindVertexBuffers(commandBuffer, stream.binding, 1, &buffer, &offset);
                }

                const Impl::BufferResource* indexBuffer = getRenderResource(impl_->buffers, draw.indexStream.buffer);
                if (indexBuffer == nullptr || indexBuffer->buffer == VK_NULL_HANDLE) {
                    throw std::runtime_error("DrawIndexedCommand index buffer 无效");
                }
                const VkIndexType indexType = draw.indexStream.indexType == IndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer->buffer, static_cast<VkDeviceSize>(draw.indexStream.offset), indexType);
                vkCmdDrawIndexed(
                    commandBuffer,
                    draw.indexCount,
                    draw.instanceCount,
                    draw.firstIndex,
                    draw.vertexOffsetElements,
                    draw.firstInstance);
            };

            for (const DrawIndexedCommand& draw : workload->indexedDraws) {
                recordDraw(draw);
            }

            vkCmdEndRendering(commandBuffer);
        }

        if (packet.present.has_value()) {
            const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, packet.present->swapchain);
            if (swapchain != nullptr && packet.present->imageIndex < swapchain->images.size()) {
                transitionTexture(swapchain->images[packet.present->imageIndex], ResourceState::Present);
            }
        }

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("vkEndCommandBuffer 失败");
        }

        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> waitStages;
        std::vector<VkSemaphore> signalSemaphores;
        std::vector<u64> waitValues;
        std::vector<u64> signalValues;
        bool usesTimelineSemaphore = false;

        for (const QueueSubmitDesc& submitDesc : packet.submissions) {
            for (const QueueWaitDesc& wait : submitDesc.waits) {
                const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, wait.semaphore);
                if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
                    throw std::runtime_error("FramePacket submission 包含无效 wait semaphore");
                }
                usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == SemaphoreType::Timeline;
                waitSemaphores.push_back(semaphore->semaphore);
                waitStages.push_back(toVkPipelineStages(wait.stages));
                waitValues.push_back(wait.value);
            }
            for (const QueueSignalDesc& signal : submitDesc.signals) {
                const Impl::SemaphoreResource* semaphore = getRenderResource(impl_->semaphores, signal.semaphore);
                if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
                    throw std::runtime_error("FramePacket submission 包含无效 signal semaphore");
                }
                usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == SemaphoreType::Timeline;
                signalSemaphores.push_back(semaphore->semaphore);
                signalValues.push_back(signal.value);
            }
        }

        VkTimelineSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.waitSemaphoreValueCount = static_cast<u32>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<u32>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.pNext = usesTimelineSemaphore ? &timelineInfo : nullptr;
        submitInfo.waitSemaphoreCount = static_cast<u32>(waitSemaphores.size());
        submitInfo.pWaitSemaphores = waitSemaphores.data();
        submitInfo.pWaitDstStageMask = waitStages.data();
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        submitInfo.signalSemaphoreCount = static_cast<u32>(signalSemaphores.size());
        submitInfo.pSignalSemaphores = signalSemaphores.data();

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(impl_->native.device, &fenceInfo, nullptr, &submitFence) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateFence(submit) 失败");
        }

        if (vkQueueSubmit(impl_->native.graphicsQueue, 1, &submitInfo, submitFence) != VK_SUCCESS) {
            throw std::runtime_error("vkQueueSubmit(recorded frame) 失败");
        }
        vkWaitForFences(impl_->native.device, 1, &submitFence, VK_TRUE, std::numeric_limits<u64>::max());

        cleanup();
        commandBuffer = VK_NULL_HANDLE;
        submitFence = VK_NULL_HANDLE;
        stagingResources.clear();

        if (packet.present.has_value()) {
            return present(*packet.present, errorMessage);
        }
        return true;
    } catch (const std::exception& error) {
        cleanup();
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool VulkanRenderer::submitFrame(const FramePacket& packet, std::string* errorMessage) {
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

void VulkanRenderer::waitIdle() const noexcept {
    if (isInitialized()) {
        vkDeviceWaitIdle(impl_->native.device);
    }
}

void VulkanRenderer::destroy(BufferHandle handle) noexcept {
    Impl::BufferResource* resource = getRenderResource(impl_->buffers, handle);
    if (resource == nullptr) return;
    if (resource->mapped != nullptr) {
        vkUnmapMemory(impl_->native.device, resource->memory);
        resource->mapped = nullptr;
    }
    if (resource->buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(impl_->native.device, resource->buffer, nullptr);
        resource->buffer = VK_NULL_HANDLE;
    }
    if (resource->memory != VK_NULL_HANDLE) {
        vkFreeMemory(impl_->native.device, resource->memory, nullptr);
        resource->memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(TextureHandle handle) noexcept {
    Impl::TextureResource* resource = getRenderResource(impl_->textures, handle);
    if (resource == nullptr) return;
    if (resource->image != VK_NULL_HANDLE && resource->ownsImage) {
        vkDestroyImage(impl_->native.device, resource->image, nullptr);
    }
    resource->image = VK_NULL_HANDLE;
    if (resource->memory != VK_NULL_HANDLE) {
        vkFreeMemory(impl_->native.device, resource->memory, nullptr);
        resource->memory = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(TextureViewHandle handle) noexcept {
    Impl::TextureViewResource* resource = getRenderResource(impl_->textureViews, handle);
    if (resource != nullptr && resource->view != VK_NULL_HANDLE) {
        vkDestroyImageView(impl_->native.device, resource->view, nullptr);
        resource->view = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(SamplerHandle handle) noexcept {
    Impl::SamplerResource* resource = getRenderResource(impl_->samplers, handle);
    if (resource != nullptr && resource->sampler != VK_NULL_HANDLE) {
        vkDestroySampler(impl_->native.device, resource->sampler, nullptr);
        resource->sampler = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(ShaderHandle handle) noexcept {
    Impl::ShaderResource* resource = getRenderResource(impl_->shaders, handle);
    if (resource != nullptr && resource->module != VK_NULL_HANDLE) {
        vkDestroyShaderModule(impl_->native.device, resource->module, nullptr);
        resource->module = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(BindGroupLayoutHandle handle) noexcept {
    Impl::BindGroupLayoutResource* resource = getRenderResource(impl_->bindGroupLayouts, handle);
    if (resource != nullptr && resource->layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(impl_->native.device, resource->layout, nullptr);
        resource->layout = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(BindGroupHandle handle) noexcept {
    Impl::BindGroupResource* resource = getRenderResource(impl_->bindGroups, handle);
    if (resource != nullptr && resource->set != VK_NULL_HANDLE) {
        vkFreeDescriptorSets(impl_->native.device, impl_->descriptorPool, 1, &resource->set);
        resource->set = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(PipelineLayoutHandle handle) noexcept {
    Impl::PipelineLayoutResource* resource = getRenderResource(impl_->pipelineLayouts, handle);
    if (resource != nullptr && resource->layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(impl_->native.device, resource->layout, nullptr);
        resource->layout = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(PipelineCacheHandle handle) noexcept {
    Impl::PipelineCacheResource* resource = getRenderResource(impl_->pipelineCaches, handle);
    if (resource != nullptr && resource->cache != VK_NULL_HANDLE) {
        vkDestroyPipelineCache(impl_->native.device, resource->cache, nullptr);
        resource->cache = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(PipelineHandle handle) noexcept {
    Impl::PipelineResource* resource = getRenderResource(impl_->pipelines, handle);
    if (resource != nullptr && resource->pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(impl_->native.device, resource->pipeline, nullptr);
        resource->pipeline = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(QueryPoolHandle handle) noexcept {
    Impl::QueryPoolResource* resource = getRenderResource(impl_->queryPools, handle);
    if (resource != nullptr && resource->pool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(impl_->native.device, resource->pool, nullptr);
        resource->pool = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(SemaphoreHandle handle) noexcept {
    Impl::SemaphoreResource* resource = getRenderResource(impl_->semaphores, handle);
    if (resource != nullptr && resource->semaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(impl_->native.device, resource->semaphore, nullptr);
        resource->semaphore = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(FenceHandle handle) noexcept {
    Impl::FenceResource* resource = getRenderResource(impl_->fences, handle);
    if (resource != nullptr && resource->fence != VK_NULL_HANDLE) {
        vkDestroyFence(impl_->native.device, resource->fence, nullptr);
        resource->fence = VK_NULL_HANDLE;
    }
}

void VulkanRenderer::destroy(SwapchainHandle handle) noexcept {
    Impl::SwapchainResource* resource = getRenderResource(impl_->swapchains, handle);
    if (resource == nullptr) return;

    for (TextureViewHandle view : resource->imageViews) {
        destroy(view);
    }
    resource->imageViews.clear();

    for (TextureHandle image : resource->images) {
        destroy(image);
    }
    resource->images.clear();

    if (resource->swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(impl_->native.device, resource->swapchain, nullptr);
        resource->swapchain = VK_NULL_HANDLE;
    }
}
