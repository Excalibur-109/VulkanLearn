// 学习导读：
// 这个文件是统一渲染抽象到 Vulkan 的落地层。RenderDefinitions.hpp 里的 BufferDesc、
// TextureDesc、PipelineDesc、FramePacket 等结构只描述“引擎想要什么”；这里负责把它们
// 翻译成 VkBuffer/VkImage/VkDescriptorSet/VkPipeline/VkCommandBuffer 等 Vulkan 对象。
//
// Vulkan 是显式 API：资源内存、资源状态、同步和命令录制都要由引擎明确处理。因此读这个
// 文件时可以按“创建设备 -> 创建资源 -> 创建绑定/管线 -> 录制命令 -> 提交/呈现 -> 销毁”
// 的顺序理解，而不是把它看成一组互不相关的函数。

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

// 统一 Format 到 VkFormat 的映射层。引擎其它模块只依赖 RenderDefinitions.hpp 中的
// Format 枚举，后端在边界处一次性转换成 native enum，这样上层不会散落 Vulkan 类型。
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
    if (hasAny(usage, BufferUsage::TransferSource))       flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (hasAny(usage, BufferUsage::TransferDestination))  flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (hasAny(usage, BufferUsage::Vertex))               flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::Index))                flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::Uniform))              flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::Storage))              flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::Indirect))             flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (hasAny(usage, BufferUsage::ShaderDeviceAddress))  flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    return flags == 0 ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : flags;
}

static VkImageUsageFlags toVkImageUsage(TextureUsage usage) {
    VkImageUsageFlags flags = 0;
    if (hasAny(usage, TextureUsage::TransferSource))           flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (hasAny(usage, TextureUsage::TransferDestination))      flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (hasAny(usage, TextureUsage::Sampled))                  flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (hasAny(usage, TextureUsage::Storage))                  flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (hasAny(usage, TextureUsage::ColorAttachment))          flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (hasAny(usage, TextureUsage::DepthStencilAttachment))   flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (hasAny(usage, TextureUsage::Transient))                flags |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    if (hasAny(usage, TextureUsage::Present))                  flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    return flags == 0 ? VK_IMAGE_USAGE_SAMPLED_BIT : flags;
}

static VkImageCreateFlags toVkImageCreateFlags(TextureCreateFlags flags) {
    VkImageCreateFlags vkFlags = 0;
    if (hasAny(flags, TextureCreateFlags::CubeCompatible)) vkFlags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
    if (hasAny(flags, TextureCreateFlags::MutableFormat))  vkFlags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
    if (hasAny(flags, TextureCreateFlags::SparseBinding))  vkFlags |= VK_IMAGE_CREATE_SPARSE_BINDING_BIT;
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
        if (isDepthFormat(format))    inferred |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilFormat(format)) inferred |= VK_IMAGE_ASPECT_STENCIL_BIT;
        return inferred == 0 ? VK_IMAGE_ASPECT_COLOR_BIT : inferred;
    }

    VkImageAspectFlags flags = 0;
    if (hasAny(aspect, TextureAspect::Color))   flags |= VK_IMAGE_ASPECT_COLOR_BIT;
    if (hasAny(aspect, TextureAspect::Depth))   flags |= VK_IMAGE_ASPECT_DEPTH_BIT;
    if (hasAny(aspect, TextureAspect::Stencil)) flags |= VK_IMAGE_ASPECT_STENCIL_BIT;
    if (hasAny(aspect, TextureAspect::Plane0))  flags |= VK_IMAGE_ASPECT_PLANE_0_BIT;
    if (hasAny(aspect, TextureAspect::Plane1))  flags |= VK_IMAGE_ASPECT_PLANE_1_BIT;
    if (hasAny(aspect, TextureAspect::Plane2))  flags |= VK_IMAGE_ASPECT_PLANE_2_BIT;
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
    if (hasAny(stages, ShaderStage::Vertex))         flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (hasAny(stages, ShaderStage::TessControl))    flags |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
    if (hasAny(stages, ShaderStage::TessEvaluation)) flags |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
    if (hasAny(stages, ShaderStage::Geometry))       flags |= VK_SHADER_STAGE_GEOMETRY_BIT;
    if (hasAny(stages, ShaderStage::Fragment))       flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (hasAny(stages, ShaderStage::Compute))        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (hasAny(stages, ShaderStage::Task))           flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
    if (hasAny(stages, ShaderStage::Mesh))           flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
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
    if (hasAny(stages, PipelineStage::TopOfPipe))             flags |= VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    if (hasAny(stages, PipelineStage::DrawIndirect))          flags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
    if (hasAny(stages, PipelineStage::VertexInput))           flags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
    if (hasAny(stages, PipelineStage::VertexShader))          flags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    if (hasAny(stages, PipelineStage::TessControlShader))     flags |= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT;
    if (hasAny(stages, PipelineStage::TessEvaluationShader))  flags |= VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
    if (hasAny(stages, PipelineStage::GeometryShader))        flags |= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
    if (hasAny(stages, PipelineStage::FragmentShader))        flags |= VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (hasAny(stages, PipelineStage::EarlyFragmentTests))    flags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    if (hasAny(stages, PipelineStage::LateFragmentTests))     flags |= VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    if (hasAny(stages, PipelineStage::ColorAttachmentOutput)) flags |= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (hasAny(stages, PipelineStage::ComputeShader))         flags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    if (hasAny(stages, PipelineStage::Transfer))              flags |= VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (hasAny(stages, PipelineStage::BottomOfPipe))          flags |= VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    if (hasAny(stages, PipelineStage::Host))                  flags |= VK_PIPELINE_STAGE_HOST_BIT;
    if (hasAny(stages, PipelineStage::AllGraphics))           flags |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
    if (hasAny(stages, PipelineStage::AllCommands))           flags |= VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    return flags == 0 ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : flags;
}

[[maybe_unused]] static VkAccessFlags toVkAccessFlags(AccessFlags access) {
    VkAccessFlags flags = 0;
    if (hasAny(access, AccessFlags::IndirectCommandRead))  flags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    if (hasAny(access, AccessFlags::IndexRead))            flags |= VK_ACCESS_INDEX_READ_BIT;
    if (hasAny(access, AccessFlags::VertexAttributeRead))  flags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    if (hasAny(access, AccessFlags::UniformRead))          flags |= VK_ACCESS_UNIFORM_READ_BIT;
    if (hasAny(access, AccessFlags::InputAttachmentRead))  flags |= VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
    if (hasAny(access, AccessFlags::ShaderRead))           flags |= VK_ACCESS_SHADER_READ_BIT;
    if (hasAny(access, AccessFlags::ShaderWrite))          flags |= VK_ACCESS_SHADER_WRITE_BIT;
    if (hasAny(access, AccessFlags::ColorAttachmentRead))  flags |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    if (hasAny(access, AccessFlags::ColorAttachmentWrite)) flags |= VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    if (hasAny(access, AccessFlags::DepthStencilRead))     flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    if (hasAny(access, AccessFlags::DepthStencilWrite))    flags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    if (hasAny(access, AccessFlags::TransferRead))         flags |= VK_ACCESS_TRANSFER_READ_BIT;
    if (hasAny(access, AccessFlags::TransferWrite))        flags |= VK_ACCESS_TRANSFER_WRITE_BIT;
    if (hasAny(access, AccessFlags::HostRead))             flags |= VK_ACCESS_HOST_READ_BIT;
    if (hasAny(access, AccessFlags::HostWrite))            flags |= VK_ACCESS_HOST_WRITE_BIT;
    if (hasAny(access, AccessFlags::MemoryRead))           flags |= VK_ACCESS_MEMORY_READ_BIT;
    if (hasAny(access, AccessFlags::MemoryWrite))          flags |= VK_ACCESS_MEMORY_WRITE_BIT;
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

// Impl 是 VulkanRenderer 的后端状态仓库。
// 公共 API 对外只暴露轻量 Handle，Handle 实际上是这些 vector 的 1-based 索引；真实的
// Vulkan 对象、创建时的描述信息、是否拥有对象生命周期等都集中保存在这里。这样上层可以
// 用统一句柄表达依赖关系，后端仍能拿到 native 对象执行命令。
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

/// 参考 Vulkan-Tutorial simple_engine 的 SwapChainSupportDetails：
/// surface 能力必须集中查询，物理设备筛选和真正 createSwapchain 时都应该用同一套结果语义。
struct VulkanSwapchainSupport {
    VkSurfaceCapabilitiesKHR        capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
    VkResult                        capabilitiesResult = VK_ERROR_INITIALIZATION_FAILED;
    VkResult                        formatsResult      = VK_ERROR_INITIALIZATION_FAILED;
    VkResult                        presentModesResult = VK_ERROR_INITIALIZATION_FAILED;

    bool isUsable() const {
        return capabilitiesResult == VK_SUCCESS && formatsResult == VK_SUCCESS && presentModesResult == VK_SUCCESS &&
               !formats.empty() && !presentModes.empty();
    }
};

static VulkanSwapchainSupport querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    VulkanSwapchainSupport support{};
    if (surface == VK_NULL_HANDLE) {
        return support;
    }

    // WSI 查询函数也会返回 VkResult。驱动、surface 生命周期或窗口系统异常时，
    // 不能把“空数组”误认为正常能力；设备筛选和 swapchain 创建会统一拒绝失败结果。
    support.capabilitiesResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &support.capabilities);
    if (support.capabilitiesResult != VK_SUCCESS) {
        return support;
    }

    u32 formatCount = 0;
    support.formatsResult = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (support.formatsResult != VK_SUCCESS) {
        return support;
    }
    support.formats.resize(formatCount);
    if (formatCount != 0) {
        support.formatsResult = vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, support.formats.data());
        if (support.formatsResult != VK_SUCCESS) {
            support.formats.clear();
            return support;
        }
    }

    u32 presentModeCount = 0;
    support.presentModesResult = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (support.presentModesResult != VK_SUCCESS) {
        return support;
    }
    support.presentModes.resize(presentModeCount);
    if (presentModeCount != 0) {
        support.presentModesResult = vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, support.presentModes.data());
        if (support.presentModesResult != VK_SUCCESS) {
            support.presentModes.clear();
        }
    }

    return support;
}

static VkSurfaceFormatKHR chooseSwapchainFormat(const VulkanSwapchainSupport& support, const SwapchainDesc& desc) {
    const VkFormat        requestedFormat     = toVkFormat(desc.preferredFormat);
    const VkColorSpaceKHR requestedColorSpace = toVkColorSpace(desc.colorSpace);

    if (support.formats.size() == 1 && support.formats[0].format == VK_FORMAT_UNDEFINED) {
        return {
            requestedFormat == VK_FORMAT_UNDEFINED ? VK_FORMAT_B8G8R8A8_SRGB : requestedFormat,
            requestedColorSpace
        };
    }

    for (const VkSurfaceFormatKHR& format : support.formats) {
        if (format.format == requestedFormat && format.colorSpace == requestedColorSpace) {
            return format;
        }
    }

    return support.formats.empty()
        ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR}
        : support.formats[0];
}

static VkPresentModeKHR chooseSwapchainPresentMode(const VulkanSwapchainSupport& support, PresentMode requestedMode) {
    const VkPresentModeKHR requestedPresentMode = toVkPresentMode(requestedMode);
    if (std::find(support.presentModes.begin(), support.presentModes.end(), requestedPresentMode) != support.presentModes.end()) {
        return requestedPresentMode;
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseSwapchainExtent(const VulkanSwapchainSupport& support, Extent2D requestedExtent) {
    if (support.capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
        return support.capabilities.currentExtent;
    }

    return {
        std::clamp(requestedExtent.width, support.capabilities.minImageExtent.width, support.capabilities.maxImageExtent.width),
        std::clamp(requestedExtent.height, support.capabilities.minImageExtent.height, support.capabilities.maxImageExtent.height)
    };
}

static u32 chooseSwapchainImageCount(const VulkanSwapchainSupport& support, u32 requestedImageCount) {
    u32 imageCount = std::max(requestedImageCount, support.capabilities.minImageCount);
    if (support.capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, support.capabilities.maxImageCount);
    }
    return imageCount;
}

static VkSurfaceTransformFlagBitsKHR chooseSwapchainTransform(const VulkanSwapchainSupport& support, SurfaceTransform requestedTransform) {
    const VkSurfaceTransformFlagBitsKHR transform = toVkSurfaceTransform(requestedTransform);
    return (support.capabilities.supportedTransforms & transform) != 0 ? transform : support.capabilities.currentTransform;
}

static VkCompositeAlphaFlagBitsKHR chooseSwapchainCompositeAlpha(const VulkanSwapchainSupport& support, CompositeAlphaMode requestedMode) {
    const VkCompositeAlphaFlagBitsKHR requestedAlpha = toVkCompositeAlpha(requestedMode);
    if ((support.capabilities.supportedCompositeAlpha & requestedAlpha) != 0) {
        return requestedAlpha;
    }
    if ((support.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }
    if ((support.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    }
    if ((support.capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) != 0) {
        return VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }
    return VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
}

// Vulkan 的队列能力挂在 queue family 上，不是每个 VkQueue 都天然支持所有操作。
// 这里优先找图形队列，再尽量找独立 compute/transfer 队列；如果找不到独立队列，就退回到
// graphics family，保证 QueueType::Transfer 等抽象至少有可用队列。
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
            const VkResult presentResult = vkGetPhysicalDeviceSurfaceSupportKHR(device, index, surface, &presentSupported);
            if (presentResult == VK_SUCCESS && presentSupported == VK_TRUE && result.present == INVALID_INDEX) {
                result.present = index;
            }
        }
    }

    if (result.transfer == INVALID_INDEX) {
        result.transfer = result.graphics;
    }
    if (surface == VK_NULL_HANDLE && result.present == INVALID_INDEX) {
        result.present = result.graphics;
    }
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

// requiredFeatures 是用户/引擎声明“没有就不能启动”的能力集合。
// 这里不要只看 Vulkan 是否暴露 feature，还要看本后端是否真的实现了对应资源模型和命令路径；
// 例如硬件可能支持光追，但当前 renderer 没有 AS、SBT、ray pipeline，所以仍要返回 false。
static bool supportsRequiredRenderFeatures(const VulkanDeviceSupport& support, const VulkanQueueFamilies& queues, RenderFeature required) {
    if (hasAny(required, RenderFeature::Compute)                 && queues.compute == INVALID_INDEX)                                        return false;
    if (hasAny(required, RenderFeature::SamplerAnisotropy)       && support.features.samplerAnisotropy != VK_TRUE)                          return false;
    if (hasAny(required, RenderFeature::GeometryShader)          && support.features.geometryShader != VK_TRUE)                             return false;
    if (hasAny(required, RenderFeature::Tessellation)            && support.features.tessellationShader != VK_TRUE)                         return false;
    if (hasAny(required, RenderFeature::TimestampQuery)          && support.properties.limits.timestampComputeAndGraphics != VK_TRUE)       return false;
    if (hasAny(required, RenderFeature::OcclusionQuery)          && support.features.occlusionQueryPrecise != VK_TRUE)                      return false;
    if (hasAny(required, RenderFeature::PipelineStatisticsQuery) && support.features.pipelineStatisticsQuery != VK_TRUE)                    return false;
    if (hasAny(required, RenderFeature::DrawIndirectCount)       && support.features12.drawIndirectCount != VK_TRUE)                        return false;
    if (hasAny(required, RenderFeature::DynamicRendering)        && support.features13.dynamicRendering != VK_TRUE)                         return false;
    if (hasAny(required, RenderFeature::TextureCompressionBC)    && support.features.textureCompressionBC != VK_TRUE)                       return false;
    if (hasAny(required, RenderFeature::TextureCompressionETC2)  && support.features.textureCompressionETC2 != VK_TRUE)                     return false;
    if (hasAny(required, RenderFeature::TextureCompressionASTC)  && support.features.textureCompressionASTC_LDR != VK_TRUE)                 return false;

    // 这些功能需要额外扩展、feature chain 或资源模型；当前后端尚未实现，不能声明为可用。
    const RenderFeature unsupportedByThisBackend =
        RenderFeature::MeshShader                |
        RenderFeature::RayTracing                |
        RenderFeature::Bindless                  |
        RenderFeature::ConservativeRasterization |
        RenderFeature::Multiview;
    return !hasAny(required, unsupportedByThisBackend);
}

// 物理设备打分用于在多 GPU 机器上选择默认设备。
// - 不满足队列、surface、扩展、required feature 的设备直接淘汰；
// - 离散 GPU 默认更高分；
// - LowPower 模式下集成 GPU 会被偏好。
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
    if (surface != VK_NULL_HANDLE && !querySwapchainSupport(device, surface).isUsable()) {
        return -1;
    }

    int score = 0;
    if (support.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)   score += 1000;
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
