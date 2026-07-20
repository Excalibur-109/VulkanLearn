#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "PBRDemoConfig.hpp"
#include "RHI.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
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

constexpr rhi::u32 WINDOW_WIDTH = 1280;
constexpr rhi::u32 WINDOW_HEIGHT = 800;
constexpr rhi::u32 FRAMES_IN_FLIGHT = 2;
constexpr rhi::u32 SHADOW_MAP_SIZE = 2048;
constexpr float PI = 3.14159265359F;

struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};
};

struct alignas(16) UniformBufferObject {
    glm::mat4 model{1.0F};
    glm::mat4 view{1.0F};
    glm::mat4 projection{1.0F};
    glm::vec4 lightDirection{};
    glm::vec4 lightColor{};
    glm::vec4 cameraPosition{};
    glm::vec4 baseColor{};
    glm::vec4 materialParameters{};
    glm::mat4 lightViewProjection{1.0F};
    // xy = one shadow texel in UV space, z = minimum bias, w = slope bias.
    glm::vec4 shadowParameters{};
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<rhi::u32> indices;
};

struct DemoOptions {
    rhi::RHIGraphicsAPI api = rhi::RHIGraphicsAPI::Vulkan;
    rhi::u64 maxFrames = 0;
};

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
                options.api = rhi::RHIGraphicsAPI::Direct3D11;
            } else if (value == "d3d12") {
                options.api = rhi::RHIGraphicsAPI::Direct3D12;
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

const wchar_t* ApiDisplayName(rhi::RHIGraphicsAPI api) noexcept {
    switch (api) {
    case rhi::RHIGraphicsAPI::Vulkan:     return L"Vulkan";
    case rhi::RHIGraphicsAPI::Direct3D11: return L"Direct3D 11";
    case rhi::RHIGraphicsAPI::Direct3D12: return L"Direct3D 12";
    default:                              return L"Unknown";
    }
}

Mesh MakePlane(float halfSize = 4.0F) {
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

Mesh MakeSphere(rhi::u32 rings, rhi::u32 segments, float radius = 1.0F) {
    Mesh mesh{};
    mesh.vertices.reserve((rings + 1) * (segments + 1));
    mesh.indices.reserve(rings * segments * 6);

    for (rhi::u32 ring = 0; ring <= rings; ++ring) {
        for (rhi::u32 segment = 0; segment <= segments; ++segment) {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float v = static_cast<float>(ring) / static_cast<float>(rings);
            const float theta = u * 2.0F * PI;
            const float phi = v * PI;
            const float sinPhi = std::sin(phi);
            const glm::vec3 normal{
                -std::cos(theta) * sinPhi,
                std::cos(phi),
                std::sin(theta) * sinPhi};
            mesh.vertices.push_back(Vertex{normal * radius, normal, {u, v}});
        }
    }

    for (rhi::u32 ring = 0; ring < rings; ++ring) {
        for (rhi::u32 segment = 0; segment < segments; ++segment) {
            const rhi::u32 row0 = ring * (segments + 1);
            const rhi::u32 row1 = (ring + 1) * (segments + 1);
            const rhi::u32 a = row0 + segment;
            const rhi::u32 b = row1 + segment;
            const rhi::u32 c = row1 + segment + 1;
            const rhi::u32 d = row0 + segment + 1;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, a, c, d});
        }
    }
    return mesh;
}

template <typename Type>
std::vector<std::byte> ToBytes(const std::vector<Type>& values) {
    std::vector<std::byte> result(values.size() * sizeof(Type));
    if (!result.empty()) {
        std::memcpy(result.data(), values.data(), result.size());
    }
    return result;
}

template <typename Type>
std::vector<std::byte> ToBytes(const Type& value) {
    std::vector<std::byte> result(sizeof(Type));
    std::memcpy(result.data(), &value, sizeof(Type));
    return result;
}

class PBRDemoApp {
public:
    explicit PBRDemoApp(DemoOptions options)
        : options_(options) {
    }

    void Run(HINSTANCE instance) {
        CreateWindowHandle(instance);
        CreateDevice(instance);
        CreateStaticResources();
        CreateSwapchainResources();
        MainLoop();
        Cleanup();
    }

private:
    DemoOptions options_{};
    HWND window_ = nullptr;
    bool running_ = true;
    bool framebufferResized_ = false;

    std::unique_ptr<rhi::RHIDevice> device_;
    rhi::RHISwapchain swapchain_{};
    std::vector<rhi::RHITexture> swapchainImages_;
    rhi::RHIFormat swapchainFormat_ = rhi::RHIFormat::Undefined;
    rhi::RHIExtent2D swapchainExtent_{};
    rhi::RHITexture depthTexture_{};
    rhi::RHITextureView depthView_{};

    rhi::RHIBuffer vertexBuffer_{};
    rhi::RHIBuffer indexBuffer_{};
    rhi::RHIBuffer sphereUniformBuffer_{};
    rhi::RHIBuffer planeUniformBuffer_{};

    // The main pass samples this texture while the shadow pass writes it as a
    // depth attachment. RenderGraph transitions the same resource between the
    // two usages; no CPU readback is involved.
    rhi::RHITexture shadowTexture_{};
    rhi::RHITextureView shadowView_{};
    rhi::RHISampler shadowSampler_{};

    rhi::RHIBindSetLayout bindSetLayout_{};
    rhi::RHIBindSet sphereBindSet_{};
    rhi::RHIBindSet planeBindSet_{};
    rhi::RHIPipelineLayout pipelineLayout_{};
    rhi::RHIPipeline pipeline_{};

    // A depth-only pass only needs the per-object UBO. Keeping a separate
    // layout prevents the shadow texture from being sampled while it is bound
    // as the current depth attachment, which is especially important in D3D11.
    rhi::RHIBindSetLayout shadowBindSetLayout_{};
    rhi::RHIBindSet shadowSphereBindSet_{};
    rhi::RHIPipelineLayout shadowPipelineLayout_{};
    rhi::RHIPipeline shadowPipeline_{};

    std::vector<std::byte> initialVertexData_;
    std::vector<std::byte> initialIndexData_;
    bool staticUploadsPending_ = true;
    rhi::u32 sphereVertexCount_ = 0;
    rhi::u32 sphereIndexCount_ = 0;
    rhi::u32 planeIndexCount_ = 0;
    rhi::u64 sphereIndexOffset_ = 0;
    rhi::u64 planeIndexOffset_ = 0;

    std::array<rhi::RHIGPUWaitGPUSignal, FRAMES_IN_FLIGHT> imageAvailable_{};
    std::array<rhi::RHIGPUWaitGPUSignal, FRAMES_IN_FLIGHT> renderFinished_{};
    rhi::u32 frameSlot_ = 0;
    rhi::u64 frameIndex_ = 0;
    std::chrono::steady_clock::time_point startTime_ = std::chrono::steady_clock::now();

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
        const std::wstring windowTitle =
            std::wstring(L"RHI RenderGraph PBR Demo - ") +
            ApiDisplayName(options_.api);
        window_ = CreateWindowExW(
            0,
            windowClass.lpszClassName,
            windowTitle.c_str(),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rectangle.right - rectangle.left,
            rectangle.bottom - rectangle.top,
            nullptr,
            nullptr,
            instance,
            this);
        if (window_ == nullptr) {
            throw std::runtime_error("CreateWindowEx failed");
        }
    }

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

    void CreateStaticResources() {
        const Mesh sphere = MakeSphere(32, 64);
        const Mesh plane = MakePlane();
        sphereVertexCount_ = static_cast<rhi::u32>(sphere.vertices.size());
        sphereIndexCount_ = static_cast<rhi::u32>(sphere.indices.size());
        planeIndexCount_ = static_cast<rhi::u32>(plane.indices.size());

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

        rhi::RHITextureDesc shadowTextureDesc{};
        shadowTextureDesc.debugName = "PBR.ShadowDepth";
        shadowTextureDesc.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
        shadowTextureDesc.format = rhi::RHIFormat::D32_Float;
        shadowTextureDesc.usage = rhi::RHITextureUsage::DepthStencilAttachment |
                                  rhi::RHITextureUsage::Sampled;
        shadowTexture_ = device_->CreateTexture(shadowTextureDesc);

        rhi::RHITextureViewDesc shadowViewDesc{};
        shadowViewDesc.debugName = "PBR.ShadowDepthView";
        shadowViewDesc.texture = shadowTexture_;
        shadowViewDesc.format = shadowTextureDesc.format;
        shadowViewDesc.aspect = rhi::RHITextureAspect::Depth;
        shadowView_ = device_->CreateTextureView(shadowViewDesc);

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
        // Samples outside the light frustum read depth 1 and therefore remain lit.
        shadowSamplerDesc.borderColor = rhi::RHIBorderColor::OpaqueWhite;
        shadowSampler_ = device_->CreateSampler(shadowSamplerDesc);

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
        bindSetLayout_ = device_->CreateBindSetLayout(bindLayoutDesc);

        sphereBindSet_ = CreatePBRBindSet("PBR.SphereBindSet", sphereUniformBuffer_);
        planeBindSet_ = CreatePBRBindSet("PBR.PlaneBindSet", planeUniformBuffer_);

        rhi::RHIPipelineLayoutDesc pipelineLayoutDesc{};
        pipelineLayoutDesc.debugName = "PBR.PipelineLayout";
        pipelineLayoutDesc.bindSetLayouts.push_back(bindSetLayout_);
        pipelineLayout_ = device_->CreatePipelineLayout(pipelineLayoutDesc);

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

    rhi::RHIBindSet CreatePBRBindSet(const char* name, rhi::RHIBuffer buffer) {
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
        return device_->CreateBindSet(desc);
    }

    rhi::RHIBindSet CreateShadowBindSet(
        const char* name,
        rhi::RHIBuffer buffer) {
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

    rhi::RHIExtent2D ClientExtent() const {
        RECT rectangle{};
        GetClientRect(window_, &rectangle);
        return {
            static_cast<rhi::u32>(std::max<LONG>(0, rectangle.right - rectangle.left)),
            static_cast<rhi::u32>(std::max<LONG>(0, rectangle.bottom - rectangle.top))};
    }

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
            const bool d3d12 = options_.api == rhi::RHIGraphicsAPI::Direct3D12;
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

        rhi::RHIShaderDesc shadowVertexShader{};
        shadowVertexShader.debugName = "PBR.ShadowVertexShader";
        shadowVertexShader.stage = rhi::RHIShaderStage::Vertex;
        if (options_.api == rhi::RHIGraphicsAPI::Vulkan) {
            shadowVertexShader.language = rhi::RHIShaderLanguage::SPIRV;
            shadowVertexShader.filePath = shaderDirectory + "/shadow.vert.spv";
        } else {
            const bool d3d12 = options_.api == rhi::RHIGraphicsAPI::Direct3D12;
            shadowVertexShader.language = rhi::RHIShaderLanguage::HLSL;
            shadowVertexShader.filePath = shaderDirectory + "/pbr.hlsl";
            shadowVertexShader.entryPoint = "ShadowVSMain";
            shadowVertexShader.compileOptions.targetProfile = d3d12 ? "vs_5_1" : "vs_5_0";
        }

        // The shadow shader reads only POSITION. The stream stride remains the
        // complete Vertex size so the same interleaved vertex buffer can be reused.
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
        // A small raster bias moves stored caster depth away from the light.
        // The receiver also applies a normal-dependent bias before comparison.
        shadowPipelineDesc.raster.depthBiasEnable = true;
        shadowPipelineDesc.raster.depthBiasConstantFactor = 1.0F;
        shadowPipelineDesc.raster.depthBiasSlopeFactor = 1.5F;
        shadowPipelineDesc.depthStencil.depthTestEnable = true;
        shadowPipelineDesc.depthStencil.depthWriteEnable = true;
        shadowPipelineDesc.depthStencil.depthCompareOp = rhi::RHICompareOp::Less;
        shadowPipelineDesc.depthStencilFormat = rhi::RHIFormat::D32_Float;
        shadowPipeline_ = device_->CreateGraphicsPipeline(shadowPipelineDesc);
    }

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

    UniformBufferObject MakeUniform(bool sphere) const {
        const float time = std::chrono::duration<float>(
                               std::chrono::steady_clock::now() - startTime_)
                               .count();
        const glm::vec3 eye{0.0F, 3.0F, 6.0F};

        UniformBufferObject uniform{};
        uniform.view = glm::lookAt(
            eye,
            glm::vec3{0.0F, 0.5F, 0.0F},
            glm::vec3{0.0F, 1.0F, 0.0F});
        uniform.projection = glm::perspective(
            glm::radians(45.0F),
            static_cast<float>(swapchainExtent_.width) /
                static_cast<float>(swapchainExtent_.height),
            0.1F,
            100.0F);
        // GLM 的投影与 D3D 的 viewport Y 方向可直接配合；Vulkan 的正高度 viewport
        // 需要翻转 clip-space Y。若 D3D 也翻转，画面会上下颠倒且三角形屏幕绕序反转。
        if (options_.api == rhi::RHIGraphicsAPI::Vulkan) {
            uniform.projection[1][1] *= -1.0F;
        }
        uniform.lightDirection = glm::vec4(
            glm::normalize(glm::vec3{-0.5F, -1.0F, -0.3F}), 0.0F);
        uniform.lightColor = {1.0F, 0.96F, 0.90F, 1.0F};
        uniform.cameraPosition = glm::vec4(eye, 1.0F);

        // Directional lights have no physical position. Shadow mapping creates
        // a virtual orthographic camera opposite the ray direction so parallel
        // light rays preserve their direction and do not gain perspective.
        const glm::vec3 lightDirection = glm::vec3(uniform.lightDirection);
        const glm::vec3 shadowTarget{0.0F, 0.5F, 0.0F};
        const glm::vec3 lightPosition = shadowTarget - lightDirection * 8.0F;
        const glm::mat4 lightView = glm::lookAt(
            lightPosition,
            shadowTarget,
            glm::vec3{0.0F, 1.0F, 0.0F});
        glm::mat4 lightProjection = glm::ortho(
            -5.0F,
            5.0F,
            -5.0F,
            5.0F,
            0.1F,
            16.0F);
        if (options_.api == rhi::RHIGraphicsAPI::Vulkan) {
            lightProjection[1][1] *= -1.0F;
        }
        uniform.lightViewProjection = lightProjection * lightView;
        uniform.shadowParameters = {
            1.0F / static_cast<float>(SHADOW_MAP_SIZE),
            1.0F / static_cast<float>(SHADOW_MAP_SIZE),
            0.00035F,
            0.0025F};

        if (sphere) {
            uniform.model = glm::translate(glm::mat4{1.0F}, {0.0F, 1.0F, 0.0F}) *
                            glm::rotate(
                                glm::mat4{1.0F},
                                time * glm::radians(45.0F),
                                glm::vec3{0.0F, 1.0F, 0.0F});
            uniform.baseColor = {0.85F, 0.55F, 0.25F, 0.85F};
            uniform.materialParameters = {0.35F, 1.0F, 0.0F, 0.0F};
        } else {
            uniform.model = glm::mat4{1.0F};
            uniform.baseColor = {0.45F, 0.45F, 0.50F, 0.0F};
            uniform.materialParameters = {0.85F, 1.0F, 0.0F, 0.0F};
        }
        return uniform;
    }

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

        if (staticUploadsPending_) {
            packet.uploads.buffers.push_back({vertexBuffer_, 0, initialVertexData_});
            packet.uploads.buffers.push_back({indexBuffer_, 0, initialIndexData_});
        }
        packet.uploads.buffers.push_back(
            {sphereUniformBuffer_, 0, ToBytes(MakeUniform(true))});
        packet.uploads.buffers.push_back(
            {planeUniformBuffer_, 0, ToBytes(MakeUniform(false))});

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
        shadowAttachment.loadOp = rhi::RHILoadOp::Clear;
        shadowAttachment.storeOp = rhi::RHIStoreOp::Store;
        shadowAttachment.clearValue.depthStencil = {1.0F, 0};
        shadowPass.depthStencilAttachment = shadowAttachment;
        packet.graph.passes.push_back(shadowPass);

        rhi::RHIRenderGraphPassDesc opaque{};
        opaque.name = "OpaquePBR";
        opaque.type = rhi::RHIRenderGraphPassType::Raster;
        opaque.reads = {
            {"Vertices", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::VertexBuffer, rhi::RHIPipelineStage::VertexInput},
            {"Indices", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::IndexBuffer, rhi::RHIPipelineStage::VertexInput},
            {"SphereUniform", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::ConstantBuffer, rhi::RHIPipelineStage::VertexShader | rhi::RHIPipelineStage::FragmentShader},
            {"PlaneUniform", rhi::RHIRenderGraphResourceType::Buffer, rhi::RHIResourceState::ConstantBuffer, rhi::RHIPipelineStage::VertexShader | rhi::RHIPipelineStage::FragmentShader},
            {"ShadowDepth", rhi::RHIRenderGraphResourceType::Texture, rhi::RHIResourceState::ShaderRead, rhi::RHIPipelineStage::FragmentShader}};

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
        packet.workloads.push_back(std::move(opaqueWorkload));

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

        packet.present = rhi::RHIPresentDesc{
            swapchain_,
            imageIndex,
            {renderFinished_[frameSlot_]},
            rhi::RHIPresentMode::FIFO,
            false};
        return packet;
    }

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

    void Cleanup() noexcept {
        if (device_ == nullptr) {
            return;
        }
        device_->WaitIdle();
        device_->Destroy(pipeline_);
        device_->Destroy(shadowPipeline_);
        device_->Destroy(pipelineLayout_);
        device_->Destroy(shadowPipelineLayout_);
        device_->Destroy(sphereBindSet_);
        device_->Destroy(planeBindSet_);
        device_->Destroy(shadowSphereBindSet_);
        device_->Destroy(bindSetLayout_);
        device_->Destroy(shadowBindSetLayout_);
        device_->Destroy(shadowSampler_);
        device_->Destroy(shadowView_);
        device_->Destroy(shadowTexture_);
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

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR commandLine, int) {
    try {
        PBRDemoApp app(ParseOptions(commandLine != nullptr ? commandLine : ""));
        app.Run(instance);
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        MessageBoxA(nullptr, exception.what(), "RHI PBR Demo", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
}
