using Microsoft::WRL::ComPtr;

// 学习导读：
// 这个文件是统一渲染抽象到 Direct3D 11 的落地层。和 Vulkan 不同，D3D11 是偏“状态机”的
// immediate context API：创建资源和状态对象后，绘制时把 input layout、shader、RTV/DSV、
// constant buffer、SRV、sampler 等绑定到 context，然后调用 Draw/Dispatch。
//
// 因为 D3D11 没有 Vulkan 那种 descriptor set、显式 image layout 和 queue submit 模型，
// 本后端会把 RenderDefinitions.hpp 的 BindGroup/Pipeline/FramePacket 翻译成 D3D11 的
// COM 对象和 context 状态设置，尽量保持上层接口和 Vulkan 后端一致。

// D3D11 资源句柄同样使用 1-based index；0 是无效句柄。真实 COM 对象保存在 Impl 的
// vector 中，公共 API 只传递轻量 Handle。
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

// DirectX API 通常用 HRESULT 表达错误；这里集中转换成异常，让 initialize/create* 函数
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

// DXGI adapter 名称是 wchar_t，这里转回 UTF-8 供 RenderCapabilities 使用。
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

// 统一 Format 到 DXGI_FORMAT 的映射层。上层资源描述不直接暴露 DXGI 枚举，便于同一份
// RenderDefinitions.hpp 同时驱动 Vulkan 和 D3D11。
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

// D3D11 深度纹理常用 typeless 资源格式创建，再用 DSV/SRV 选择具体解释方式：
// - DSV 解释成深度/模板格式用于深度测试；
// - SRV 解释成 R 通道格式用于 shader 采样阴影图等数据。
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

// 深度纹理作为 shader resource 读取时不能直接用 D24/D32 这类 DSV 格式，需要改成可采样的
// color-like 格式；普通 color texture 则直接沿用 toDxgiFormat。
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

// 深度模板视图需要 DSV 专用格式，和创建资源用的 typeless 格式不同。
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

// Swapchain 只支持 DXGI 可呈现格式；这里把引擎偏好的格式收敛到 DXGI 能接受的后备缓冲格式。
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
    if (hasAny(usage, BufferUsage::Vertex))  flags |= D3D11_BIND_VERTEX_BUFFER;
    if (hasAny(usage, BufferUsage::Index))   flags |= D3D11_BIND_INDEX_BUFFER;
    if (hasAny(usage, BufferUsage::Uniform)) flags |= D3D11_BIND_CONSTANT_BUFFER;
    if (hasAny(usage, BufferUsage::Storage)) flags |= D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
    return flags;
}

static UINT toTextureBindFlags(TextureUsage usage, MemoryUsage memoryUsage) {
    if (memoryUsage == MemoryUsage::GpuToCpu || memoryUsage == MemoryUsage::CpuOnly) {
        return 0;
    }

    UINT flags = 0;
    if (hasAny(usage, TextureUsage::Sampled))                                                  flags |= D3D11_BIND_SHADER_RESOURCE;
    if (hasAny(usage, TextureUsage::Storage))                                                  flags |= D3D11_BIND_UNORDERED_ACCESS;
    if (hasAny(usage, TextureUsage::ColorAttachment) || hasAny(usage, TextureUsage::Present))  flags |= D3D11_BIND_RENDER_TARGET;
    if (hasAny(usage, TextureUsage::DepthStencilAttachment))                                   flags |= D3D11_BIND_DEPTH_STENCIL;
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

// 压缩纹理按 4x4 block 存储，上传时 row pitch 不是 width * pixelBytes，而是 block 数乘
// block 大小。这里封装 pitch 计算，避免调用 UpdateSubresource 时行跨度错误。
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

// ShaderDesc 可以不显式写 target profile；D3D11 需要按 stage 编译到 vs/ps/cs 等 profile。
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

// TextureViewDesc 是引擎的统一“看这张纹理的哪一部分”的描述。D3D11 会根据资源维度、
// array/mip 范围、MSAA 情况生成 SRV/RTV/DSV 描述，真正绑定到 shader 或 output merger。
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

// Impl 是 D3D11Renderer 的后端状态仓库。
// D3D11 对象都是 COM 对象，这里用 ComPtr 管生命周期；公共 Handle 只保存 1-based index。
// 注意 BindGroupLayout/PipelineLayout 在 D3D11 中没有原生等价物，它们主要作为统一抽象的
// 描述和校验数据存在，真正的绑定发生在 applyBindGroup/applyPipeline 里。
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

// WARP/software adapter 一般只作为 fallback，不参与默认硬件选择。
static bool isSoftwareAdapter(const DXGI_ADAPTER_DESC1& desc) {
    return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
}

// DXGI adapter 选择策略：
// - Default 取第一个非软件 adapter；
// - HighPerformance 倾向显存更大的 adapter；
// - LowPower 倾向显存更小/通常更省电的 adapter。
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

// D3D11 的 feature set 相对固定，但统一 RenderFeature 里包含 Vulkan/D3D12 风格能力。
// 这里既检查 caps 是否支持，也明确排除本后端没有实现或 API 本身不适合表达的能力。
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

// 把 adapter/feature level 整理成引擎统一的 RenderCapabilities。
// 这些能力会被上层用于选择渲染路径，也会被 requiredFeatures 校验。
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

// D3D11 private 片段放所有“后端内部语言”：
// - 引擎句柄到 vector 槽位的 1-based handle 管理；
// - Format/VertexFormat/Sampler/Blend/Stencil 等跨 API enum 到 DXGI/D3D11 enum 的转换；
// - Impl 中保存的 COM 资源结构；
// - DXGI adapter 选择和 RenderCapabilities 生成。
// 读这里时重点看“RenderDefinitions.hpp 的抽象字段，最终落到哪个 D3D11 原生类型”。
