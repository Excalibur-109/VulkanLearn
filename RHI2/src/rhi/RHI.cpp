#include "rhi/RHI.h"
#include <stdexcept>

namespace rhi {
std::unique_ptr<Device> createSoftwareDevice(const DeviceCreateInfo&);
std::unique_ptr<Device> createVulkanDevice(const DeviceCreateInfo&);
std::unique_ptr<Device> createD3D11Device(const DeviceCreateInfo&);
std::unique_ptr<Device> createD3D12Device(const DeviceCreateInfo&);

const char* backendName(Backend b) {
  switch (b) { case Backend::Software:return "Software"; case Backend::Vulkan:return "Vulkan"; case Backend::D3D11:return "D3D11"; case Backend::D3D12:return "D3D12"; }
  return "Unknown";
}

std::unique_ptr<Device> createDevice(const DeviceCreateInfo& info) {
  switch (info.backend) {
    case Backend::Vulkan: return createVulkanDevice(info);
    case Backend::D3D11: return createD3D11Device(info);
    case Backend::D3D12: return createD3D12Device(info);
    case Backend::Software: return createSoftwareDevice(info);
  }
  throw std::runtime_error("Unknown RHI backend");
}
}
