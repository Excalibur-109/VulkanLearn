#include "RHIDeviceFactory.hpp"

namespace rhi {

std::unique_ptr<RHIDevice> CreateRHIDevice(RHIGraphicsAPI api, std::string*) {
    return std::make_unique<RHIDevice>(api);
}

std::unique_ptr<RHIDevice> CreateInitializedRHIDevice(const RHIDeviceCreateDesc& desc, std::string* errorMessage) {
    std::unique_ptr<RHIDevice> device = std::make_unique<RHIDevice>(desc.backend.preferredApi);
    if (!device->Initialize(desc, errorMessage)) {
        return nullptr;
    }
    return device;
}

} // namespace rhi




