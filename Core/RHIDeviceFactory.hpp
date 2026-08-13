#pragma once

#include "RHIDevice.hpp"

#include <memory>
#include <string>

namespace RHI {

[[nodiscard]] std::unique_ptr<RHIDevice> CreateRHIDevice(
    RHIGraphicsAPI api,
    std::string* errorMessage = nullptr);

[[nodiscard]] std::unique_ptr<RHIDevice> CreateInitializedRHIDevice(
    const RHIDeviceCreateDesc& desc,
    std::string* errorMessage = nullptr);

} // namespace RHI




