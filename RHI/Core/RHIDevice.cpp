#include "RHIDevice.hpp"

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
};

RHIDevice::RHIDevice(RHIGraphicsAPI requestedApi)
    : impl_(std::make_unique<Impl>()) {
    impl_->requestedApi = requestedApi;
}

RHIDevice::~RHIDevice() {
    shutdown();
}

RHIDevice::RHIDevice(RHIDevice&&) noexcept = default;
RHIDevice& RHIDevice::operator=(RHIDevice&&) noexcept = default;

RHIGraphicsAPI RHIDevice::api() const noexcept {
    return impl_->activeApi != RHIGraphicsAPI::Unknown ? impl_->activeApi : impl_->requestedApi;
}

const char* RHIDevice::backendName() const noexcept {
    switch (api()) {
    case RHIGraphicsAPI::Vulkan:     return "Vulkan";
    case RHIGraphicsAPI::Direct3D11: return "Direct3D 11";
    case RHIGraphicsAPI::Direct3D12: return "Direct3D 12";
    default:                         return "Uninitialized RHI";
    }
}

bool RHIDevice::initialize(const RHIDeviceCreateDesc& desc, std::string* errorMessage) {
    shutdown();

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
        if (!implementation.initialize(nativeDesc, errorMessage)) {
            impl_->implementation.emplace<std::monostate>();
            return false;
        }
        break;
    }
#if defined(_WIN32)
    case RHIGraphicsAPI::Direct3D11: {
        RHID3D11Desc nativeDesc{};
        nativeDesc.backend = desc.backend;
        nativeDesc.backend.preferredApi = RHIGraphicsAPI::Direct3D11;
        nativeDesc.surface.hwnd = static_cast<HWND>(desc.nativeWindow);
        nativeDesc.minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        nativeDesc.allowWarpFallback = desc.allowSoftwareAdapter;

        RHID3D11& implementation = impl_->implementation.emplace<RHID3D11>();
        if (!implementation.initialize(nativeDesc, errorMessage)) {
            impl_->implementation.emplace<std::monostate>();
            return false;
        }
        break;
    }
    case RHIGraphicsAPI::Direct3D12: {
        RHID3D12Desc nativeDesc{};
        nativeDesc.backend = desc.backend;
        nativeDesc.backend.preferredApi = RHIGraphicsAPI::Direct3D12;
        nativeDesc.surface.hwnd = static_cast<HWND>(desc.nativeWindow);
        nativeDesc.minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
        nativeDesc.allowWarpFallback = desc.allowSoftwareAdapter;

        RHID3D12& implementation = impl_->implementation.emplace<RHID3D12>();
        if (!implementation.initialize(nativeDesc, errorMessage)) {
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

void RHIDevice::shutdown() noexcept {
    visitImplementationNoexcept(impl_->implementation, [](auto& implementation) { implementation.shutdown(); });
    impl_->implementation.emplace<std::monostate>();
    impl_->activeApi = RHIGraphicsAPI::Unknown;
}

bool RHIDevice::isInitialized() const noexcept {
    return std::visit(
        [](const auto& implementation) noexcept {
            using Implementation = std::remove_cvref_t<decltype(implementation)>;
            if constexpr (std::is_same_v<Implementation, std::monostate>) {
                return false;
            } else {
                return implementation.isInitialized();
            }
        },
        impl_->implementation);
}

const RHICapabilities& RHIDevice::capabilities() const noexcept {
    static const RHICapabilities emptyCapabilities{};
    return std::visit(
        [&](const auto& implementation) -> const RHICapabilities& {
            using Implementation = std::remove_cvref_t<decltype(implementation)>;
            if constexpr (std::is_same_v<Implementation, std::monostate>) {
                return emptyCapabilities;
            } else {
                return implementation.capabilities();
            }
        },
        impl_->implementation);
}

#define RHI_FORWARD_RETURN(ReturnType, Method, ParameterType) \
    ReturnType RHIDevice::Method(const ParameterType& desc) { \
        return visitImplementation<ReturnType>(impl_->implementation, [&](auto& implementation) { return implementation.Method(desc); }); \
    }

RHI_FORWARD_RETURN(RHIBuffer, createBuffer, RHIBufferDesc)
RHI_FORWARD_RETURN(RHITexture, createTexture, RHITextureDesc)
RHI_FORWARD_RETURN(RHITextureView, createTextureView, RHITextureViewDesc)
RHI_FORWARD_RETURN(RHISampler, createSampler, RHISamplerDesc)
RHI_FORWARD_RETURN(RHIShader, createShaderModule, RHIShaderDesc)
RHI_FORWARD_RETURN(RHIBindGroupLayout, createBindGroupLayout, RHIBindGroupLayoutDesc)
RHI_FORWARD_RETURN(RHIBindGroup, createBindGroup, RHIBindGroupDesc)
RHI_FORWARD_RETURN(RHIPipelineLayout, createPipelineLayout, RHIPipelineLayoutDesc)
RHI_FORWARD_RETURN(RHIPipelineCache, createPipelineCache, RHIPipelineCacheDesc)
RHI_FORWARD_RETURN(RHIPipeline, createGraphicsPipeline, RHIGraphicsPipelineDesc)
RHI_FORWARD_RETURN(RHIPipeline, createComputePipeline, RHIComputePipelineDesc)
RHI_FORWARD_RETURN(RHIQueryPool, createQueryPool, RHIQueryPoolDesc)
RHI_FORWARD_RETURN(RHISemaphore, createSemaphore, RHISemaphoreDesc)
RHI_FORWARD_RETURN(RHIFence, createFence, RHIFenceDesc)
RHI_FORWARD_RETURN(RHISwapchain, createSwapchain, RHISwapchainDesc)

#undef RHI_FORWARD_RETURN

std::vector<RHITexture> RHIDevice::getSwapchainImages(RHISwapchain handle) const {
    return visitImplementation<std::vector<RHITexture>>(impl_->implementation, [&](const auto& implementation) { return implementation.getSwapchainImages(handle); });
}

std::vector<RHITextureView> RHIDevice::getSwapchainImageViews(RHISwapchain handle) const {
    return visitImplementation<std::vector<RHITextureView>>(impl_->implementation, [&](const auto& implementation) { return implementation.getSwapchainImageViews(handle); });
}

RHIFormat RHIDevice::getSwapchainFormat(RHISwapchain handle) const {
    return visitImplementation<RHIFormat>(impl_->implementation, [&](const auto& implementation) { return implementation.getSwapchainFormat(handle); });
}

RHIExtent2D RHIDevice::getSwapchainExtent(RHISwapchain handle) const {
    return visitImplementation<RHIExtent2D>(impl_->implementation, [&](const auto& implementation) { return implementation.getSwapchainExtent(handle); });
}

bool RHIDevice::acquireNextImage(RHISwapchain swapchain, RHISemaphore signalSemaphore, RHIFence signalFence, u32* imageIndex, std::string* errorMessage) {
    return visitImplementation<bool>(impl_->implementation, [&](auto& implementation) { return implementation.acquireNextImage(swapchain, signalSemaphore, signalFence, imageIndex, errorMessage); });
}

bool RHIDevice::submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) {
    return visitImplementation<bool>(impl_->implementation, [&](auto& implementation) { return implementation.submit(desc, errorMessage); });
}

bool RHIDevice::present(const RHIPresentDesc& desc, std::string* errorMessage) {
    return visitImplementation<bool>(impl_->implementation, [&](auto& implementation) { return implementation.present(desc, errorMessage); });
}

bool RHIDevice::submitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    return visitImplementation<bool>(impl_->implementation, [&](auto& implementation) { return implementation.submitFrame(packet, errorMessage); });
}

void RHIDevice::waitIdle() const noexcept {
    visitImplementationNoexcept(impl_->implementation, [](const auto& implementation) { implementation.waitIdle(); });
}

#define RHI_FORWARD_DESTROY(HandleType) \
    void RHIDevice::destroy(HandleType handle) noexcept { \
        visitImplementationNoexcept(impl_->implementation, [&](auto& implementation) { implementation.destroy(handle); }); \
    }

RHI_FORWARD_DESTROY(RHIBuffer)
RHI_FORWARD_DESTROY(RHITexture)
RHI_FORWARD_DESTROY(RHITextureView)
RHI_FORWARD_DESTROY(RHISampler)
RHI_FORWARD_DESTROY(RHIShader)
RHI_FORWARD_DESTROY(RHIBindGroupLayout)
RHI_FORWARD_DESTROY(RHIBindGroup)
RHI_FORWARD_DESTROY(RHIPipelineLayout)
RHI_FORWARD_DESTROY(RHIPipelineCache)
RHI_FORWARD_DESTROY(RHIPipeline)
RHI_FORWARD_DESTROY(RHIQueryPool)
RHI_FORWARD_DESTROY(RHISemaphore)
RHI_FORWARD_DESTROY(RHIFence)
RHI_FORWARD_DESTROY(RHISwapchain)

#undef RHI_FORWARD_DESTROY

} // namespace rhi





