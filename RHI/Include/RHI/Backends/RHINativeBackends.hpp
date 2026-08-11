#pragma once

#include "RHI/RHIBackend.hpp"

namespace RHI
{

/** 向注册中心添加 Vulkan 后端工厂。 */
RHI_API RHIStatus RegisterVulkanBackend(RHIBackendRegistry& registry);

/** 向注册中心添加 Direct3D 12 后端工厂。 */
RHI_API RHIStatus RegisterD3D12Backend(RHIBackendRegistry& registry);

/** 向注册中心添加 Direct3D 11 后端工厂。 */
RHI_API RHIStatus RegisterD3D11Backend(RHIBackendRegistry& registry);

} // namespace RHI
