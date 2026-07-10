#pragma once

#include "RHIDevice.hpp"

#include <memory>
#include <string>

namespace rhi {

[[nodiscard]] std::unique_ptr<RHIDevice> createRHIDevice(
    RHIGraphicsAPI api,
    std::string* errorMessage = nullptr);

[[nodiscard]] std::unique_ptr<RHIDevice> createInitializedRHIDevice(
    const RHIDeviceCreateDesc& desc,
    std::string* errorMessage = nullptr);

} // namespace rhi