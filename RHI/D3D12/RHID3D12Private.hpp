#pragma once

#include "RHID3D12Device.hpp"
#include "../../RenderD3D12/RenderD3D12.hpp"

namespace rhi {

/// 兼容桥只存在于后端实现内部，上层不会看到旧 Renderer 类型。
struct RHID3D12Device::Impl {
    D3D12Renderer renderer;
};

} // namespace rhi