#pragma once

#include "RHID3D11Device.hpp"
#include "../../RenderD3D11/RenderD3D11.hpp"

namespace rhi {

/// 兼容桥只存在于后端实现内部，上层不会看到旧 Renderer 类型。
struct RHID3D11Device::Impl {
    D3D11Renderer renderer;
};

} // namespace rhi