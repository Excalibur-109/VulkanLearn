#include "RHIVulkanPrivate.hpp"

namespace rhi {

RHIVulkanDevice::RHIVulkanDevice()
    : impl_(std::make_unique<Impl>()) {
}

RHIVulkanDevice::~RHIVulkanDevice() {
    shutdown();
}

RHIGraphicsAPI RHIVulkanDevice::api() const noexcept { return RHIGraphicsAPI::Vulkan; }
const char* RHIVulkanDevice::backendName() const noexcept { return "Vulkan"; }

bool RHIVulkanDevice::initialize(const RHIDeviceCreateDesc& desc, std::string* errorMessage) {
    RHIVulkanBackendDesc nativeDesc{};
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
    return impl_->backend.initialize(nativeDesc, errorMessage);
}

void RHIVulkanDevice::shutdown() noexcept { impl_->backend.shutdown(); }
bool RHIVulkanDevice::isInitialized() const noexcept { return impl_->backend.isInitialized(); }
const RHICapabilities& RHIVulkanDevice::capabilities() const noexcept { return impl_->backend.capabilities(); }

} // namespace rhi