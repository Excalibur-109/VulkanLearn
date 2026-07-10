#pragma once

#include "../RHIDefinitions.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace rhi {

/// 统一 RHI 设备初始化描述。公共层不包含任何图形 API 原生类型。
struct RHIDeviceCreateDesc {
    RHIBackendDesc backend{}; ///< API、验证、功能和并行帧数等通用配置。
    void* nativeWindow = nullptr; ///< D3D11/D3D12 后端解释为 HWND；离屏渲染可为空。

    std::uintptr_t vulkanSurface = 0; ///< 平台层已经创建的 VkSurfaceKHR，可选。
    std::function<std::uintptr_t(std::uintptr_t)> createVulkanSurface; ///< 输入 VkInstance，返回 VkSurfaceKHR。
    bool ownsVulkanSurface = false; ///< Vulkan 设备关闭时是否销毁 surface。
    std::vector<const char*> requiredVulkanInstanceExtensions;
    std::vector<const char*> optionalVulkanInstanceExtensions;
    std::vector<const char*> requiredVulkanDeviceExtensions;
    std::vector<const char*> optionalVulkanDeviceExtensions;
    std::vector<RHIQueueDesc> queues;

    bool allowSoftwareAdapter = true; ///< D3D 初始化失败时是否允许 WARP 回退。
};

} // namespace rhi