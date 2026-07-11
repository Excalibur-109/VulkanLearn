#include "RHID3D12Backend.hpp"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace rhi {

// 学习导读：
// Direct3D 12 和 Vulkan 一样属于显式图形 API：资源状态转换、descriptor、command list、
// queue submit、fence 都要由后端明确管理。它不像 D3D11 immediate context 那样“设置状态后立刻绘制”，
// 也不像当前 RHIVulkanBackend 后端那样已有完整 RenderGraph command buffer 录制路径。
//
// 本文件只负责把 RHID3D12Backend 的实现片段按阅读顺序装配起来：
// 1. RHID3D12BackendPrivate.inl       - 句柄、HRESULT、格式/状态映射、Impl 资源结构、adapter/capability 查询。
// 2. RHID3D12BackendCore.inl          - debug layer、factory/device/queue/command list/fence 初始化和关闭。
// 3. RHID3D12BackendResources.inl     - buffer/texture/view/sampler/shader/bind group/layout 资源创建。
// 4. RHID3D12BackendPipelines.inl     - root signature、graphics/compute PSO 创建。
// 5. RHID3D12BackendSyncSwapchain.inl - query/semaphore/fence/swapchain/acquire/submit/present。
// 6. RHID3D12BackendFrame.inl         - RHIFramePacket 入口，目前明确标出尚未完成的 command recording 路径。
// 7. RHID3D12BackendDestroy.inl       - 所有 D3D12 COM 资源和 Win32 event 释放路径。
//
// 读 D3D12 时建议重点对比 Vulkan：
// - VkDescriptorSetLayout / VkPipelineLayout 约等于 D3D12 root signature + descriptor table。
// - VkCommandBuffer 约等于 ID3D12GraphicsCommandList。
// - VkFence/timeline semaphore 的职责在 D3D12 中主要由 ID3D12Fence 承担。
// - VkImageLayout 的显式转换约等于 D3D12_RESOURCE_BARRIER transition。

#include "RHID3D12BackendPrivate.inl"
#include "RHID3D12BackendCore.inl"
#include "RHID3D12BackendResources.inl"
#include "RHID3D12BackendPipelines.inl"
#include "RHID3D12BackendSyncSwapchain.inl"
#include "RHID3D12BackendFrame.inl"
#include "RHID3D12BackendDestroy.inl"

} // namespace rhi
