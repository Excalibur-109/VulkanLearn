#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include "Math.hpp"
#include "PBRDemoConfig.hpp"
#include "RHI.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr rhi::u32 WINDOW_WIDTH = 1280;     ///< 窗口模式下的默认客户区宽度。
constexpr rhi::u32 WINDOW_HEIGHT = 800;     ///< 窗口模式下的默认客户区高度。
constexpr rhi::u32 FRAMES_IN_FLIGHT = 2;    ///< CPU 可提前准备的最大帧数，也是二进制信号的轮转数量。
constexpr rhi::u32 SHADOW_MAP_SIZE = 2048;  ///< 阴影图边长；精度与显存、清理和采样成本之间的折中。
constexpr float PI = 3.14159265359F;        ///< 球面参数化使用的单精度圆周率。

/// PBR 与 Shadow Pipeline 共用的交错顶点格式。
struct Vertex {
    float3 position{};  ///< 模型空间位置。
    float3 normal{};    ///< 模型空间单位法线。
    float2 uv{};        ///< 纹理坐标；当前材质仍保留它以演示完整顶点布局。
};

/// 与 GLSL std140/HLSL cbuffer 对应的每物体常量数据，16 字节对齐后可跨后端上传。
struct alignas(16) UniformBufferObject {
    // 主相机和物体变换。Shader 中按 projection * view * model * position 使用。
    float4x4 model{1.0F};       ///< 模型空间到世界空间。
    float4x4 view{1.0F};        ///< 世界空间到主相机观察空间。
    float4x4 projection{1.0F};  ///< 主相机观察空间到裁剪空间。

    // lightDirection.xyz 表示光线传播方向，因此 Shader 取反得到“表面指向光源”的 L。
    // vec4 可让 C++、GLSL std140 和 HLSL cbuffer 都自然满足 16 字节对齐。
    float4 lightDirection{};      ///< xyz 为世界空间光线传播方向，w 未使用。
    float4 lightColor{};          ///< rgb 为线性空间光强，a 未使用。
    float4 cameraPosition{};      ///< xyz 为世界空间相机位置，w 固定为 1。
    float4 baseColor{};           ///< rgb 为 albedo，a 为 metallic。
    float4 materialParameters{};  ///< x 为 roughness，y 为 AO，zw 保留。

    // lightViewProjection 是生成/查询 Shadow Map 的共同坐标系；shadowParameters 的
    // xy 是单个 texel 的 UV 尺寸，z 是最小 bias，w 是随法线斜率增长的 bias。
    float4x4 lightViewProjection{1.0F};  ///< 世界空间到光源裁剪空间。
    float4 shadowParameters{};           ///< PCF texel 步长与深度比较偏移。
};

/// Math 的 Matrix 是行主序；两个 shader 均按 column_major 接收，因此上传前转置。
[[nodiscard]] float4x4 ToShaderMatrix(const float4x4& matrix) noexcept {
    return Transpose(matrix);
}

/// CPU 侧临时网格，创建 GPU buffer 后即可释放。
struct Mesh {
    std::vector<Vertex> vertices;   ///< 交错顶点数组。
    std::vector<rhi::u32> indices;  ///< 32 位三角形索引数组。
};

/// 用 RAII 管理当前线程的 COM 初始化，供 Windows Imaging Component 解码 PNG。
class ComApartment {
public:
    ComApartment() {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result_) && result_ != RPC_E_CHANGED_MODE) {
            throw std::runtime_error("CoInitializeEx failed");
        }
    }

    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }

private:
    HRESULT result_ = E_FAIL;  ///< 保存初始化结果，仅成功初始化时调用 CoUninitialize。
};

/// 将失败的 HRESULT 转成包含操作名称的 C++ 异常。
void ThrowIfFailed(HRESULT result, const char* operation) {
    if (FAILED(result)) {
        throw std::runtime_error(
            std::string(operation) + " failed with HRESULT " +
            std::to_string(static_cast<unsigned long>(result)));
    }
}

/// 将 UTF-8 资源路径转换为 Win32/WIC 使用的 UTF-16 字符串。
std::wstring Utf8ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (length <= 0) {
        throw std::runtime_error("Asset path is not valid UTF-8");
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            length) != length) {
        throw std::runtime_error("Asset path conversion failed");
    }
    return wide;
}

/// WIC 统一转换后的 RGBA8 图像。
struct DecodedImage {
    rhi::u32 width = 0;             ///< 像素宽度。
    rhi::u32 height = 0;            ///< 像素高度。
    std::vector<std::byte> pixels;  ///< 从左上角开始的紧密 RGBA8 像素。
};

/// 通过 WIC 解码 PNG，并强制转换为每像素 4 字节的 RGBA8 数据。
DecodedImage LoadPngRGBA8(std::string_view utf8Path) {
    using Microsoft::WRL::ComPtr;

    ComPtr<IWICImagingFactory> factory;
    ThrowIfFailed(
        CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf())),
        "Create WIC imaging factory");

    ComPtr<IWICBitmapDecoder> decoder;
    const std::wstring path = Utf8ToWide(utf8Path);
    ThrowIfFailed(
        factory->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            decoder.GetAddressOf()),
        "Open skybox PNG");

    ComPtr<IWICBitmapFrameDecode> frame;
    ThrowIfFailed(decoder->GetFrame(0, frame.GetAddressOf()), "Decode skybox PNG frame");

    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed(factory->CreateFormatConverter(converter.GetAddressOf()), "Create WIC format converter");
    ThrowIfFailed(
        converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom),
        "Convert skybox PNG to RGBA8");

    UINT width = 0;
    UINT height = 0;
    ThrowIfFailed(converter->GetSize(&width, &height), "Read skybox PNG dimensions");
    if (width == 0 || height == 0 || width > UINT_MAX / 4U) {
        throw std::runtime_error("Skybox PNG dimensions are invalid");
    }

    const UINT stride = width * 4U;
    const rhi::u64 byteCount = static_cast<rhi::u64>(stride) * height;
    if (byteCount > UINT_MAX) {
        throw std::runtime_error("Skybox PNG is too large for WIC CopyPixels");
    }

    DecodedImage result{};
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<std::size_t>(byteCount));
    ThrowIfFailed(
        converter->CopyPixels(
            nullptr,
            stride,
            static_cast<UINT>(byteCount),
            reinterpret_cast<BYTE*>(result.pixels.data())),
        "Copy skybox PNG pixels");
    return result;
}

/// 从 WinMain 命令行读取的 Demo 启动配置。
struct DemoOptions {
    rhi::RHIGraphicsAPI api = rhi::RHIGraphicsAPI::Vulkan;  ///< `--api=` 选择的 RHI 后端。
    rhi::u64 maxFrames = 0;                                 ///< `--frames=` 上限；0 表示持续运行。
};

/// 解析 `--api=vulkan|d3d11|d3d12` 和可选的 `--frames=N`。
DemoOptions ParseOptions(std::string_view commandLine) {
    DemoOptions options{};
    std::istringstream arguments{std::string(commandLine)};
    for (std::string argument; arguments >> argument;) {
        constexpr std::string_view apiPrefix = "--api=";
        constexpr std::string_view framesPrefix = "--frames=";
        if (argument.starts_with(apiPrefix)) {
            const std::string_view value(
                argument.data() + apiPrefix.size(),
                argument.size() - apiPrefix.size());
            if (value == "vulkan") {
                options.api = rhi::RHIGraphicsAPI::Vulkan;
            } else if (value == "d3d11") {
                options.api = rhi::RHIGraphicsAPI::D3D11;
            } else if (value == "d3d12") {
                options.api = rhi::RHIGraphicsAPI::D3D12;
            } else {
                throw std::runtime_error("Unknown --api value: " + std::string(value));
            }
        } else if (argument.starts_with(framesPrefix)) {
            const std::string_view value(
                argument.data() + framesPrefix.size(),
                argument.size() - framesPrefix.size());
            const auto [end, error] = std::from_chars(
                value.data(),
                value.data() + value.size(),
                options.maxFrames);
            if (error != std::errc{} || end != value.data() + value.size()) {
                throw std::runtime_error("Invalid --frames value: " + std::string(value));
            }
        }
    }
    return options;
}

/// 返回适合显示在 Win32 标题栏中的后端名称。
const wchar_t* ApiDisplayName(rhi::RHIGraphicsAPI api) noexcept {
    switch (api) {
    case rhi::RHIGraphicsAPI::Vulkan: return L"Vulkan";
    case rhi::RHIGraphicsAPI::D3D11:  return L"Direct3D 11";
    case rhi::RHIGraphicsAPI::D3D12:  return L"Direct3D 12";
    default:                           return L"Unknown";
    }
}

/// 创建位于 XZ 平面的方形接收面，正面朝向 +Y。
Mesh MakePlane(float halfSize = 3.25F) {
    Mesh mesh{};
    mesh.vertices = {
        {{-halfSize, 0.0F, -halfSize}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}},
        {{halfSize, 0.0F, -halfSize}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}},
        {{halfSize, 0.0F, halfSize}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F}},
        {{-halfSize, 0.0F, halfSize}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}}};
    // 从 +Y 方向观察时保持逆时针绕序，使几何正面与顶点法线 (0, 1, 0)
    // 一致；否则启用 back-face culling 后整张地面都会被剔除。
    mesh.indices = {0, 2, 1, 0, 3, 2};
    return mesh;
}

/// 通过经纬线参数化生成单位法线平滑的 UV 球体。
Mesh MakeSphere(
    rhi::u32 latitudeCount,
    rhi::u32 longitudeCount,
    float radius = 1.0F) {
    Mesh mesh{};
    mesh.vertices.reserve((latitudeCount + 1) * (longitudeCount + 1));
    mesh.indices.reserve(latitudeCount * longitudeCount * 6);

    for (rhi::u32 latitudeIndex = 0; latitudeIndex <= latitudeCount; ++latitudeIndex) {
        for (rhi::u32 longitudeIndex = 0; longitudeIndex <= longitudeCount; ++longitudeIndex) {
            const float longitudeRatio =
                static_cast<float>(longitudeIndex) / static_cast<float>(longitudeCount);
            const float latitudeRatio =
                static_cast<float>(latitudeIndex) / static_cast<float>(latitudeCount);
            const float longitudeAngle = longitudeRatio * 2.0F * PI;
            const float latitudeAngle = latitudeRatio * PI;
            const float sinLatitude = std::sin(latitudeAngle);
            const float3 normal{
                -std::cos(longitudeAngle) * sinLatitude,
                std::cos(latitudeAngle),
                std::sin(longitudeAngle) * sinLatitude};
            mesh.vertices.push_back(
                Vertex{normal * radius, normal, {longitudeRatio, latitudeRatio}});
        }
    }

    // 相邻两条纬线构成一条纬度带；每次外层循环为该带生成一整圈三角形。
    for (rhi::u32 latitudeIndex = 0; latitudeIndex < latitudeCount; ++latitudeIndex) {
        // 相邻两条经线把纬度带切成一个四边形网格。
        for (rhi::u32 longitudeIndex = 0; longitudeIndex < longitudeCount; ++longitudeIndex) {
            // 每行有 longitudeCount + 1 个顶点，因为 U=0 和 U=1 在接缝处位置相同，
            // 但需要保留两份顶点以分别保存纹理坐标 0 和 1。
            const rhi::u32 upperRow = latitudeIndex * (longitudeCount + 1);
            const rhi::u32 lowerRow = (latitudeIndex + 1) * (longitudeCount + 1);
            const rhi::u32 upperLeft = upperRow + longitudeIndex;
            const rhi::u32 lowerLeft = lowerRow + longitudeIndex;
            const rhi::u32 lowerRight = lowerRow + longitudeIndex + 1;
            const rhi::u32 upperRight = upperRow + longitudeIndex + 1;
            // 按逆时针顺序把四边形拆成两个三角形，使右手坐标系下的正面朝向球外。
            mesh.indices.insert(
                mesh.indices.end(),
                {upperLeft, lowerLeft, lowerRight, upperLeft, lowerRight, upperRight});
        }
    }
    return mesh;
}

/// 将连续 trivially-copyable 元素复制为 RHI 上传队列使用的字节数组。
template <typename Type>
std::vector<std::byte> ToBytes(const std::vector<Type>& values) {
    std::vector<std::byte> result(values.size() * sizeof(Type));
    if (!result.empty()) {
        std::memcpy(result.data(), values.data(), result.size());
    }
    return result;
}

/// 将单个 trivially-copyable 对象复制为 RHI 上传队列使用的字节数组。
template <typename Type>
std::vector<std::byte> ToBytes(const Type& value) {
    std::vector<std::byte> result(sizeof(Type));
    std::memcpy(result.data(), &value, sizeof(Type));
    return result;
}

/// 串联 Win32 窗口、RHI 资源、RenderGraph 构建和逐帧提交的 PBR 演示程序。
class PBRDemoApp {
public:
    /// 保存启动选项；实际系统资源统一由 Run 按依赖顺序创建。
    explicit PBRDemoApp(DemoOptions options)
        : options_(options) {
    }

    /// 执行完整应用生命周期，并在主循环退出后释放全部 RHI 资源。
    void Run(HINSTANCE instance) {
        CreateWindowHandle(instance);
        CreateDevice(instance);
        CreateStaticResources();
        CreateSwapchainResources();
        MainLoop();
        Cleanup();
    }

private:
    DemoOptions options_{};            ///< 后端和自动退出帧数等启动配置。
    HWND window_ = nullptr;            ///< swapchain 绑定的 Win32 窗口。
    bool running_ = true;              ///< 主消息循环继续运行的标志。
    bool framebufferResized_ = false;  ///< WM_SIZE 或 acquire/submit 失败后请求重建 swapchain。
    bool isFullscreen_ = false;        ///< true 为覆盖主显示器的无边框全屏，false 为 1280x800 窗口。

    std::unique_ptr<rhi::RHIDevice> device_;                      ///< 当前选择的 Vulkan/D3D 设备门面。
    rhi::RHISwapchain swapchain_{};                               ///< 窗口呈现链。
    std::vector<rhi::RHITexture> swapchainImages_;                ///< swapchain 暴露的可呈现颜色纹理。
    rhi::RHIFormat swapchainFormat_ = rhi::RHIFormat::Undefined;  ///< Pipeline color attachment 必须匹配的格式。
    rhi::RHIExtent2D swapchainExtent_{};                          ///< 当前可呈现区域的像素尺寸。
    rhi::RHITexture depthTexture_{};                              ///< 与窗口尺寸一致的主相机深度纹理。
    rhi::RHITextureView depthView_{};                             ///< 主深度纹理的 depth aspect 视图。

    rhi::RHIBuffer vertexBuffer_{};         ///< 球体与地面共享的交错顶点缓冲。
    rhi::RHIBuffer indexBuffer_{};          ///< 球体与地面共享的 32 位索引缓冲。
    rhi::RHIBuffer sphereUniformBuffer_{};  ///< 球体每帧 UBO。
    rhi::RHIBuffer planeUniformBuffer_{};   ///< 地面每帧 UBO。

    // Shadow Mapping 的核心资源：
    // 1. ShadowMap Pass 从光源视角把最近深度写入 shadowTexture_；
    // 2. OpaquePBR Pass 通过 shadowView_ + shadowSampler_ 比较当前片元深度；
    // 3. 全过程只发生在 GPU，CPU 不读取阴影图。
    //
    // Texture 是实际显存资源，View 说明如何解释其 depth aspect，Sampler 说明过滤、
    // 越界和深度比较规则。三者职责不同，所以 RHI 将它们拆成三个对象。
    rhi::RHITexture shadowTexture_{};   ///< Shadow Pass 写入、PBR Pass 采样的 D32 深度纹理。
    rhi::RHITextureView shadowView_{};  ///< 仅暴露 shadowTexture_ 的 depth aspect。
    rhi::RHISampler shadowSampler_{};   ///< 执行 LessOrEqual 深度比较和 PCF 基础过滤。

    rhi::RHIBindSetLayout bindSetLayout_{};    ///< 主 PBR 的 UBO、阴影图和 skybox 资源布局。
    rhi::RHIBindSet sphereBindSet_{};          ///< 球体 UBO 与场景共享纹理绑定。
    rhi::RHIBindSet planeBindSet_{};           ///< 地面 UBO 与场景共享纹理绑定。
    rhi::RHIPipelineLayout pipelineLayout_{};  ///< 主 PBR Pipeline 使用的资源布局集合。
    rhi::RHIPipeline pipeline_{};              ///< 主相机绘制球体和地面的 PBR 图形管线。

    // Shadow Pass 只需要物体 UBO，不需要读取 Shadow Map 自己，因此使用独立 BindSet。
    // 如果直接复用主 PBR BindSet，就可能在同一时刻把 shadowTexture_ 同时绑定为：
    // - DSV/depth attachment：当前 Pass 正在写；
    // - SRV/sampled texture：Shader 准备读。
    // 这是资源读写冲突，D3D11 会强制解绑并报告警告，Vulkan/D3D12 则需要非法状态组合。
    rhi::RHIBindSetLayout shadowBindSetLayout_{};    ///< Shadow Pipeline 仅包含 UBO 的绑定布局。
    rhi::RHIBindSet shadowSphereBindSet_{};          ///< 阴影投射球体使用的 UBO 绑定。
    rhi::RHIPipelineLayout shadowPipelineLayout_{};  ///< depth-only Pipeline 的资源布局集合。
    rhi::RHIPipeline shadowPipeline_{};              ///< 从光源视角写入 D32 的 depth-only 管线。

    // Cube texture 保存环境图，Cube view 负责方向采样，独立 Pipeline 在主深度之后绘制背景。
    rhi::RHITexture skyboxTexture_{};                ///< 六层 RGBA8 sRGB cube-compatible 纹理。
    rhi::RHITextureView skyboxView_{};               ///< 将六个 array layer 解释为 cubemap。
    rhi::RHISampler skyboxSampler_{};                ///< ClampToEdge 线性采样器，避免面边缘重复。
    rhi::RHIBindSetLayout skyboxBindSetLayout_{};    ///< Skybox UBO 与 cubemap 的资源布局。
    rhi::RHIBindSet skyboxBindSet_{};                ///< 相机 UBO 和环境 cubemap 的实际绑定。
    rhi::RHIPipelineLayout skyboxPipelineLayout_{};  ///< Skybox Pipeline 使用的布局集合。
    rhi::RHIPipeline skyboxPipeline_{};              ///< 深度只读、LessOrEqual 的背景绘制管线。

    std::vector<std::byte> initialVertexData_;                       ///< 首帧上传的合并顶点数据。
    std::vector<std::byte> initialIndexData_;                        ///< 首帧上传的合并索引数据。
    std::array<std::vector<std::byte>, 6> initialSkyboxFaceData_{};  ///< 按 +X/-X/+Y/-Y/+Z/-Z 排列的六面像素。
    bool staticUploadsPending_ = true;                               ///< 静态 buffer/cubemap 仅在首个成功帧前上传。
    rhi::u32 skyboxWidth_ = 0;                                       ///< cubemap 单面的像素宽度。
    rhi::u32 skyboxHeight_ = 0;                                      ///< cubemap 单面的像素高度。
    rhi::u32 sphereVertexCount_ = 0;                                 ///< 计算地面 base-vertex 使用的球体顶点数。
    rhi::u32 sphereIndexCount_ = 0;                                  ///< 球体 draw 的索引数量。
    rhi::u32 planeIndexCount_ = 0;                                   ///< 地面 draw 的索引数量。
    rhi::u64 sphereIndexOffset_ = 0;                                 ///< 合并索引 buffer 中球体的字节偏移。
    rhi::u64 planeIndexOffset_ = 0;                                  ///< 合并索引 buffer 中地面的字节偏移。

    // 每个 frames-in-flight 槽位各自持有 acquire/present 二进制同步信号。
    std::array<rhi::RHIGPUWaitGPUSignal, FRAMES_IN_FLIGHT> imageAvailable_{};  ///< acquire 完成后由提交等待。
    std::array<rhi::RHIGPUWaitGPUSignal, FRAMES_IN_FLIGHT> renderFinished_{};  ///< 图形提交完成后由 present 等待。

    // 只有成功提交后才推进槽位和累计帧号。
    rhi::u32 frameSlot_ = 0;   ///< 当前轮转的 frames-in-flight 槽位。
    rhi::u64 frameIndex_ = 0;  ///< 已成功提交的累计帧号。

    /// 球体旋转动画使用的单调时钟原点。
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();

    /// 将 Win32 消息转发给实例，并把 resize/close 转为主循环状态。
    static LRESULT CALLBACK WindowProcedure(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam) {
        if (message == WM_CREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCT*>(lParam);
            SetWindowLongPtr(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            return 0;
        }
        auto* app = reinterpret_cast<PBRDemoApp*>(
            GetWindowLongPtr(window, GWLP_USERDATA));
        if (app != nullptr) {
            if (message == WM_SIZE) {
                app->framebufferResized_ = true;
                return 0;
            }
            if (message == WM_CLOSE || message == WM_DESTROY) {
                app->running_ = false;
                PostQuitMessage(0);
                return 0;
            }
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    /// 注册窗口类并创建窗口模式或无边框全屏窗口。
    void CreateWindowHandle(HINSTANCE instance) {
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.hInstance = instance;
        windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
        windowClass.lpszClassName = L"RHIRenderGraphPBRDemo";
        if (RegisterClassExW(&windowClass) == 0) {
            throw std::runtime_error("RegisterClassEx failed");
        }

        RECT rectangle{0, 0, static_cast<LONG>(WINDOW_WIDTH), static_cast<LONG>(WINDOW_HEIGHT)};
        AdjustWindowRect(&rectangle, WS_OVERLAPPEDWINDOW, FALSE);

        DWORD windowStyle = WS_OVERLAPPEDWINDOW;
        int windowX = CW_USEDEFAULT;
        int windowY = CW_USEDEFAULT;
        int windowWidth = rectangle.right - rectangle.left;
        int windowHeight = rectangle.bottom - rectangle.top;

        if (isFullscreen_) {
            // 无边框全屏不改变显示器分辨率，只让 WS_POPUP 窗口覆盖整个主显示器。
            // 使用 rcMonitor 而不是 rcWork，确保窗口也覆盖任务栏所在区域。
            const POINT primaryMonitorPoint{0, 0};
            const HMONITOR monitor = MonitorFromPoint(
                primaryMonitorPoint,
                MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO monitorInfo{};
            monitorInfo.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(monitor, &monitorInfo) == FALSE) {
                throw std::runtime_error("GetMonitorInfoW failed");
            }

            windowStyle = WS_POPUP;
            windowX = monitorInfo.rcMonitor.left;
            windowY = monitorInfo.rcMonitor.top;
            windowWidth = monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left;
            windowHeight = monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top;
        }

        const std::wstring windowTitle =
            std::wstring(L"RHI RenderGraph PBR Demo - ") +
            ApiDisplayName(options_.api);
        window_ = CreateWindowExW(
            0,
            windowClass.lpszClassName,
            windowTitle.c_str(),
            windowStyle | WS_VISIBLE,
            windowX,
            windowY,
            windowWidth,
            windowHeight,
            nullptr,
            nullptr,
            instance,
            this);
        if (window_ == nullptr) {
            throw std::runtime_error("CreateWindowEx failed");
        }
    }

    /// 初始化所选 RHI 后端，并创建每帧 acquire/present 二进制信号。
    void CreateDevice(HINSTANCE instance) {
        rhi::RHIDeviceCreateDesc desc{};
        desc.backend.applicationName = "RHI RenderGraph PBR Demo";
        desc.backend.preferredApi = options_.api;
        desc.backend.validation = rhi::RHIValidationMode::Enabled;
        desc.backend.framesInFlight = FRAMES_IN_FLIGHT;
        desc.nativeWindow = window_;

        // Vulkan 必须由平台层提供 VkSurfaceKHR；D3D 后端直接使用上面的 HWND。
        // 将原生参数限制在对应 API 分支，避免公共 Demo 无意间依赖某个后端。
        if (options_.api == rhi::RHIGraphicsAPI::Vulkan) {
            desc.requiredVulkanInstanceExtensions = {
                VK_KHR_SURFACE_EXTENSION_NAME,
                VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
            desc.requiredVulkanDeviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
            desc.createVulkanSurface = [instance, window = window_](
                                           std::uintptr_t nativeInstance) {
                VkWin32SurfaceCreateInfoKHR surfaceInfo{};
                surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                surfaceInfo.hinstance = instance;
                surfaceInfo.hwnd = window;
                VkSurfaceKHR surface = VK_NULL_HANDLE;
                if (vkCreateWin32SurfaceKHR(
                        reinterpret_cast<VkInstance>(nativeInstance),
                        &surfaceInfo,
                        nullptr,
                        &surface) != VK_SUCCESS) {
                    return std::uintptr_t{0};
                }
                return reinterpret_cast<std::uintptr_t>(surface);
            };
            desc.ownsVulkanSurface = true;
        }

        std::string error;
        device_ = rhi::CreateInitializedRHIDevice(desc, &error);
        if (device_ == nullptr) {
            throw std::runtime_error("RHI initialization failed: " + error);
        }

        for (rhi::u32 index = 0; index < FRAMES_IN_FLIGHT; ++index) {
            imageAvailable_[index] = device_->CreateGPUWaitGPUSignal(
                {"PBR.ImageAvailable" + std::to_string(index),
                 rhi::RHIGPUWaitGPUSignalType::Binary,
                 0});
            renderFinished_[index] = device_->CreateGPUWaitGPUSignal(
                {"PBR.RenderFinished" + std::to_string(index),
                 rhi::RHIGPUWaitGPUSignalType::Binary,
                 0});
        }
    }

    /// 创建与窗口尺寸无关的几何、UBO、阴影、skybox、布局和绑定资源。
    void CreateStaticResources() {
        const Mesh sphere = MakeSphere(32, 64);
        const Mesh plane = MakePlane();
        sphereVertexCount_ = static_cast<rhi::u32>(sphere.vertices.size());
        sphereIndexCount_ = static_cast<rhi::u32>(sphere.indices.size());
        planeIndexCount_ = static_cast<rhi::u32>(plane.indices.size());

        // 球体和地面合并进同一对 GPU buffer。地面 draw 通过 vertexOffsetElements
        // 跳过球体顶点，通过 planeIndexOffset_ 跳过球体索引。
        std::vector<Vertex> vertices = sphere.vertices;
        vertices.insert(vertices.end(), plane.vertices.begin(), plane.vertices.end());
        std::vector<rhi::u32> indices = sphere.indices;
        indices.insert(indices.end(), plane.indices.begin(), plane.indices.end());
        initialVertexData_ = ToBytes(vertices);
        initialIndexData_ = ToBytes(indices);
        sphereIndexOffset_ = 0;
        planeIndexOffset_ = static_cast<rhi::u64>(sphereIndexCount_) * sizeof(rhi::u32);

        rhi::RHIBufferDesc vertexDesc{};
        vertexDesc.debugName = "PBR.VertexBuffer";
        vertexDesc.size = initialVertexData_.size();
        vertexDesc.usage = rhi::RHIBufferUsage::Vertex |
                           rhi::RHIBufferUsage::TransferDestination;
        vertexBuffer_ = device_->CreateBuffer(vertexDesc);

        rhi::RHIBufferDesc indexDesc{};
        indexDesc.debugName = "PBR.IndexBuffer";
        indexDesc.size = initialIndexData_.size();
        indexDesc.usage = rhi::RHIBufferUsage::Index |
                          rhi::RHIBufferUsage::TransferDestination;
        indexBuffer_ = device_->CreateBuffer(indexDesc);

        rhi::RHIBufferDesc uniformDesc{};
        uniformDesc.size = sizeof(UniformBufferObject);
        uniformDesc.usage = rhi::RHIBufferUsage::Uniform |
                            rhi::RHIBufferUsage::TransferDestination;
        uniformDesc.debugName = "PBR.SphereUniform";
        sphereUniformBuffer_ = device_->CreateBuffer(uniformDesc);
        uniformDesc.debugName = "PBR.PlaneUniform";
        planeUniformBuffer_ = device_->CreateBuffer(uniformDesc);

        // 文件顺序必须与 cubemap array layer 约定一致，否则方向采样会看到错位或旋转的面。
        constexpr std::array<std::string_view, 6> faceFiles = {
            "px.png", "nx.png", "py.png", "ny.png", "pz.png", "nz.png"};
        const std::string skyboxDirectory =
            std::string(pbr_demo_config::ASSET_DIRECTORY) +
            "/sky_27_cubemap_2k/";
        for (std::size_t face = 0; face < faceFiles.size(); ++face) {
            DecodedImage image = LoadPngRGBA8(
                skyboxDirectory + std::string(faceFiles[face]));
            if (face == 0) {
                skyboxWidth_ = image.width;
                skyboxHeight_ = image.height;
                if (skyboxWidth_ != skyboxHeight_) {
                    throw std::runtime_error("Skybox faces must be square");
                }
            } else if (image.width != skyboxWidth_ || image.height != skyboxHeight_) {
                throw std::runtime_error("All skybox faces must have identical dimensions");
            }
            initialSkyboxFaceData_[face] = std::move(image.pixels);
        }

        // 环境图是显示颜色，所以使用 sRGB 格式让采样阶段自动解码到线性空间参与 PBR。
        // CubeCompatible 允许同一个 6-layer Texture2D 建立 Cube view。
        rhi::RHITextureDesc skyboxTextureDesc{};
        skyboxTextureDesc.debugName = "PBR.SkyboxCube";
        skyboxTextureDesc.dimension = rhi::RHITextureDimension::Texture2D;
        skyboxTextureDesc.extent = {skyboxWidth_, skyboxHeight_, 1};
        skyboxTextureDesc.arrayLayers = 6;
        skyboxTextureDesc.format = rhi::RHIFormat::RGBA8_SRGB;
        skyboxTextureDesc.usage = rhi::RHITextureUsage::Sampled |
                                  rhi::RHITextureUsage::TransferDestination;
        skyboxTextureDesc.flags = rhi::RHITextureCreateFlags::CubeCompatible;
        skyboxTexture_ = device_->CreateTexture(skyboxTextureDesc);

        rhi::RHITextureViewDesc skyboxViewDesc{};
        skyboxViewDesc.debugName = "PBR.SkyboxCubeView";
        skyboxViewDesc.texture = skyboxTexture_;
        skyboxViewDesc.dimension = rhi::RHITextureViewDimension::Cube;
        skyboxViewDesc.format = skyboxTextureDesc.format;
        skyboxViewDesc.aspect = rhi::RHITextureAspect::Color;
        skyboxViewDesc.arrayLayerCount = 6;
        skyboxView_ = device_->CreateTextureView(skyboxViewDesc);

        rhi::RHISamplerDesc skyboxSamplerDesc{};
        skyboxSamplerDesc.debugName = "PBR.SkyboxSampler";
        skyboxSamplerDesc.minFilter = rhi::RHIFilterMode::Linear;
        skyboxSamplerDesc.magFilter = rhi::RHIFilterMode::Linear;
        skyboxSamplerDesc.mipmapMode = rhi::RHIMipmapMode::Nearest;
        skyboxSamplerDesc.addressU = rhi::RHIAddressMode::ClampToEdge;
        skyboxSamplerDesc.addressV = rhi::RHIAddressMode::ClampToEdge;
        skyboxSamplerDesc.addressW = rhi::RHIAddressMode::ClampToEdge;
        skyboxSamplerDesc.maxLod = 0.0F;
        skyboxSampler_ = device_->CreateSampler(skyboxSamplerDesc);

        // binding 0 提供相机矩阵，由 Skybox Vertex Shader 去除观察平移；binding 2
        // 提供环境 cubemap。编号与 PBR layout 一致，方便 GLSL/HLSL 共享资源槽约定。
        rhi::RHIBindSetLayoutDesc skyboxBindLayoutDesc{};
        skyboxBindLayoutDesc.debugName = "PBR.SkyboxBindSetLayout";
        skyboxBindLayoutDesc.set = 0;
        skyboxBindLayoutDesc.entries.push_back({
            0,
            rhi::RHIBindingType::UniformBuffer,
            rhi::RHIShaderStage::Vertex});
        rhi::RHIBindSetLayoutEntry skyboxTextureEntry{};
        skyboxTextureEntry.binding = 2;
        skyboxTextureEntry.type = rhi::RHIBindingType::CombinedTextureSampler;
        skyboxTextureEntry.visibility = rhi::RHIShaderStage::Fragment;
        skyboxTextureEntry.textureViewDimension = rhi::RHITextureViewDimension::Cube;
        skyboxTextureEntry.textureSampleType = rhi::RHITextureSampleType::Float;
        skyboxBindLayoutDesc.entries.push_back(skyboxTextureEntry);
        skyboxBindSetLayout_ = device_->CreateBindSetLayout(skyboxBindLayoutDesc);

        rhi::RHIBindSetDesc skyboxBindSetDesc{};
        skyboxBindSetDesc.debugName = "PBR.SkyboxBindSet";
        skyboxBindSetDesc.layout = skyboxBindSetLayout_;
        rhi::RHIResourceBinding skyboxUniformBinding{};
        skyboxUniformBinding.binding = 0;
        skyboxUniformBinding.type = rhi::RHIBindingType::UniformBuffer;
        skyboxUniformBinding.buffer = {
            sphereUniformBuffer_, 0, sizeof(UniformBufferObject)};
        skyboxBindSetDesc.bindings.push_back(skyboxUniformBinding);
        rhi::RHIResourceBinding skyboxTextureBinding{};
        skyboxTextureBinding.binding = 2;
        skyboxTextureBinding.type = rhi::RHIBindingType::CombinedTextureSampler;
        skyboxTextureBinding.texture = {skyboxView_, skyboxTexture_};
        skyboxTextureBinding.sampler = skyboxSampler_;
        skyboxBindSetDesc.bindings.push_back(skyboxTextureBinding);
        skyboxBindSet_ = device_->CreateBindSet(skyboxBindSetDesc);

        rhi::RHIPipelineLayoutDesc skyboxPipelineLayoutDesc{};
        skyboxPipelineLayoutDesc.debugName = "PBR.SkyboxPipelineLayout";
        skyboxPipelineLayoutDesc.bindSetLayouts.push_back(skyboxBindSetLayout_);
        skyboxPipelineLayout_ = device_->CreatePipelineLayout(
            skyboxPipelineLayoutDesc);

        // Shadow Map 本质是一张“可采样的深度附件”：
        // - DepthStencilAttachment：允许 Shadow Pass 做深度测试并写入最近深度；
        // - Sampled：允许后续 PBR Fragment Shader 把它当只读纹理采样。
        // Vulkan 会据此组合 VkImageUsageFlags；D3D11/D3D12 后端会创建 typeless 资源，
        // 再分别建立 D32 DSV 与 R32_FLOAT SRV，使同一块显存支持两种解释方式。
        rhi::RHITextureDesc shadowTextureDesc{};
        shadowTextureDesc.debugName = "PBR.ShadowDepth";
        shadowTextureDesc.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
        shadowTextureDesc.format = rhi::RHIFormat::D32_Float;
        shadowTextureDesc.usage = rhi::RHITextureUsage::DepthStencilAttachment |
                                  rhi::RHITextureUsage::Sampled;
        shadowTexture_ = device_->CreateTexture(shadowTextureDesc);

        // View 只暴露 depth aspect。这里没有 stencil，也不需要 color view。
        rhi::RHITextureViewDesc shadowViewDesc{};
        shadowViewDesc.debugName = "PBR.ShadowDepthView";
        shadowViewDesc.texture = shadowTexture_;
        shadowViewDesc.format = shadowTextureDesc.format;
        shadowViewDesc.aspect = rhi::RHITextureAspect::Depth;
        shadowView_ = device_->CreateTextureView(shadowViewDesc);

        // Comparison Sampler 不直接返回纹理中的深度，而是执行：
        //     referenceDepth <= storedDepth ? 1 : 0
        // Shader 传入当前片元的光源空间深度作为 referenceDepth；返回 1 表示没有被挡住。
        // Linear 过滤会在硬件支持时对邻近比较结果插值，再叠加 Shader 的 3x3 PCF，
        // 从而把锯齿状硬边变成较平滑的阴影边缘。
        rhi::RHISamplerDesc shadowSamplerDesc{};
        shadowSamplerDesc.debugName = "PBR.ShadowComparisonSampler";
        shadowSamplerDesc.minFilter = rhi::RHIFilterMode::Linear;
        shadowSamplerDesc.magFilter = rhi::RHIFilterMode::Linear;
        shadowSamplerDesc.mipmapMode = rhi::RHIMipmapMode::Nearest;
        shadowSamplerDesc.addressU = rhi::RHIAddressMode::ClampToBorder;
        shadowSamplerDesc.addressV = rhi::RHIAddressMode::ClampToBorder;
        shadowSamplerDesc.addressW = rhi::RHIAddressMode::ClampToBorder;
        shadowSamplerDesc.maxLod = 0.0F;
        shadowSamplerDesc.enableCompare = true;
        shadowSamplerDesc.compareOp = rhi::RHICompareOp::LessOrEqual;
        // 光源视锥外采到边框深度 1.0；标准深度下它代表最远处，因此默认判定为受光。
        shadowSamplerDesc.borderColor = rhi::RHIBorderColor::OpaqueWhite;
        shadowSampler_ = device_->CreateSampler(shadowSamplerDesc);

        // 主 PBR BindSet 的资源契约必须与 Shader 完全一致：
        // binding 0 -> 每个物体各自的 UniformBuffer；
        // binding 1 -> 全场景共享的 Shadow Map + Comparison Sampler。
        // CombinedTextureSampler 在 Vulkan 对应 combined image sampler；D3D 后端会拆到
        // 同编号的 SRV(t1) 和 Sampler(s1)。
        rhi::RHIBindSetLayoutDesc bindLayoutDesc{};
        bindLayoutDesc.debugName = "PBR.BindSetLayout";
        bindLayoutDesc.set = 0;
        bindLayoutDesc.entries.push_back({
            0,
            rhi::RHIBindingType::UniformBuffer,
            rhi::RHIShaderStage::Vertex | rhi::RHIShaderStage::Fragment});

        rhi::RHIBindSetLayoutEntry shadowMapEntry{};
        shadowMapEntry.binding = 1;
        shadowMapEntry.type = rhi::RHIBindingType::CombinedTextureSampler;
        shadowMapEntry.visibility = rhi::RHIShaderStage::Fragment;
        shadowMapEntry.textureViewDimension = rhi::RHITextureViewDimension::View2D;
        shadowMapEntry.textureSampleType = rhi::RHITextureSampleType::Depth;
        bindLayoutDesc.entries.push_back(shadowMapEntry);
        rhi::RHIBindSetLayoutEntry pbrSkyboxEntry{};
        pbrSkyboxEntry.binding = 2;
        pbrSkyboxEntry.type = rhi::RHIBindingType::CombinedTextureSampler;
        pbrSkyboxEntry.visibility = rhi::RHIShaderStage::Fragment;
        pbrSkyboxEntry.textureViewDimension = rhi::RHITextureViewDimension::Cube;
        pbrSkyboxEntry.textureSampleType = rhi::RHITextureSampleType::Float;
        bindLayoutDesc.entries.push_back(pbrSkyboxEntry);
        bindSetLayout_ = device_->CreateBindSetLayout(bindLayoutDesc);

        sphereBindSet_ = CreatePBRBindSet("PBR.SphereBindSet", sphereUniformBuffer_);
        planeBindSet_ = CreatePBRBindSet("PBR.PlaneBindSet", planeUniformBuffer_);

        rhi::RHIPipelineLayoutDesc pipelineLayoutDesc{};
        pipelineLayoutDesc.debugName = "PBR.PipelineLayout";
        pipelineLayoutDesc.bindSetLayouts.push_back(bindSetLayout_);
        pipelineLayout_ = device_->CreatePipelineLayout(pipelineLayoutDesc);

        // Depth-only Shadow Pipeline 的布局只有 binding 0。它只需要 model 和
        // lightViewProjection，不会访问 binding 1 的 Shadow Map。
        rhi::RHIBindSetLayoutDesc shadowBindLayoutDesc{};
        shadowBindLayoutDesc.debugName = "PBR.ShadowBindSetLayout";
        shadowBindLayoutDesc.set = 0;
        shadowBindLayoutDesc.entries.push_back({
            0,
            rhi::RHIBindingType::UniformBuffer,
            rhi::RHIShaderStage::Vertex});
        shadowBindSetLayout_ = device_->CreateBindSetLayout(shadowBindLayoutDesc);
        shadowSphereBindSet_ = CreateShadowBindSet(
            "PBR.ShadowSphereBindSet",
            sphereUniformBuffer_);

        rhi::RHIPipelineLayoutDesc shadowPipelineLayoutDesc{};
        shadowPipelineLayoutDesc.debugName = "PBR.ShadowPipelineLayout";
        shadowPipelineLayoutDesc.bindSetLayouts.push_back(shadowBindSetLayout_);
        shadowPipelineLayout_ = device_->CreatePipelineLayout(
            shadowPipelineLayoutDesc);
    }

    /// 为一个物体绑定独立 UBO，并复用全场景的阴影图与环境 cubemap。
    rhi::RHIBindSet CreatePBRBindSet(const char* name, rhi::RHIBuffer buffer) {
        // 球和 Plane 各有自己的 UBO（model、材质不同），但共享同一张阴影图。
        // BindSet 把“这个 draw 实际使用哪些资源”与 Pipeline 的静态布局分离。
        rhi::RHIBindSetDesc desc{};
        desc.debugName = name;
        desc.layout = bindSetLayout_;

        rhi::RHIResourceBinding uniformBinding{};
        uniformBinding.binding = 0;
        uniformBinding.type = rhi::RHIBindingType::UniformBuffer;
        uniformBinding.buffer = {buffer, 0, sizeof(UniformBufferObject)};
        desc.bindings.push_back(uniformBinding);

        rhi::RHIResourceBinding shadowBinding{};
        shadowBinding.binding = 1;
        shadowBinding.type = rhi::RHIBindingType::CombinedTextureSampler;
        shadowBinding.texture = {shadowView_, shadowTexture_};
        shadowBinding.sampler = shadowSampler_;
        desc.bindings.push_back(shadowBinding);

        rhi::RHIResourceBinding skyboxBinding{};
        skyboxBinding.binding = 2;
        skyboxBinding.type = rhi::RHIBindingType::CombinedTextureSampler;
        skyboxBinding.texture = {skyboxView_, skyboxTexture_};
        skyboxBinding.sampler = skyboxSampler_;
        desc.bindings.push_back(skyboxBinding);
        return device_->CreateBindSet(desc);
    }

    /// 为 Shadow Pass 创建只包含物体 UBO 的绑定，避免边写边采样 shadowTexture_。
    rhi::RHIBindSet CreateShadowBindSet(
        const char* name,
        rhi::RHIBuffer buffer) {
        // Shadow Pass 当前只有球体充当 caster，所以只创建球体 BindSet。
        // Plane 是 receiver，不写入 Shadow Map；否则它只会写下自身平面深度，
        // 对“球是否挡住光线”的判断没有额外帮助，并增加一次无意义绘制。
        rhi::RHIBindSetDesc desc{};
        desc.debugName = name;
        desc.layout = shadowBindSetLayout_;

        rhi::RHIResourceBinding uniformBinding{};
        uniformBinding.binding = 0;
        uniformBinding.type = rhi::RHIBindingType::UniformBuffer;
        uniformBinding.buffer = {buffer, 0, sizeof(UniformBufferObject)};
        desc.bindings.push_back(uniformBinding);
        return device_->CreateBindSet(desc);
    }

    /// 查询当前 Win32 客户区尺寸；窗口最小化时允许返回 0x0。
    rhi::RHIExtent2D ClientExtent() const {
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        return {
            static_cast<rhi::u32>(std::max<LONG>(0, rectangle.right - rectangle.left)),
            static_cast<rhi::u32>(std::max<LONG>(0, rectangle.bottom - rectangle.top))};
    }

    /// 创建随窗口尺寸变化的 swapchain、主深度资源，并按需创建图形管线。
    void CreateSwapchainResources() {
        const rhi::RHIExtent2D extent = ClientExtent();
        if (extent.width == 0 || extent.height == 0) {
            return;
        }

        rhi::RHISwapchainDesc swapchainDesc{};
        swapchainDesc.debugName = "PBR.Swapchain";
        swapchainDesc.extent = extent;
        swapchainDesc.preferredFormat = rhi::RHIFormat::BGRA8_SRGB;
        swapchainDesc.presentMode = rhi::RHIPresentMode::FIFO;
        swapchainDesc.imageCount = FRAMES_IN_FLIGHT + 1;
        swapchain_ = device_->CreateSwapchain(swapchainDesc);
        swapchainImages_ = device_->GetSwapchainImages(swapchain_);
        swapchainFormat_ = device_->GetSwapchainFormat(swapchain_);
        swapchainExtent_ = device_->GetSwapchainExtent(swapchain_);

        rhi::RHITextureDesc depthDesc{};
        depthDesc.debugName = "PBR.Depth";
        depthDesc.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
        depthDesc.format = rhi::RHIFormat::D32_Float;
        depthDesc.usage = rhi::RHITextureUsage::DepthStencilAttachment;
        depthTexture_ = device_->CreateTexture(depthDesc);

        rhi::RHITextureViewDesc depthViewDesc{};
        depthViewDesc.debugName = "PBR.DepthView";
        depthViewDesc.texture = depthTexture_;
        depthViewDesc.format = depthDesc.format;
        depthViewDesc.aspect = rhi::RHITextureAspect::Depth;
        depthView_ = device_->CreateTextureView(depthViewDesc);

        if (!pipeline_) {
            CreatePipeline();
        }
    }

    /// 根据当前后端选择 SPIR-V/HLSL，并创建 PBR、Shadow 和 Skybox 三条管线。
    void CreatePipeline() {
        rhi::RHIShaderDesc vertexShader{};
        vertexShader.debugName = "PBR.VertexShader";
        vertexShader.stage = rhi::RHIShaderStage::Vertex;

        rhi::RHIShaderDesc fragmentShader{};
        fragmentShader.debugName = "PBR.FragmentShader";
        fragmentShader.stage = rhi::RHIShaderStage::Fragment;

        const std::string shaderDirectory = pbr_demo_config::SHADER_DIRECTORY;
        if (options_.api == rhi::RHIGraphicsAPI::Vulkan) {
            vertexShader.language = rhi::RHIShaderLanguage::SPIRV;
            vertexShader.filePath = shaderDirectory + "/pbr.vert.spv";
            fragmentShader.language = rhi::RHIShaderLanguage::SPIRV;
            fragmentShader.filePath = shaderDirectory + "/pbr.frag.spv";
        } else {
            // D3D11 与 D3D12 共用 HLSL 源码，但分别编译到各自合适的 shader model。
            // b0、POSITION/NORMAL/TEXCOORD 和 SV_POSITION/SV_TARGET 共同构成 D3D 管线契约。
            const bool d3d12 = options_.api == rhi::RHIGraphicsAPI::D3D12;
            vertexShader.language = rhi::RHIShaderLanguage::HLSL;
            vertexShader.filePath = shaderDirectory + "/pbr.hlsl";
            vertexShader.entryPoint = "VSMain";
            vertexShader.compileOptions.targetProfile = d3d12 ? "vs_5_1" : "vs_5_0";
            fragmentShader.language = rhi::RHIShaderLanguage::HLSL;
            fragmentShader.filePath = shaderDirectory + "/pbr.hlsl";
            fragmentShader.entryPoint = "PSMain";
            fragmentShader.compileOptions.targetProfile = d3d12 ? "ps_5_1" : "ps_5_0";
        }

        rhi::RHIVertexBufferLayoutDesc vertexLayout{};
        vertexLayout.binding = 0;
        vertexLayout.stride = sizeof(Vertex);
        vertexLayout.attributes = {
            {"POSITION", 0, 0, 0, rhi::RHIVertexFormat::Float32x3, offsetof(Vertex, position)},
            {"NORMAL", 0, 1, 0, rhi::RHIVertexFormat::Float32x3, offsetof(Vertex, normal)},
            {"TEXCOORD", 0, 2, 0, rhi::RHIVertexFormat::Float32x2, offsetof(Vertex, uv)}};

        rhi::RHIGraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.debugName = "PBR.GraphicsPipeline";
        pipelineDesc.layout = pipelineLayout_;
        pipelineDesc.shaders = {vertexShader, fragmentShader};
        pipelineDesc.vertexBuffers.push_back(vertexLayout);
        pipelineDesc.inputAssembly.topology = rhi::RHIPrimitiveTopology::TriangleList;
        pipelineDesc.raster.cullMode = rhi::RHICullMode::Back;
        pipelineDesc.raster.frontFace = rhi::RHIFrontFace::CounterClockwise;
        pipelineDesc.depthStencil.depthTestEnable = true;
        pipelineDesc.depthStencil.depthWriteEnable = true;
        pipelineDesc.depthStencil.depthCompareOp = rhi::RHICompareOp::Less;
        pipelineDesc.blend.attachments.push_back({});
        pipelineDesc.colorFormats.push_back(swapchainFormat_);
        pipelineDesc.depthStencilFormat = rhi::RHIFormat::D32_Float;
        pipeline_ = device_->CreateGraphicsPipeline(pipelineDesc);

        // Shadow Pipeline 只包含 Vertex Shader：顶点经过 model 和光源 VP 矩阵后，
        // 固定功能光栅器会自动生成片元深度并写入 D32 attachment。没有颜色输出，
        // 所以不需要 Fragment/Pixel Shader，也不声明 colorFormats。
        rhi::RHIShaderDesc shadowVertexShader{};
        shadowVertexShader.debugName = "PBR.ShadowVertexShader";
        shadowVertexShader.stage = rhi::RHIShaderStage::Vertex;
        if (options_.api == rhi::RHIGraphicsAPI::Vulkan) {
            shadowVertexShader.language = rhi::RHIShaderLanguage::SPIRV;
            shadowVertexShader.filePath = shaderDirectory + "/shadow.vert.spv";
        } else {
            const bool d3d12 = options_.api == rhi::RHIGraphicsAPI::D3D12;
            shadowVertexShader.language = rhi::RHIShaderLanguage::HLSL;
            shadowVertexShader.filePath = shaderDirectory + "/pbr.hlsl";
            shadowVertexShader.entryPoint = "ShadowVSMain";
            shadowVertexShader.compileOptions.targetProfile = d3d12 ? "vs_5_1" : "vs_5_0";
        }

        // Shadow Shader 只读取 POSITION，但 stride 仍是 sizeof(Vertex)：
        // position/normal/uv 在同一交错顶点中，下一顶点仍需跨过完整 Vertex。
        // 省略 NORMAL 和 UV attribute 只会减少输入布局声明，不会改变内存步长。
        rhi::RHIVertexBufferLayoutDesc shadowVertexLayout{};
        shadowVertexLayout.binding = 0;
        shadowVertexLayout.stride = sizeof(Vertex);
        shadowVertexLayout.attributes = {
            {"POSITION", 0, 0, 0, rhi::RHIVertexFormat::Float32x3, offsetof(Vertex, position)}};

        rhi::RHIGraphicsPipelineDesc shadowPipelineDesc{};
        shadowPipelineDesc.debugName = "PBR.ShadowGraphicsPipeline";
        shadowPipelineDesc.layout = shadowPipelineLayout_;
        shadowPipelineDesc.shaders = {shadowVertexShader};
        shadowPipelineDesc.vertexBuffers.push_back(shadowVertexLayout);
        shadowPipelineDesc.inputAssembly.topology = rhi::RHIPrimitiveTopology::TriangleList;
        shadowPipelineDesc.raster.cullMode = rhi::RHICullMode::Back;
        shadowPipelineDesc.raster.frontFace = rhi::RHIFrontFace::CounterClockwise;
        // Shadow acne 的来源：有限深度精度和光栅化采样位置会让接收面与自己写入的深度
        // 略有误差，从而被错误判断为“自己遮挡自己”。Raster Depth Bias 将 caster
        // 写入的深度轻微推远；Fragment Shader 还会在比较前使用法线相关 bias。
        // bias 太小会出现条纹，太大则会产生阴影与物体分离的 Peter-panning。
        shadowPipelineDesc.raster.depthBiasEnable = true;
        shadowPipelineDesc.raster.depthBiasConstantFactor = 1.0F;
        shadowPipelineDesc.raster.depthBiasSlopeFactor = 1.5F;
        shadowPipelineDesc.depthStencil.depthTestEnable = true;
        shadowPipelineDesc.depthStencil.depthWriteEnable = true;
        shadowPipelineDesc.depthStencil.depthCompareOp = rhi::RHICompareOp::Less;
        shadowPipelineDesc.depthStencilFormat = rhi::RHIFormat::D32_Float;
        shadowPipeline_ = device_->CreateGraphicsPipeline(shadowPipelineDesc);

        // Skybox 复用球体 POSITION 作为方向。Vertex Shader 把深度固定在远平面，
        // LessOrEqual 且关闭深度写入，使它只填充尚未被场景几何覆盖的背景像素。
        rhi::RHIShaderDesc skyboxVertexShader{};
        skyboxVertexShader.debugName = "PBR.SkyboxVertexShader";
        skyboxVertexShader.stage = rhi::RHIShaderStage::Vertex;
        rhi::RHIShaderDesc skyboxFragmentShader{};
        skyboxFragmentShader.debugName = "PBR.SkyboxFragmentShader";
        skyboxFragmentShader.stage = rhi::RHIShaderStage::Fragment;
        if (options_.api == rhi::RHIGraphicsAPI::Vulkan) {
            skyboxVertexShader.language = rhi::RHIShaderLanguage::SPIRV;
            skyboxVertexShader.filePath = shaderDirectory + "/skybox.vert.spv";
            skyboxFragmentShader.language = rhi::RHIShaderLanguage::SPIRV;
            skyboxFragmentShader.filePath = shaderDirectory + "/skybox.frag.spv";
        } else {
            const bool d3d12 = options_.api == rhi::RHIGraphicsAPI::D3D12;
            skyboxVertexShader.language = rhi::RHIShaderLanguage::HLSL;
            skyboxVertexShader.filePath = shaderDirectory + "/pbr.hlsl";
            skyboxVertexShader.entryPoint = "SkyboxVSMain";
            skyboxVertexShader.compileOptions.targetProfile = d3d12 ? "vs_5_1" : "vs_5_0";
            skyboxFragmentShader.language = rhi::RHIShaderLanguage::HLSL;
            skyboxFragmentShader.filePath = shaderDirectory + "/pbr.hlsl";
            skyboxFragmentShader.entryPoint = "SkyboxPSMain";
            skyboxFragmentShader.compileOptions.targetProfile = d3d12 ? "ps_5_1" : "ps_5_0";
        }

        rhi::RHIGraphicsPipelineDesc skyboxPipelineDesc{};
        skyboxPipelineDesc.debugName = "PBR.SkyboxGraphicsPipeline";
        skyboxPipelineDesc.layout = skyboxPipelineLayout_;
        skyboxPipelineDesc.shaders = {skyboxVertexShader, skyboxFragmentShader};
        skyboxPipelineDesc.vertexBuffers.push_back(shadowVertexLayout);
        skyboxPipelineDesc.inputAssembly.topology = rhi::RHIPrimitiveTopology::TriangleList;
        skyboxPipelineDesc.raster.cullMode = rhi::RHICullMode::None;
        skyboxPipelineDesc.raster.frontFace = rhi::RHIFrontFace::CounterClockwise;
        skyboxPipelineDesc.depthStencil.depthTestEnable = true;
        skyboxPipelineDesc.depthStencil.depthWriteEnable = false;
        skyboxPipelineDesc.depthStencil.depthCompareOp = rhi::RHICompareOp::LessOrEqual;
        skyboxPipelineDesc.blend.attachments.push_back({});
        skyboxPipelineDesc.colorFormats.push_back(swapchainFormat_);
        skyboxPipelineDesc.depthStencilFormat = rhi::RHIFormat::D32_Float;
        skyboxPipeline_ = device_->CreateGraphicsPipeline(skyboxPipelineDesc);
    }

    /// 等待旧呈现资源空闲后重建窗口尺寸相关资源；静态场景资源保持不变。
    void RecreateSwapchain() {
        const rhi::RHIExtent2D extent = ClientExtent();
        if (extent.width == 0 || extent.height == 0) {
            return;
        }
        device_->WaitIdle();
        device_->Destroy(depthView_);
        device_->Destroy(depthTexture_);
        device_->Destroy(swapchain_);
        depthView_ = {};
        depthTexture_ = {};
        swapchain_ = {};
        swapchainImages_.clear();
        CreateSwapchainResources();
        framebufferResized_ = false;
    }

    /// 为球体或地面生成当前帧的相机、光源、阴影和材质常量。
    UniformBufferObject MakeUniform(bool sphere) const {
        const float time = std::chrono::duration<float>(
                               std::chrono::steady_clock::now() - startTime_)
                               .count();
        const float3 eye{0.0F, 3.0F, 6.0F};

        UniformBufferObject uniform{};
        uniform.view = ToShaderMatrix(LookAtRH(
            eye,
            float3{0.0F, 0.8F, 0.0F},
            float3{0.0F, 1.0F, 0.0F}));
        float4x4 projection = PerspectiveRH_ZO(
            math::Radians(45.0F),
            static_cast<float>(swapchainExtent_.width) /
                static_cast<float>(swapchainExtent_.height),
            0.1F,
            100.0F);
        // RH ZO 投影与 D3D 的 viewport Y 方向可直接配合；Vulkan 的正高度 viewport
        // 需要翻转 clip-space Y。若 D3D 也翻转，画面会上下颠倒且三角形屏幕绕序反转。
        if (options_.api == rhi::RHIGraphicsAPI::Vulkan) {
            projection[1][1] *= -1.0F;
        }
        uniform.projection = ToShaderMatrix(projection);
        const float3 normalizedLightDirection = Normalize(float3{-0.5F, -1.0F, -0.3F});
        uniform.lightDirection = {
            normalizedLightDirection.x,
            normalizedLightDirection.y,
            normalizedLightDirection.z,
            0.0F};
        uniform.lightColor = {1.0F, 0.96F, 0.90F, 1.0F};
        uniform.cameraPosition = {eye.x, eye.y, eye.z, 1.0F};

        // 方向光没有真实位置，所有光线互相平行。为了生成 Shadow Map，仍需构造一个
        // “虚拟光源相机”：把它放在光线传播方向的反方向，并朝场景中心观察。
        //
        // lightPosition = target - lightDirection * distance
        // 因为 lightDirection 指向光线前进方向，减去它才会回到光线来源一侧。
        const float3 lightDirection{
            uniform.lightDirection.x,
            uniform.lightDirection.y,
            uniform.lightDirection.z};
        const float3 shadowTarget{0.0F, 0.5F, 0.0F};
        const float3 lightPosition = shadowTarget - lightDirection * 8.0F;
        const float4x4 lightView = LookAtRH(
            lightPosition,
            shadowTarget,
            float3{0.0F, 1.0F, 0.0F});

        // 方向光没有“近大远小”，所以使用正交投影而不是透视投影。
        // left/right/bottom/top 决定 Shadow Map 覆盖的世界区域；范围过大时，每个 texel
        // 覆盖更多世界空间，阴影会变糊；范围过小时，范围外物体不会进入阴影图。
        // near/far 决定光源方向上的可记录深度范围，也应尽量贴合场景以提高精度。
        float4x4 lightProjection = OrthographicRH_ZO(
            -5.0F,
            5.0F,
            -5.0F,
            5.0F,
            0.1F,
            16.0F);

        // 主相机和光源相机必须采用相同的后端 Y 约定。这里为 Vulkan 翻转光源投影 Y，
        // 因此 GLSL 查询阴影时可以直接执行 ndc.xy * 0.5 + 0.5；D3D 不翻矩阵，
        // 转而在 HLSL 计算 shadowUV 时翻转 Y。
        if (options_.api == rhi::RHIGraphicsAPI::Vulkan) {
            lightProjection[1][1] *= -1.0F;
        }

        // 顶点先 world = model * local，再 lightClip = lightVP * world。
        // model 因物体而异，lightVP 对同一个方向光覆盖的所有物体相同。
        uniform.lightViewProjection = ToShaderMatrix(lightProjection * lightView);

        // PCF 每次偏移一个 texel，因此把 1 / resolution 传给 Shader，避免硬编码。
        // z/w 是经过实际画面调节的比较 bias，单位是归一化光源深度 [0, 1]。
        uniform.shadowParameters = {
            1.0F / static_cast<float>(SHADOW_MAP_SIZE),
            1.0F / static_cast<float>(SHADOW_MAP_SIZE),
            0.00035F,
            0.0025F};

        if (sphere) {
            uniform.model = ToShaderMatrix(
                TranslationMatrix(float3{0.0F, 1.0F, 0.0F}) *
                RotationYMatrix(time * math::Radians(45.0F)));
            uniform.baseColor = {0.85F, 0.55F, 0.25F, 0.85F};
            uniform.materialParameters = {0.35F, 1.0F, 0.0F, 0.0F};
        } else {
            uniform.model = ToShaderMatrix(float4x4{1.0F});
            uniform.baseColor = {0.45F, 0.45F, 0.50F, 0.0F};
            uniform.materialParameters = {0.85F, 1.0F, 0.0F, 0.0F};
        }
        return uniform;
    }

    /// 将资源上传、RenderGraph、draw workload、队列同步和 present 组装为一帧 packet。
    rhi::RHIFramePacket BuildFrame(rhi::u32 imageIndex) {
        rhi::RHIFramePacket packet{};
        packet.settings.drawableSize = swapchainExtent_;
        packet.settings.viewport = {
            0.0F,
            0.0F,
            static_cast<float>(swapchainExtent_.width),
            static_cast<float>(swapchainExtent_.height),
            0.0F,
            1.0F};
        packet.settings.scissor = {{0, 0}, swapchainExtent_};
        packet.settings.frameIndex = frameIndex_;
        packet.settings.maxFramesInFlight = FRAMES_IN_FLIGHT;

        // 静态几何和六面 cubemap 只上传一次；UBO 则因动画和相机参数每帧更新。
        if (staticUploadsPending_) {
            packet.uploads.buffers.push_back({vertexBuffer_, 0, initialVertexData_});
            packet.uploads.buffers.push_back({indexBuffer_, 0, initialIndexData_});
            for (rhi::u32 face = 0; face < initialSkyboxFaceData_.size(); ++face) {
                rhi::RHITextureUploadDesc upload{};
                upload.destination = skyboxTexture_;
                upload.arrayLayer = face;
                upload.extent = {skyboxWidth_, skyboxHeight_, 1};
                upload.data = initialSkyboxFaceData_[face];
                packet.uploads.textures.push_back(std::move(upload));
            }
        }
        packet.uploads.buffers.push_back(
            {sphereUniformBuffer_, 0, ToBytes(MakeUniform(true))});
        packet.uploads.buffers.push_back(
            {planeUniformBuffer_, 0, ToBytes(MakeUniform(false))});

        // RenderGraph 只引用长期存在的外部 RHI 句柄，不接管这些 buffer 的生命周期。
        const auto importBuffer = [&](
            const char* name,
            rhi::RHIBuffer handle,
            rhi::u64 size,
            rhi::RHIBufferUsage usage) {
            rhi::RHIRenderGraphBufferDesc resource{};
            resource.name = name;
            resource.imported = true;
            resource.flags = rhi::RHIRenderGraphResourceFlags::Imported;
            resource.externalHandle = handle;
            resource.desc.size = size;
            resource.desc.usage = usage;
            packet.graph.buffers.push_back(resource);
        };
        importBuffer("Vertices", vertexBuffer_, initialVertexData_.size(), rhi::RHIBufferUsage::Vertex);
        importBuffer("Indices", indexBuffer_, initialIndexData_.size(), rhi::RHIBufferUsage::Index);
        importBuffer("SphereUniform", sphereUniformBuffer_, sizeof(UniformBufferObject), rhi::RHIBufferUsage::Uniform);
        importBuffer("PlaneUniform", planeUniformBuffer_, sizeof(UniformBufferObject), rhi::RHIBufferUsage::Uniform);

        // swapchain image 和主深度均由图外创建，本帧只声明它们的用途和初始状态。
        rhi::RHIRenderGraphTextureDesc backBuffer{};
        backBuffer.name = "BackBuffer";
        backBuffer.imported = true;
        backBuffer.flags = rhi::RHIRenderGraphResourceFlags::Imported;
        backBuffer.externalHandle = swapchainImages_[imageIndex];
        backBuffer.desc.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
        backBuffer.desc.format = swapchainFormat_;
        backBuffer.desc.usage = rhi::RHITextureUsage::ColorAttachment |
                                rhi::RHITextureUsage::Present;
        backBuffer.desc.initialState = rhi::RHIResourceState::Present;
        packet.graph.textures.push_back(backBuffer);

        rhi::RHIRenderGraphTextureDesc depth{};
        depth.name = "Depth";
        depth.imported = true;
        depth.flags = rhi::RHIRenderGraphResourceFlags::Imported;
        depth.externalHandle = depthTexture_;
        depth.desc.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
        depth.desc.format = rhi::RHIFormat::D32_Float;
        depth.desc.usage = rhi::RHITextureUsage::DepthStencilAttachment;
        packet.graph.textures.push_back(depth);

        // shadowTexture_ 在图外长期创建，所以作为 Imported 资源交给 RenderGraph。
        // RenderGraph 不拥有它的生命周期，但会追踪本帧内的状态和访问顺序。
        // 同一资源同时声明 DepthStencilAttachment 与 Sampled，正好对应先写后读两种用途。
        rhi::RHIRenderGraphTextureDesc shadowDepth{};
        shadowDepth.name = "ShadowDepth";
        shadowDepth.imported = true;
        shadowDepth.flags = rhi::RHIRenderGraphResourceFlags::Imported;
        shadowDepth.externalHandle = shadowTexture_;
        shadowDepth.desc.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
        shadowDepth.desc.format = rhi::RHIFormat::D32_Float;
        shadowDepth.desc.usage = rhi::RHITextureUsage::DepthStencilAttachment |
                                 rhi::RHITextureUsage::Sampled;
        packet.graph.textures.push_back(shadowDepth);

        // Cubemap 本帧仅在 Fragment Shader 中读取；首帧上传由 packet.uploads 先行完成。
        rhi::RHIRenderGraphTextureDesc skybox{};
        skybox.name = "SkyboxCube";
        skybox.imported = true;
        skybox.flags = rhi::RHIRenderGraphResourceFlags::Imported;
        skybox.externalHandle = skyboxTexture_;
        skybox.desc.dimension = rhi::RHITextureDimension::Texture2D;
        skybox.desc.extent = {skyboxWidth_, skyboxHeight_, 1};
        skybox.desc.arrayLayers = 6;
        skybox.desc.format = rhi::RHIFormat::RGBA8_SRGB;
        skybox.desc.usage = rhi::RHITextureUsage::Sampled |
                            rhi::RHITextureUsage::TransferDestination;
        skybox.desc.flags = rhi::RHITextureCreateFlags::CubeCompatible;
        packet.graph.textures.push_back(skybox);

        // -----------------------------------------------------------------
        // Pass 1：从光源视角生成 Shadow Map。
        // -----------------------------------------------------------------
        // reads 声明顶点、索引和球体 UBO 的读取阶段；depth attachment 会被编译器
        // 自动视为对 ShadowDepth 的 DepthWrite。这里只画 sphere，所以 sphere 是 caster。
        rhi::RHIRenderGraphPassDesc shadowPass{};
        shadowPass.name = "ShadowMap";
        shadowPass.type = rhi::RHIRenderGraphPassType::Raster;
        shadowPass.reads = {
            {"Vertices", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::VertexBuffer, rhi::RHIPipelineStage::VertexInput},
            {"Indices", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::IndexBuffer, rhi::RHIPipelineStage::VertexInput},
            {"SphereUniform", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::ConstantBuffer, rhi::RHIPipelineStage::VertexShader}};

        rhi::RHIRenderGraphAttachmentDesc shadowAttachment{};
        shadowAttachment.resourceName = "ShadowDepth";
        shadowAttachment.aspect = rhi::RHITextureAspect::Depth;
        // 每帧都从 1.0（最远深度）开始。Clear 避免保留上一帧球体位置造成残影。
        shadowAttachment.loadOp = rhi::RHILoadOp::Clear;
        // 后续 OpaquePBR 要采样结果，必须 Store；Discard 会允许后端丢掉深度内容。
        shadowAttachment.storeOp = rhi::RHIStoreOp::Store;
        shadowAttachment.clearValue.depthStencil = {1.0F, 0};
        shadowPass.depthStencilAttachment = shadowAttachment;
        packet.graph.passes.push_back(shadowPass);

        // -----------------------------------------------------------------
        // Pass 2：主相机 PBR 绘制。
        // -----------------------------------------------------------------
        // 对 ShadowDepth 声明 FragmentShader/ShaderRead 后，RenderGraph 能从同一资源的
        // DepthWrite -> ShaderRead 自动推导：
        // - ShadowMap 必须先于 OpaquePBR；
        // - Vulkan 插入 image layout/access barrier；
        // - D3D12 插入 DEPTH_WRITE -> PIXEL_SHADER_RESOURCE transition；
        // - D3D11 虽无显式 barrier，也会按编译后的 Pass 顺序解绑 DSV 再绑定 SRV。
        rhi::RHIRenderGraphPassDesc opaque{};
        opaque.name = "OpaquePBR";
        opaque.type = rhi::RHIRenderGraphPassType::Raster;
        opaque.reads = {
            {"Vertices", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::VertexBuffer, rhi::RHIPipelineStage::VertexInput},
            {"Indices", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::IndexBuffer, rhi::RHIPipelineStage::VertexInput},
            {"SphereUniform", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::ConstantBuffer, rhi::RHIPipelineStage::VertexShader | rhi::RHIPipelineStage::FragmentShader},
            {"PlaneUniform", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::ConstantBuffer, rhi::RHIPipelineStage::VertexShader | rhi::RHIPipelineStage::FragmentShader},
            {"ShadowDepth", rhi::RHIRenderGraphResourceType::Texture, rhi::RHIResourceState::ShaderRead, rhi::RHIPipelineStage::FragmentShader},
            {"SkyboxCube", rhi::RHIRenderGraphResourceType::Texture, rhi::RHIResourceState::ShaderRead, rhi::RHIPipelineStage::FragmentShader}};

        rhi::RHIRenderGraphAttachmentDesc colorAttachment{};
        colorAttachment.resourceName = "BackBuffer";
        colorAttachment.loadOp = rhi::RHILoadOp::Clear;
        colorAttachment.storeOp = rhi::RHIStoreOp::Store;
        colorAttachment.clearValue.color = {0.02F, 0.02F, 0.02F, 1.0F};
        opaque.colorAttachments.push_back(colorAttachment);

        rhi::RHIRenderGraphAttachmentDesc depthAttachment{};
        depthAttachment.resourceName = "Depth";
        depthAttachment.aspect = rhi::RHITextureAspect::Depth;
        depthAttachment.loadOp = rhi::RHILoadOp::Clear;
        depthAttachment.storeOp = rhi::RHIStoreOp::Store;
        depthAttachment.clearValue.depthStencil = {1.0F, 0};
        opaque.depthStencilAttachment = depthAttachment;
        packet.graph.passes.push_back(opaque);

        // Present Pass 把 BackBuffer 从 color attachment 状态转换回呈现状态。
        rhi::RHIRenderGraphPassDesc presentPass{};
        presentPass.name = "Present";
        presentPass.type = rhi::RHIRenderGraphPassType::Present;
        presentPass.queue = rhi::RHIQueueType::Present;
        presentPass.reads.push_back({
            "BackBuffer",
            rhi::RHIRenderGraphResourceType::SwapchainImage,
            rhi::RHIResourceState::Present,
            rhi::RHIPipelineStage::BottomOfPipe});
        packet.graph.passes.push_back(presentPass);

        // PassDesc 只描述依赖和 attachment；Workload 才保存真正的 draw command。
        // Shadow Pass 必须使用 Shadow Map 自己的 2048x2048 viewport/scissor，不能沿用窗口
        // 尺寸，否则只会写入深度图的一部分，或者产生错误的 texel 到像素映射。
        rhi::RHIRenderPassWorkload shadowWorkload{};
        shadowWorkload.passName = "ShadowMap";
        shadowWorkload.viewport = {
            0.0F,
            0.0F,
            static_cast<float>(SHADOW_MAP_SIZE),
            static_cast<float>(SHADOW_MAP_SIZE),
            0.0F,
            1.0F};
        shadowWorkload.scissor = {{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};

        // Shadow draw 复用主场景的 vertex/index buffer，但切换为 depth-only Pipeline 和
        // 只含 UBO 的 BindSet。索引范围只覆盖球体，Plane 不作为 caster 绘制。
        rhi::RHIDrawIndexedCommand shadowSphereDraw{};
        shadowSphereDraw.pipeline = shadowPipeline_;
        shadowSphereDraw.bindSets = {shadowSphereBindSet_};
        shadowSphereDraw.vertexStreams = {{vertexBuffer_, 0, 0, sizeof(Vertex)}};
        shadowSphereDraw.indexStream.buffer = indexBuffer_;
        shadowSphereDraw.indexStream.indexType = rhi::RHIIndexType::UInt32;
        shadowSphereDraw.indexStream.offset = sphereIndexOffset_;
        shadowSphereDraw.indexStream.indexCount = sphereIndexCount_;
        shadowSphereDraw.indexCount = sphereIndexCount_;
        shadowWorkload.indexedDraws.push_back(shadowSphereDraw);
        packet.workloads.push_back(std::move(shadowWorkload));

        // 主 workload 依次绘制球、地面和背景。Skybox 不写深度，因此放在最后不会覆盖物体。
        rhi::RHIRenderPassWorkload opaqueWorkload{};
        opaqueWorkload.passName = "OpaquePBR";
        opaqueWorkload.viewport = packet.settings.viewport;
        opaqueWorkload.scissor = packet.settings.scissor;

        rhi::RHIDrawIndexedCommand sphereDraw{};
        sphereDraw.pipeline = pipeline_;
        sphereDraw.bindSets = {sphereBindSet_};
        sphereDraw.vertexStreams = {{vertexBuffer_, 0, 0, sizeof(Vertex)}};
        sphereDraw.indexStream.buffer = indexBuffer_;
        sphereDraw.indexStream.indexType = rhi::RHIIndexType::UInt32;
        sphereDraw.indexStream.offset = sphereIndexOffset_;
        sphereDraw.indexStream.indexCount = sphereIndexCount_;
        sphereDraw.indexCount = sphereIndexCount_;
        opaqueWorkload.indexedDraws.push_back(sphereDraw);

        rhi::RHIDrawIndexedCommand planeDraw{};
        planeDraw.pipeline = pipeline_;
        planeDraw.bindSets = {planeBindSet_};
        planeDraw.vertexStreams = {{vertexBuffer_, 0, 0, sizeof(Vertex)}};
        planeDraw.indexStream.buffer = indexBuffer_;
        planeDraw.indexStream.indexType = rhi::RHIIndexType::UInt32;
        planeDraw.indexStream.offset = planeIndexOffset_;
        planeDraw.indexStream.indexCount = planeIndexCount_;
        planeDraw.indexCount = planeIndexCount_;
        planeDraw.vertexOffsetElements = static_cast<rhi::i32>(sphereVertexCount_);
        opaqueWorkload.indexedDraws.push_back(planeDraw);

        rhi::RHIDrawIndexedCommand skyboxDraw{};
        skyboxDraw.pipeline = skyboxPipeline_;
        skyboxDraw.bindSets = {skyboxBindSet_};
        skyboxDraw.vertexStreams = {{vertexBuffer_, 0, 0, sizeof(Vertex)}};
        skyboxDraw.indexStream.buffer = indexBuffer_;
        skyboxDraw.indexStream.indexType = rhi::RHIIndexType::UInt32;
        skyboxDraw.indexStream.offset = sphereIndexOffset_;
        skyboxDraw.indexStream.indexCount = sphereIndexCount_;
        skyboxDraw.indexCount = sphereIndexCount_;
        opaqueWorkload.indexedDraws.push_back(skyboxDraw);
        packet.workloads.push_back(std::move(opaqueWorkload));

        // 三个 Pass 放在同一次 Graphics Queue submission 中。passNames 的顺序还会接受
        // RenderGraph 依赖验证，防止调用方把消费者 OpaquePBR 提交到生产者 ShadowMap 前面。
        rhi::RHIQueueSubmitDesc submit{};
        submit.debugName = "PBR.RenderGraphSubmit";
        submit.queue = rhi::RHIQueueType::Graphics;
        submit.passNames = {"ShadowMap", "OpaquePBR", "Present"};
        submit.waits.push_back({
            imageAvailable_[frameSlot_],
            0,
            rhi::RHIPipelineStage::ColorAttachmentOutput});
        submit.signals.push_back({renderFinished_[frameSlot_], 0});
        packet.submissions.push_back(submit);

        // acquire signal 保证 backbuffer 可写，render-finished signal 保证 present 只读取完成帧。
        packet.present = rhi::RHIPresentDesc{
            swapchain_,
            imageIndex,
            {renderFinished_[frameSlot_]},
            rhi::RHIPresentMode::FIFO,
            false};
        return packet;
    }

    /// 获取一张 swapchain image，提交 RenderGraph 帧，并推进同步槽位和累计帧号。
    void DrawFrame() {
        if (!swapchain_) {
            return;
        }
        rhi::u32 imageIndex = 0;
        std::string error;
        if (!device_->AcquireNextImage(
                swapchain_,
                imageAvailable_[frameSlot_],
                {},
                &imageIndex,
                &error)) {
            framebufferResized_ = true;
            return;
        }

        const rhi::RHIFramePacket packet = BuildFrame(imageIndex);
        if (!device_->SubmitFrame(packet, &error)) {
            if (framebufferResized_) {
                return;
            }
            throw std::runtime_error("RenderGraph frame submission failed: " + error);
        }
        staticUploadsPending_ = false;
        frameSlot_ = (frameSlot_ + 1) % FRAMES_IN_FLIGHT;
        ++frameIndex_;
    }

    /// 处理 Win32 消息、最小化等待、swapchain 重建和逐帧渲染。
    void MainLoop() {
        MSG message{};
        while (running_) {
            while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
                if (message.message == WM_QUIT) {
                    running_ = false;
                    break;
                }
                TranslateMessage(&message);
                DispatchMessage(&message);
            }
            if (!running_) {
                break;
            }
            const rhi::RHIExtent2D extent = ClientExtent();
            if (extent.width == 0 || extent.height == 0) {
                Sleep(16);
                continue;
            }
            if (framebufferResized_) {
                RecreateSwapchain();
            }
            DrawFrame();
            if (options_.maxFrames != 0 && frameIndex_ >= options_.maxFrames) {
                running_ = false;
            }
        }
    }

    /// 等待 GPU 空闲，并按“使用者先于被引用资源”的逆依赖顺序销毁对象。
    void Cleanup() noexcept {
        if (device_ == nullptr) {
            return;
        }
        device_->WaitIdle();
        // 销毁顺序与引用关系相反：先销毁使用资源的 Pipeline/BindSet/Layout，再销毁
        // Sampler/View/Texture。WaitIdle 保证 GPU 不再访问这些对象。
        device_->Destroy(pipeline_);
        device_->Destroy(shadowPipeline_);
        device_->Destroy(skyboxPipeline_);
        device_->Destroy(pipelineLayout_);
        device_->Destroy(shadowPipelineLayout_);
        device_->Destroy(skyboxPipelineLayout_);
        device_->Destroy(sphereBindSet_);
        device_->Destroy(planeBindSet_);
        device_->Destroy(shadowSphereBindSet_);
        device_->Destroy(skyboxBindSet_);
        device_->Destroy(bindSetLayout_);
        device_->Destroy(shadowBindSetLayout_);
        device_->Destroy(skyboxBindSetLayout_);
        device_->Destroy(shadowSampler_);
        device_->Destroy(shadowView_);
        device_->Destroy(shadowTexture_);
        device_->Destroy(skyboxSampler_);
        device_->Destroy(skyboxView_);
        device_->Destroy(skyboxTexture_);
        device_->Destroy(sphereUniformBuffer_);
        device_->Destroy(planeUniformBuffer_);
        device_->Destroy(indexBuffer_);
        device_->Destroy(vertexBuffer_);
        device_->Destroy(depthView_);
        device_->Destroy(depthTexture_);
        device_->Destroy(swapchain_);
        for (rhi::u32 index = 0; index < FRAMES_IN_FLIGHT; ++index) {
            device_->Destroy(imageAvailable_[index]);
            device_->Destroy(renderFinished_[index]);
        }
        device_->Shutdown();
    }
};

} // namespace

/// Windows GUI 入口：初始化 COM、运行 Demo，并将异常显示为消息框。
int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR commandLine, int) {
    try {
        const ComApartment comApartment;
        PBRDemoApp app(ParseOptions(commandLine != nullptr ? commandLine : ""));
        app.Run(instance);
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        MessageBoxA(nullptr, exception.what(), "RHI PBR Demo", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}
