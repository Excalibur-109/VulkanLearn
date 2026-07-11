#pragma once

#include "RHIVulkanDevice.hpp"
#include "RHIVulkanBackend.hpp"

namespace rhi {

/// 公共 RHIDevice 门面持有同目录内的原生 API 后端，不依赖任何目录外实现。
struct RHIVulkanDevice::Impl {
    RHIVulkanBackend backend;
};

} // namespace rhi