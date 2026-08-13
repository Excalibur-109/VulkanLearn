#include "rhi/RHI.h"

#include <stdexcept>

namespace RHI {

// 不能把软件验证后端伪装成 D3D12。真实 D3D12 后端需要 DXGI Factory、
// Command Queue、Fence、Frame Allocator、描述符堆、资源状态屏障和交换链。
std::unique_ptr<Device> createD3D12Device(const DeviceCreateInfo&) {
    throw std::runtime_error(
        "D3D12 原生后端尚未实现；不会回退到 software 后端。"
        "请使用 d3d11，或先完成 D3D12 队列/栅栏/描述符堆实现。");
}

} // 命名空间 rhi
