#pragma once

#include "RHID3D12Device.hpp"
#include "RHID3D12Backend.hpp"

namespace rhi {

/// 公共 RHIDevice 门面持有同目录内的原生 API 后端，不依赖任何目录外实现。
struct RHID3D12Device::Impl {
    RHID3D12Backend backend;
};

} // namespace rhi