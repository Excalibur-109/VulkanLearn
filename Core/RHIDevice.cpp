#include "RHIDevice.hpp"

#include "../RenderGraph/RHIRenderGraph.hpp"
#include "../Vulkan/RHIVulkan.hpp"

#if defined(_WIN32)
#include "../D3D11/RHID3D11.hpp"
#include "../D3D12/RHID3D12.hpp"
#endif

#include <stdexcept>
#include <type_traits>
#include <utility>
#include <variant>

namespace rhi {

namespace {

using RHIImplementationVariant = std::variant<
    std::monostate,
    RHIVulkan
#if defined(_WIN32)
    , RHID3D11
    , RHID3D12
#endif
>;

template <typename Return, typename Variant, typename Function>
Return visitImplementation(Variant&& variant, Function&& function) {
    return std::visit(
        [&](auto&& implementation) -> Return {
            using Implementation = std::remove_cvref_t<decltype(implementation)>;
            if constexpr (std::is_same_v<Implementation, std::monostate>) {
                throw std::runtime_error("RHIDevice is not initialized");
            } else {
                return function(implementation);
            }
        },
        std::forward<Variant>(variant));
}

template <typename Variant, typename Function>
void visitImplementationNoexcept(Variant&& variant, Function&& function) noexcept {
    std::visit(
        [&](auto&& implementation) noexcept {
            using Implementation = std::remove_cvref_t<decltype(implementation)>;
            if constexpr (!std::is_same_v<Implementation, std::monostate>) {
                function(implementation);
            }
        },
        std::forward<Variant>(variant));
}

} // namespace

struct RHIDevice::Impl {
    RHIGraphicsAPI requestedApi = RHIGraphicsAPI::Unknown;
    RHIGraphicsAPI activeApi = RHIGraphicsAPI::Unknown;
    RHIImplementationVariant implementation{};

    // RenderGraph 的拓扑通常连续数百帧保持不变。缓存编译结果可以跳过每帧的
    // 名称查找、hazard 分析、拓扑排序和 barrier 规划。执行计划只保存源数组索引，
    // clear value、viewport、draw 参数等动态数据仍从当前 RHIFramePacket 读取。
    u64 cachedGraphHash = 0;
    bool hasCachedGraph = false;
    RHIRenderGraphExecutionPlan cachedGraphPlan{};
};

RHIDevice::RHIDevice(RHIGraphicsAPI requestedApi)
    : impl_(std::make_unique<Impl>()) {
    impl_->requestedApi = requestedApi;
}

RHIDevice::~RHIDevice() {
    Shutdown();
}

RHIDevice::RHIDevice(RHIDevice&&) noexcept = default;
RHIDevice& RHIDevice::operator=(RHIDevice&&) noexcept = default;

RHIGraphicsAPI RHIDevice::Api() const noexcept {
    return impl_->activeApi != RHIGraphicsAPI::Unknown ? impl_->activeApi : impl_->requestedApi;
}

const char* RHIDevice::BackendName() const noexcept {
    switch (Api()) {
        case RHIGraphicsAPI::Vulkan:    return "Vulkan";
        case RHIGraphicsAPI::D3D11:     return "Direct3D 11";
        case RHIGraphicsAPI::D3D12:     return "Direct3D 12";
        default:                        return "UnInitialized RHI";
    }
}

bool RHIDevice::Initialize(const RHIDeviceCreateDesc& desc, std::string* errorMessage) {
    Shutdown();

    RHIGraphicsAPI selectedApi = impl_->requestedApi;
    if (selectedApi == RHIGraphicsAPI::Unknown) {
        selectedApi = desc.backend.preferredApi;
    }

    switch (selectedApi) {
    case RHIGraphicsAPI::Vulkan: {
        RHIVulkanDesc nativeDesc{};
        nativeDesc.backend = desc.backend;
        nativeDesc.backend.preferredApi = RHIGraphicsAPI::Vulkan;
        nativeDesc.surface.surface = reinterpret_cast<VkSurfaceKHR>(desc.vulkanSurface);
        nativeDesc.surface.ownsSurface = desc.ownsVulkanSurface;
        if (desc.createVulkanSurface) {
            nativeDesc.surface.createSurface = [factory = desc.createVulkanSurface](VkInstance instance) {
                return reinterpret_cast<VkSurfaceKHR>(factory(reinterpret_cast<std::uintptr_t>(instance)));
            };
        }
        nativeDesc.requiredInstanceExtensions = desc.requiredVulkanInstanceExtensions;
        nativeDesc.optionalInstanceExtensions = desc.optionalVulkanInstanceExtensions;
        nativeDesc.requiredDeviceExtensions = desc.requiredVulkanDeviceExtensions;
        nativeDesc.optionalDeviceExtensions = desc.optionalVulkanDeviceExtensions;
        nativeDesc.queues = desc.queues;

        RHIVulkan& implementation = impl_->implementation.emplace<RHIVulkan>();
        if (!implementation.Initialize(nativeDesc, errorMessage)) {
            impl_->implementation.emplace<std::monostate>();
            return false;
        }
        break;
    }
#if defined(_WIN32)
    case RHIGraphicsAPI::D3D11: {
        RHID3D11Desc nativeDesc{};
        nativeDesc.backend = desc.backend;
        nativeDesc.backend.preferredApi = RHIGraphicsAPI::D3D11;
        nativeDesc.surface.hwnd = static_cast<HWND>(desc.nativeWindow);
        nativeDesc.minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        nativeDesc.allowWarpFallback = desc.allowSoftwareAdapter;

        RHID3D11& implementation = impl_->implementation.emplace<RHID3D11>();
        if (!implementation.Initialize(nativeDesc, errorMessage)) {
            impl_->implementation.emplace<std::monostate>();
            return false;
        }
        break;
    }
    case RHIGraphicsAPI::D3D12: {
        RHID3D12Desc nativeDesc{};
        nativeDesc.backend = desc.backend;
        nativeDesc.backend.preferredApi = RHIGraphicsAPI::D3D12;
        nativeDesc.surface.hwnd = static_cast<HWND>(desc.nativeWindow);
        nativeDesc.minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        nativeDesc.allowWarpFallback = desc.allowSoftwareAdapter;

        RHID3D12& implementation = impl_->implementation.emplace<RHID3D12>();
        if (!implementation.Initialize(nativeDesc, errorMessage)) {
            impl_->implementation.emplace<std::monostate>();
            return false;
        }
        break;
    }
#endif
    default:
        if (errorMessage != nullptr) {
            *errorMessage = "RHIDevice 不支持请求的图形 API";
        }
        return false;
    }

    impl_->activeApi = selectedApi;
    return true;
}

void RHIDevice::Shutdown() noexcept {
    visitImplementationNoexcept(impl_->implementation, [](auto& implementation) { implementation.Shutdown(); });
    impl_->implementation.emplace<std::monostate>();
    impl_->activeApi = RHIGraphicsAPI::Unknown;
    impl_->cachedGraphHash = 0;
    impl_->hasCachedGraph = false;
    impl_->cachedGraphPlan = {};
}

bool RHIDevice::IsInitialized() const noexcept {
    return std::visit(
        [](const auto& implementation) noexcept {
            using Implementation = std::remove_cvref_t<decltype(implementation)>;
            if constexpr (std::is_same_v<Implementation, std::monostate>) {
                return false;
            } else {
                return implementation.IsInitialized();
            }
        },
        impl_->implementation);
}

const RHICapabilities& RHIDevice::Capabilities() const noexcept {
    static const RHICapabilities emptyCapabilities{};
    return std::visit(
        [&](const auto& implementation) -> const RHICapabilities& {
            using Implementation = std::remove_cvref_t<decltype(implementation)>;
            if constexpr (std::is_same_v<Implementation, std::monostate>) {
                return emptyCapabilities;
            } else {
                return implementation.Capabilities();
            }
        },
        impl_->implementation);
}

#define RHI_FORWARD_RETURN(ReturnType, Method, ParameterType)                                           \
    ReturnType RHIDevice::Method(const ParameterType& desc) {                                           \
        return visitImplementation<ReturnType>(                                                         \
            impl_->implementation,                                                                      \
            [&](auto& implementation) { return implementation.Method(desc); });                         \
    }

RHI_FORWARD_RETURN(RHIBuffer, CreateBuffer, RHIBufferDesc)
RHI_FORWARD_RETURN(RHITexture, CreateTexture, RHITextureDesc)
RHI_FORWARD_RETURN(RHITextureView, CreateTextureView, RHITextureViewDesc)
RHI_FORWARD_RETURN(RHISampler, CreateSampler, RHISamplerDesc)
RHI_FORWARD_RETURN(RHIShader, CreateShaderModule, RHIShaderDesc)
RHI_FORWARD_RETURN(RHIBindSetLayout, CreateBindSetLayout, RHIBindSetLayoutDesc)
RHI_FORWARD_RETURN(RHIBindSet, CreateBindSet, RHIBindSetDesc)
RHI_FORWARD_RETURN(RHIPipelineLayout, CreatePipelineLayout, RHIPipelineLayoutDesc)
RHI_FORWARD_RETURN(RHIPipelineCache, CreatePipelineCache, RHIPipelineCacheDesc)
RHI_FORWARD_RETURN(RHIPipeline, CreateGraphicsPipeline, RHIGraphicsPipelineDesc)
RHI_FORWARD_RETURN(RHIPipeline, CreateComputePipeline, RHIComputePipelineDesc)
RHI_FORWARD_RETURN(RHIQueryPool, CreateQueryPool, RHIQueryPoolDesc)
RHI_FORWARD_RETURN(RHIGPUWaitGPUSignal, CreateGPUWaitGPUSignal, RHIGPUWaitGPUSignalDesc)
RHI_FORWARD_RETURN(RHICPUWaitGPUSignal, CreateCPUWaitGPUSignal, RHICPUWaitGPUSignalDesc)
RHI_FORWARD_RETURN(RHISwapchain, CreateSwapchain, RHISwapchainDesc)

#undef RHI_FORWARD_RETURN

std::vector<RHITexture> RHIDevice::GetSwapchainImages(RHISwapchain handle) const {
    return visitImplementation<std::vector<RHITexture>>(impl_->implementation, [&](const auto& implementation) { return implementation.GetSwapchainImages(handle); });
}

std::vector<RHITextureView> RHIDevice::GetSwapchainImageViews(RHISwapchain handle) const {
    return visitImplementation<std::vector<RHITextureView>>(impl_->implementation, [&](const auto& implementation) { return implementation.GetSwapchainImageViews(handle); });
}

RHIFormat RHIDevice::GetSwapchainFormat(RHISwapchain handle) const {
    return visitImplementation<RHIFormat>(impl_->implementation, [&](const auto& implementation) { return implementation.GetSwapchainFormat(handle); });
}

RHIExtent2D RHIDevice::GetSwapchainExtent(RHISwapchain handle) const {
    return visitImplementation<RHIExtent2D>(impl_->implementation, [&](const auto& implementation) { return implementation.GetSwapchainExtent(handle); });
}

bool RHIDevice::AcquireNextImage(RHISwapchain swapchain, RHIGPUWaitGPUSignal gpuWaitGPUSignal, RHICPUWaitGPUSignal cpuWaitGPUSignal, u32* imageIndex, std::string* errorMessage) {
    return visitImplementation<bool>(impl_->implementation, [&](auto& implementation) { return implementation.AcquireNextImage(swapchain, gpuWaitGPUSignal, cpuWaitGPUSignal, imageIndex, errorMessage); });
}

bool RHIDevice::Submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) {
    return visitImplementation<bool>(impl_->implementation, [&](auto& implementation) { return implementation.Submit(desc, errorMessage); });
}

bool RHIDevice::Present(const RHIPresentDesc& desc, std::string* errorMessage) {
    return visitImplementation<bool>(impl_->implementation, [&](auto& implementation) { return implementation.Present(desc, errorMessage); });
}

bool RHIDevice::SubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    // RenderGraph 的拓扑通常连续很多帧不变。结构哈希只覆盖会影响依赖、裁剪、
    // barrier 和 transient 槽分配的静态声明，因此每帧计算一次的成本远低于重新编译。
    const u64 graphHash = HashRHIRenderGraphStructure(packet.graph, packet.workloads);
    if (!impl_->hasCachedGraph || impl_->cachedGraphHash != graphHash) {
        // 首帧或结构改变时才编译。编译失败不会覆盖上一份可用计划，错误会直接返回
        // 调用方，避免后端执行一个只有部分依赖信息的 plan。
        RHIRenderGraphCompileResult graph =
            CompileRHIRenderGraph(packet.graph, packet.workloads);
        if (!graph.Succeeded()) {
            if (errorMessage != nullptr) {
                *errorMessage = graph.ErrorMessage();
            }
            return false;
        }

        impl_->cachedGraphHash = graphHash;
        impl_->cachedGraphPlan = std::move(graph.plan);
        impl_->hasCachedGraph = true;
    }

    // 缓存的是纯静态 ExecutionPlan，而 packet 始终使用当前帧版本。clear value、
    // viewport、draw 参数、upload 数据及 imported native handle 都能逐帧变化；后端
    // 用 plan 的整数下标回到当前 packet 解析这些动态数据。
    return visitImplementation<bool>(
        impl_->implementation,
        [&](auto& implementation) {
            return implementation.SubmitFrame(packet, impl_->cachedGraphPlan, errorMessage);
        });
}

void RHIDevice::WaitIdle() const noexcept {
    visitImplementationNoexcept(impl_->implementation, [](const auto& implementation) { implementation.WaitIdle(); });
}

#define RHI_FORWARD_DESTROY(HandleType)                                                                 \
    void RHIDevice::Destroy(HandleType handle) noexcept {                                               \
        visitImplementationNoexcept(                                                                    \
            impl_->implementation,                                                                      \
            [&](auto& implementation) { implementation.Destroy(handle); });                             \
    }

RHI_FORWARD_DESTROY(RHIBuffer)
RHI_FORWARD_DESTROY(RHITexture)
RHI_FORWARD_DESTROY(RHITextureView)
RHI_FORWARD_DESTROY(RHISampler)
RHI_FORWARD_DESTROY(RHIShader)
RHI_FORWARD_DESTROY(RHIBindSetLayout)
RHI_FORWARD_DESTROY(RHIBindSet)
RHI_FORWARD_DESTROY(RHIPipelineLayout)
RHI_FORWARD_DESTROY(RHIPipelineCache)
RHI_FORWARD_DESTROY(RHIPipeline)
RHI_FORWARD_DESTROY(RHIQueryPool)
RHI_FORWARD_DESTROY(RHIGPUWaitGPUSignal)
RHI_FORWARD_DESTROY(RHICPUWaitGPUSignal)
RHI_FORWARD_DESTROY(RHISwapchain)

#undef RHI_FORWARD_DESTROY

} // namespace rhi













