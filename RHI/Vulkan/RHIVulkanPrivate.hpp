#pragma once

#include "RHIVulkanDevice.hpp"
#include "../../RenderVulkan/RenderVulkan.hpp"

namespace rhi {

/// 兼容桥只存在于后端实现内部，上层不会看到旧 Renderer 类型。
struct RHIVulkanDevice::Impl {
    VulkanRenderer renderer;
};

} // namespace rhi