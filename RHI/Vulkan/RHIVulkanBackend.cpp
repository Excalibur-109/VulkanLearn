#include "RHIVulkanBackend.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace rhi {

// 学习导读：
// 这个翻译单元仍然只生成一个 Vulkan 后端实现，但内部代码已经按 simple_engine 的组织方式拆成
// private/device/resources/pipelines/swapchain/frame/destroy 几个片段。这样可以保留 RHIVulkanBackend 的私有 Impl，
// 同时避免一个 3000 行文件把设备选择、资源创建、pipeline 创建和命令录制全部混在一起。
//
// 阅读顺序建议：
// 1. RHIVulkanBackendPrivate.inl       - 通用句柄、格式转换、队列/设备查询和 Impl 资源结构。
// 2. RHIVulkanBackendCore.inl          - initialize/shutdown/capabilities/native handle。
// 3. RHIVulkanBackendResources.inl     - buffer/texture/view/sampler/shader/descriptor 资源创建。
// 4. RHIVulkanBackendPipelines.inl     - graphics/compute pipeline 创建。
// 5. RHIVulkanBackendSyncSwapchain.inl - query/semaphore/fence/swapchain/acquire/submit/present。
// 6. RHIVulkanBackendFrame.inl         - RHIFramePacket 到 Vulkan command buffer 的录制和提交。
// 7. RHIVulkanBackendDestroy.inl       - 所有 Vulkan 资源销毁路径。

#include "RHIVulkanBackendPrivate.inl"
#include "RHIVulkanBackendCore.inl"
#include "RHIVulkanBackendResources.inl"
#include "RHIVulkanBackendPipelines.inl"
#include "RHIVulkanBackendSyncSwapchain.inl"
#include "RHIVulkanBackendFrame.inl"
#include "RHIVulkanBackendDestroy.inl"

} // namespace rhi
