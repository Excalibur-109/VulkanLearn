#include "RHIDeviceFactory.hpp"

#include "../Vulkan/RHIVulkanDevice.hpp"

#if defined(_WIN32)
#include "../D3D11/RHID3D11Device.hpp"
#include "../D3D12/RHID3D12Device.hpp"
#endif

namespace rhi {

std::unique_ptr<RHIDevice> createRHIDevice(RHIGraphicsAPI api, std::string* errorMessage) {
    switch (api) {
    case RHIGraphicsAPI::Vulkan:
        return std::make_unique<RHIVulkanDevice>();
#if defined(_WIN32)
    case RHIGraphicsAPI::Direct3D11:
        return std::make_unique<RHID3D11Device>();
    case RHIGraphicsAPI::Direct3D12:
        return std::make_unique<RHID3D12Device>();
#endif
    default:
        if (errorMessage != nullptr) {
            *errorMessage = "当前 RHI 没有实现请求的图形 API";
        }
        return nullptr;
    }
}

std::unique_ptr<RHIDevice> createInitializedRHIDevice(const RHIDeviceCreateDesc& desc, std::string* errorMessage) {
    std::unique_ptr<RHIDevice> device = createRHIDevice(desc.backend.preferredApi, errorMessage);
    if (device == nullptr || !device->initialize(desc, errorMessage)) {
        return nullptr;
    }
    return device;
}

} // namespace rhi