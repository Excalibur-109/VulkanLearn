#include "rhi/RHI.h"

#include <stdexcept>

namespace RHI {

// 不能把软件验证后端伪装成 Vulkan。真实 Vulkan 后端需要实例、物理设备
// 选择、设备队列、KHR 交换链、SPIR-V 编译、描述符集和同步对象的完整实现。
std::unique_ptr<Device> createVulkanDevice(const DeviceCreateInfo&) {
    throw std::runtime_error(
        "Vulkan 原生后端尚未实现；不会回退到 software 后端。"
        "请使用 d3d11，或先完成 Vulkan 设备/交换链/命令缓冲实现。");
}

} // 命名空间 rhi
