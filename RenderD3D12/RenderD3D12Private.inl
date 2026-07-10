using Microsoft::WRL::ComPtr;

// D3D12 资源句柄沿用现有后端的 1-based index 规则：0 是无效句柄，真实资源从 1 开始。
// 这样公共层 Handle<T> 不需要知道 ID3D12Resource*，也不会把不同资源类型误传。
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

// 统一 Format 到 DXGI_FORMAT。D3D11/D3D12 同属 DXGI 格式体系，所以大部分映射一致。
// 这里保留对齐后的 return，方便横向检查缺失格式。
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

// D3D12 的 resource state 是显式同步的核心。RenderDefinitions.hpp 的 ResourceState 是“意图”，
// 后端在 barrier/创建资源时把它翻译成 D3D12_RESOURCE_STATES。
static D3D12_RESOURCE_STATES toD3D12ResourceStates(ResourceState state) {
    switch (state) {
    case ResourceState::Undefined:                  return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::Common:                     return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::CopySource:                 return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case ResourceState::CopyDestination:            return D3D12_RESOURCE_STATE_COPY_DEST;
    case ResourceState::VertexBuffer:               return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::IndexBuffer:                return D3D12_RESOURCE_STATE_INDEX_BUFFER;
    case ResourceState::ConstantBuffer:             return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    case ResourceState::ShaderRead:                 return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case ResourceState::ShaderWrite:                return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case ResourceState::RenderTarget:               return D3D12_RESOURCE_STATE_RENDER_TARGET;
    case ResourceState::DepthRead:                  return D3D12_RESOURCE_STATE_DEPTH_READ;
    case ResourceState::DepthWrite:                 return D3D12_RESOURCE_STATE_DEPTH_WRITE;
    case ResourceState::ResolveSource:              return D3D12_RESOURCE_STATE_RESOLVE_SOURCE;
    case ResourceState::ResolveDestination:         return D3D12_RESOURCE_STATE_RESOLVE_DEST;
    case ResourceState::Present:                    return D3D12_RESOURCE_STATE_PRESENT;
    case ResourceState::IndirectArgument:           return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case ResourceState::AccelerationStructureRead:  return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::AccelerationStructureWrite: return D3D12_RESOURCE_STATE_COMMON;
    case ResourceState::ShadingRateTexture:         return D3D12_RESOURCE_STATE_COMMON;
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

static D3D12_HEAP_TYPE toD3D12HeapType(MemoryUsage usage) {
    switch (usage) {
    case MemoryUsage::GpuOnly:  return D3D12_HEAP_TYPE_DEFAULT;
    case MemoryUsage::CpuToGpu: return D3D12_HEAP_TYPE_UPLOAD;
    case MemoryUsage::GpuToCpu: return D3D12_HEAP_TYPE_READBACK;
    case MemoryUsage::CpuOnly:  return D3D12_HEAP_TYPE_UPLOAD;
    }
    return D3D12_HEAP_TYPE_DEFAULT;
}

static D3D12_RESOURCE_STATES initialBufferState(MemoryUsage usage, ResourceState requested) {
    switch (usage) {
    case MemoryUsage::CpuToGpu:
    case MemoryUsage::CpuOnly:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case MemoryUsage::GpuToCpu:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case MemoryUsage::GpuOnly:
        return requested == ResourceState::Undefined ? D3D12_RESOURCE_STATE_COMMON : toD3D12ResourceStates(requested);
    }
    return D3D12_RESOURCE_STATE_COMMON;
}

static D3D12_RESOURCE_FLAGS toBufferResourceFlags(BufferUsage usage, MemoryUsage memoryUsage) {
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (memoryUsage == MemoryUsage::GpuOnly && hasAny(usage, BufferUsage::Storage)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    return flags;
}

static D3D12_RESOURCE_FLAGS toTextureResourceFlags(TextureUsage usage) {
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    if (hasAny(usage, TextureUsage::ColorAttachment) || hasAny(usage, TextureUsage::Present)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (hasAny(usage, TextureUsage::DepthStencilAttachment)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }
    if (hasAny(usage, TextureUsage::Storage)) {
        flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    return flags;
}

static D3D12_FILTER toD3D12Filter(const SamplerDesc& desc) {
    if (desc.enableAnisotropy) {
        return desc.enableCompare ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
    }

    const bool minLinear = desc.minFilter == FilterMode::Linear;
    const bool magLinear = desc.magFilter == FilterMode::Linear;
    const bool mipLinear = desc.mipmapMode == MipmapMode::Linear;
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

static D3D12_TEXTURE_ADDRESS_MODE toD3D12AddressMode(AddressMode mode) {
    switch (mode) {
    case AddressMode::Repeat:         return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case AddressMode::MirroredRepeat: return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case AddressMode::ClampToEdge:    return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    case AddressMode::ClampToBorder:  return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    }
    return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
}

static D3D12_COMPARISON_FUNC toD3D12Compare(CompareOp op) {
    switch (op) {
    case CompareOp::Never:          return D3D12_COMPARISON_FUNC_NEVER;
    case CompareOp::Less:           return D3D12_COMPARISON_FUNC_LESS;
    case CompareOp::Equal:          return D3D12_COMPARISON_FUNC_EQUAL;
    case CompareOp::LessOrEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case CompareOp::Greater:        return D3D12_COMPARISON_FUNC_GREATER;
    case CompareOp::NotEqual:       return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case CompareOp::GreaterOrEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case CompareOp::Always:         return D3D12_COMPARISON_FUNC_ALWAYS;
    }
    return D3D12_COMPARISON_FUNC_ALWAYS;
}

static DXGI_FORMAT toD3D12VertexFormat(VertexFormat format) {
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

static D3D_PRIMITIVE_TOPOLOGY toD3D12Topology(const InputAssemblyState& state) {
    switch (state.topology) {
    case PrimitiveTopology::PointList:     return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case PrimitiveTopology::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case PrimitiveTopology::LineStrip:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
    case PrimitiveTopology::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::PatchList: {
        const u32 points = std::clamp(state.patchControlPoints == 0 ? 3u : state.patchControlPoints, 1u, 32u);
        return static_cast<D3D_PRIMITIVE_TOPOLOGY>(D3D_PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + points - 1);
    }
    }
    return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE toD3D12TopologyType(const InputAssemblyState& state) {
    switch (state.topology) {
    case PrimitiveTopology::PointList:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case PrimitiveTopology::LineList:
    case PrimitiveTopology::LineStrip:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case PrimitiveTopology::TriangleList:
    case PrimitiveTopology::TriangleStrip: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case PrimitiveTopology::PatchList:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    }
    return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
}

static D3D12_STENCIL_OP toD3D12StencilOp(StencilOp op) {
    switch (op) {
    case StencilOp::Keep:           return D3D12_STENCIL_OP_KEEP;
    case StencilOp::Zero:           return D3D12_STENCIL_OP_ZERO;
    case StencilOp::Replace:        return D3D12_STENCIL_OP_REPLACE;
    case StencilOp::IncrementClamp: return D3D12_STENCIL_OP_INCR_SAT;
    case StencilOp::DecrementClamp: return D3D12_STENCIL_OP_DECR_SAT;
    case StencilOp::Invert:         return D3D12_STENCIL_OP_INVERT;
    case StencilOp::IncrementWrap:  return D3D12_STENCIL_OP_INCR;
    case StencilOp::DecrementWrap:  return D3D12_STENCIL_OP_DECR;
    }
    return D3D12_STENCIL_OP_KEEP;
}

static D3D12_BLEND toD3D12Blend(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:                     return D3D12_BLEND_ZERO;
    case BlendFactor::One:                      return D3D12_BLEND_ONE;
    case BlendFactor::SourceColor:              return D3D12_BLEND_SRC_COLOR;
    case BlendFactor::OneMinusSourceColor:      return D3D12_BLEND_INV_SRC_COLOR;
    case BlendFactor::DestinationColor:         return D3D12_BLEND_DEST_COLOR;
    case BlendFactor::OneMinusDestinationColor: return D3D12_BLEND_INV_DEST_COLOR;
    case BlendFactor::SourceAlpha:              return D3D12_BLEND_SRC_ALPHA;
    case BlendFactor::OneMinusSourceAlpha:      return D3D12_BLEND_INV_SRC_ALPHA;
    case BlendFactor::DestinationAlpha:         return D3D12_BLEND_DEST_ALPHA;
    case BlendFactor::OneMinusDestinationAlpha: return D3D12_BLEND_INV_DEST_ALPHA;
    case BlendFactor::ConstantColor:
    case BlendFactor::ConstantAlpha:            return D3D12_BLEND_BLEND_FACTOR;
    case BlendFactor::OneMinusConstantColor:
    case BlendFactor::OneMinusConstantAlpha:    return D3D12_BLEND_INV_BLEND_FACTOR;
    }
    return D3D12_BLEND_ONE;
}

static D3D12_BLEND_OP toD3D12BlendOp(BlendOp op) {
    switch (op) {
    case BlendOp::Add:             return D3D12_BLEND_OP_ADD;
    case BlendOp::Subtract:        return D3D12_BLEND_OP_SUBTRACT;
    case BlendOp::ReverseSubtract: return D3D12_BLEND_OP_REV_SUBTRACT;
    case BlendOp::Min:             return D3D12_BLEND_OP_MIN;
    case BlendOp::Max:             return D3D12_BLEND_OP_MAX;
    }
    return D3D12_BLEND_OP_ADD;
}

static UINT8 toD3D12ColorWriteMask(ColorWriteMask mask) {
    UINT8 flags = 0;
    if (hasAny(mask, ColorWriteMask::R)) flags |= D3D12_COLOR_WRITE_ENABLE_RED;
    if (hasAny(mask, ColorWriteMask::G)) flags |= D3D12_COLOR_WRITE_ENABLE_GREEN;
    if (hasAny(mask, ColorWriteMask::B)) flags |= D3D12_COLOR_WRITE_ENABLE_BLUE;
    if (hasAny(mask, ColorWriteMask::A)) flags |= D3D12_COLOR_WRITE_ENABLE_ALPHA;
    return flags;
}

static D3D12_SHADER_VISIBILITY toD3D12ShaderVisibility(ShaderStage visibility) {
    if (visibility == ShaderStage::Vertex)         return D3D12_SHADER_VISIBILITY_VERTEX;
    if (visibility == ShaderStage::TessControl)    return D3D12_SHADER_VISIBILITY_HULL;
    if (visibility == ShaderStage::TessEvaluation) return D3D12_SHADER_VISIBILITY_DOMAIN;
    if (visibility == ShaderStage::Geometry)       return D3D12_SHADER_VISIBILITY_GEOMETRY;
    if (visibility == ShaderStage::Fragment)       return D3D12_SHADER_VISIBILITY_PIXEL;
    return D3D12_SHADER_VISIBILITY_ALL;
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
    case ShaderStage::Vertex:         return "vs_5_1";
    case ShaderStage::TessControl:    return "hs_5_1";
    case ShaderStage::TessEvaluation: return "ds_5_1";
    case ShaderStage::Geometry:       return "gs_5_1";
    case ShaderStage::Fragment:       return "ps_5_1";
    case ShaderStage::Compute:        return "cs_5_1";
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
};

static D3D12_SHADER_RESOURCE_VIEW_DESC makeTextureSrvDesc(const TextureDesc& texture, const TextureViewDesc& view, Format viewFormat) {
    D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
    desc.Format = toSrvFormat(viewFormat);
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    switch (view.dimension) {
    case TextureViewDimension::View1D:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        desc.Texture1D.MostDetailedMip = view.baseMipLevel;
        desc.Texture1D.MipLevels = view.mipLevelCount;
        break;
    case TextureViewDimension::View1DArray:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        desc.Texture1DArray.MostDetailedMip = view.baseMipLevel;
        desc.Texture1DArray.MipLevels = view.mipLevelCount;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
        break;
    case TextureViewDimension::View2D:
        desc.ViewDimension = texture.samples == SampleCount::Count1 ? D3D12_SRV_DIMENSION_TEXTURE2D : D3D12_SRV_DIMENSION_TEXTURE2DMS;
        desc.Texture2D.MostDetailedMip = view.baseMipLevel;
        desc.Texture2D.MipLevels = view.mipLevelCount;
        break;
    case TextureViewDimension::View2DArray:
        desc.ViewDimension = texture.samples == SampleCount::Count1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
        desc.Texture2DArray.MostDetailedMip = view.baseMipLevel;
        desc.Texture2DArray.MipLevels = view.mipLevelCount;
        desc.Texture2DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture2DArray.ArraySize = view.arrayLayerCount;
        break;
    case TextureViewDimension::View3D:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MostDetailedMip = view.baseMipLevel;
        desc.Texture3D.MipLevels = view.mipLevelCount;
        break;
    case TextureViewDimension::Cube:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        desc.TextureCube.MostDetailedMip = view.baseMipLevel;
        desc.TextureCube.MipLevels = view.mipLevelCount;
        break;
    case TextureViewDimension::CubeArray:
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        desc.TextureCubeArray.MostDetailedMip = view.baseMipLevel;
        desc.TextureCubeArray.MipLevels = view.mipLevelCount;
        desc.TextureCubeArray.First2DArrayFace = view.baseArrayLayer;
        desc.TextureCubeArray.NumCubes = std::max(1u, view.arrayLayerCount / 6u);
        break;
    }
    return desc;
}

static D3D12_RENDER_TARGET_VIEW_DESC makeRtvDesc(const TextureDesc& texture, const TextureViewDesc& view, Format viewFormat) {
    D3D12_RENDER_TARGET_VIEW_DESC desc{};
    desc.Format = toDxgiFormat(viewFormat);
    if (texture.dimension == TextureDimension::Texture3D) {
        desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = view.baseMipLevel;
        desc.Texture3D.FirstWSlice = view.baseArrayLayer;
        desc.Texture3D.WSize = view.arrayLayerCount;
    } else if (texture.dimension == TextureDimension::Texture1D) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_RTV_DIMENSION_TEXTURE1DARRAY : D3D12_RTV_DIMENSION_TEXTURE1D;
        desc.Texture1DArray.MipSlice = view.baseMipLevel;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
    } else if (texture.samples != SampleCount::Count1) {
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

static D3D12_DEPTH_STENCIL_VIEW_DESC makeDsvDesc(const TextureDesc& texture, const TextureViewDesc& view, Format viewFormat) {
    D3D12_DEPTH_STENCIL_VIEW_DESC desc{};
    desc.Format = toDsvFormat(viewFormat);
    desc.Flags = D3D12_DSV_FLAG_NONE;
    if (texture.dimension == TextureDimension::Texture1D) {
        desc.ViewDimension = texture.arrayLayers > 1 ? D3D12_DSV_DIMENSION_TEXTURE1DARRAY : D3D12_DSV_DIMENSION_TEXTURE1D;
        desc.Texture1DArray.MipSlice = view.baseMipLevel;
        desc.Texture1DArray.FirstArraySlice = view.baseArrayLayer;
        desc.Texture1DArray.ArraySize = view.arrayLayerCount;
    } else if (texture.samples != SampleCount::Count1) {
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

static D3D12_UNORDERED_ACCESS_VIEW_DESC makeTextureUavDesc(const TextureDesc& texture, const TextureViewDesc& view, Format viewFormat) {
    D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
    desc.Format = toDxgiFormat(viewFormat);
    if (texture.dimension == TextureDimension::Texture3D) {
        desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
        desc.Texture3D.MipSlice = view.baseMipLevel;
        desc.Texture3D.FirstWSlice = view.baseArrayLayer;
        desc.Texture3D.WSize = view.arrayLayerCount;
    } else if (texture.dimension == TextureDimension::Texture1D) {
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

// Impl 是 D3D12Renderer 的状态仓库。D3D12 的核心对象都是 COM 对象，用 ComPtr 管理生命周期。
// Descriptor 不是 COM 对象，只是 descriptor heap 中的一个槽位，所以用 CpuDescriptor 保存 CPU handle。
struct D3D12Renderer::Impl {
    struct BufferResource {
        BufferDesc desc{};
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
        void* mappedData = nullptr;
    };

    struct TextureResource {
        TextureDesc desc{};
        ComPtr<ID3D12Resource> resource;
        D3D12_RESOURCE_STATES currentState = D3D12_RESOURCE_STATE_COMMON;
        bool swapchainImage = false;
    };

    struct TextureViewResource {
        TextureViewDesc desc{};
        CpuDescriptor srv{};
        CpuDescriptor rtv{};
        CpuDescriptor dsv{};
        CpuDescriptor uav{};
    };

    struct SamplerResource {
        SamplerDesc desc{};
        CpuDescriptor sampler{};
    };

    struct ShaderResource {
        ShaderDesc desc{};
        std::vector<std::byte> bytecode;
    };

    struct BindGroupLayoutResource {
        BindGroupLayoutDesc desc{};
    };

    struct ResolvedBinding {
        u32 slot = 0;
        BindingType type = BindingType::UniformBuffer;
        ShaderStage visibility = ShaderStage::AllGraphics;
        CpuDescriptor resourceDescriptor{};
        CpuDescriptor samplerDescriptor{};
    };

    struct BindGroupResource {
        BindGroupDesc desc{};
        std::vector<ResolvedBinding> bindings;
    };

    struct PipelineLayoutResource {
        PipelineLayoutDesc desc{};
        ComPtr<ID3D12RootSignature> rootSignature;
    };

    struct PipelineCacheResource {
        PipelineCacheDesc desc{};
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
        QueryPoolDesc desc{};
        ComPtr<ID3D12QueryHeap> heap;
    };

    struct SemaphoreResource {
        SemaphoreDesc desc{};
        u64 value = 0;
        bool signaled = false;
    };

    struct FenceResource {
        FenceDesc desc{};
        ComPtr<ID3D12Fence> fence;
        HANDLE eventHandle = nullptr;
        u64 value = 0;
        bool signaled = false;
    };

    struct SwapchainResource {
        SwapchainDesc desc{};
        ComPtr<IDXGISwapChain3> swapchain;
        Format format = Format::Undefined;
        Extent2D extent{};
        std::vector<TextureHandle> images;
        std::vector<TextureViewHandle> imageViews;
        u32 currentImage = 0;
    };

    D3D12NativeHandles native{};
    D3D12RendererDesc initDesc{};
    RenderCapabilities caps{};

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

    void createDescriptorHeap(DescriptorHeapArena& arena, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity) {
        arena.type = type;
        arena.capacity = capacity;
        arena.used = 0;
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type;
        desc.NumDescriptors = capacity;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
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
        if (arena == nullptr || !arena->heap || arena->used >= arena->capacity) {
            throw std::runtime_error("D3D12 descriptor heap is exhausted");
        }

        CpuDescriptor descriptor{};
        descriptor.type = type;
        descriptor.index = arena->used++;
        descriptor.valid = true;
        descriptor.handle = arena->heap->GetCPUDescriptorHandleForHeapStart();
        descriptor.handle.ptr += static_cast<SIZE_T>(descriptor.index) * arena->increment;
        return descriptor;
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

static DXGI_GPU_PREFERENCE toDxgiGpuPreference(PowerPreference preference) {
    switch (preference) {
    case PowerPreference::Default:         return DXGI_GPU_PREFERENCE_UNSPECIFIED;
    case PowerPreference::LowPower:        return DXGI_GPU_PREFERENCE_MINIMUM_POWER;
    case PowerPreference::HighPerformance: return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
    }
    return DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
}

static bool canCreateD3D12Device(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL minimumFeatureLevel) {
    return SUCCEEDED(D3D12CreateDevice(adapter, minimumFeatureLevel, __uuidof(ID3D12Device), nullptr));
}

static ComPtr<IDXGIAdapter1> chooseAdapter(IDXGIFactory6* factory, PowerPreference preference, D3D_FEATURE_LEVEL minimumFeatureLevel) {
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

static bool supportsRequiredFeatures(const RenderCapabilities& caps, RenderFeature required) {
    if (hasAny(required, RenderFeature::Compute)                 && !caps.supportsCompute)                 return false;
    if (hasAny(required, RenderFeature::GeometryShader)          && !caps.supportsGeometryShader)          return false;
    if (hasAny(required, RenderFeature::Tessellation)            && !caps.supportsTessellation)            return false;
    if (hasAny(required, RenderFeature::SamplerAnisotropy)       && !caps.supportsSamplerAnisotropy)       return false;
    if (hasAny(required, RenderFeature::SamplerCompare)          && !caps.supportsSamplerCompare)          return false;
    if (hasAny(required, RenderFeature::TimestampQuery)          && !caps.supportsTimestampQuery)          return false;
    if (hasAny(required, RenderFeature::OcclusionQuery)          && !caps.supportsOcclusionQuery)          return false;
    if (hasAny(required, RenderFeature::PipelineStatisticsQuery) && !caps.supportsPipelineStatisticsQuery) return false;
    if (hasAny(required, RenderFeature::IndirectDraw)            && !caps.supportsIndirectDraw)            return false;
    if (hasAny(required, RenderFeature::TextureCompressionBC)    && !caps.supportsTextureCompressionBC)    return false;

    const RenderFeature unsupported =
        RenderFeature::MeshShader                |
        RenderFeature::RayTracing                |
        RenderFeature::Bindless                  |
        RenderFeature::DrawIndirectCount         |
        RenderFeature::DynamicRendering          |
        RenderFeature::ConservativeRasterization |
        RenderFeature::TextureCompressionETC2    |
        RenderFeature::TextureCompressionASTC    |
        RenderFeature::Multiview;
    return !hasAny(required, unsupported);
}

static RenderCapabilities makeCapabilities(IDXGIAdapter1* adapter, D3D_FEATURE_LEVEL featureLevel) {
    RenderCapabilities caps{};
    caps.api = GraphicsApi::Direct3D12;

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
    caps.maxBindGroups = 8;
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
        RenderFeature::Compute                 |
        RenderFeature::GeometryShader          |
        RenderFeature::Tessellation            |
        RenderFeature::SamplerAnisotropy       |
        RenderFeature::SamplerCompare          |
        RenderFeature::TimestampQuery          |
        RenderFeature::OcclusionQuery          |
        RenderFeature::PipelineStatisticsQuery |
        RenderFeature::IndirectDraw            |
        RenderFeature::TextureCompressionBC    |
        RenderFeature::DebugMarkers;
    return caps;
}

// D3D12 private 片段是“公共渲染抽象 -> D3D12 原生语言”的词典：
// - Format/ResourceState/Sampler/PipelineState 的枚举转换；
// - Descriptor heap CPU 槽位分配；
// - Impl 中保存的 ID3D12Resource/RootSignature/PSO/Swapchain/Fence；
// - DXGI adapter 选择、feature level 和 RenderCapabilities 生成。
