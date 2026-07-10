#include "RenderD3D12.hpp"

#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

// 学习导读：
// Direct3D 12 和 Vulkan 一样属于显式图形 API：资源状态转换、descriptor、command list、
// queue submit、fence 都要由后端明确管理。它不像 D3D11 immediate context 那样“设置状态后立刻绘制”，
// 也不像当前 RenderVulkan 后端那样已有完整 RenderGraph command buffer 录制路径。
//
// 本文件只负责把 D3D12Renderer 的实现片段按阅读顺序装配起来：
// 1. RenderD3D12Private.inl       - 句柄、HRESULT、格式/状态映射、Impl 资源结构、adapter/capability 查询。
// 2. RenderD3D12Core.inl          - debug layer、factory/device/queue/command list/fence 初始化和关闭。
// 3. RenderD3D12Resources.inl     - buffer/texture/view/sampler/shader/bind group/layout 资源创建。
// 4. RenderD3D12Pipelines.inl     - root signature、graphics/compute PSO 创建。
// 5. RenderD3D12SyncSwapchain.inl - query/semaphore/fence/swapchain/acquire/submit/present。
// 6. RenderD3D12Frame.inl         - FramePacket 入口，目前明确标出尚未完成的 command recording 路径。
// 7. RenderD3D12Destroy.inl       - 所有 D3D12 COM 资源和 Win32 event 释放路径。
//
// 读 D3D12 时建议重点对比 Vulkan：
// - VkDescriptorSetLayout / VkPipelineLayout 约等于 D3D12 root signature + descriptor table。
// - VkCommandBuffer 约等于 ID3D12GraphicsCommandList。
// - VkFence/timeline semaphore 的职责在 D3D12 中主要由 ID3D12Fence 承担。
// - VkImageLayout 的显式转换约等于 D3D12_RESOURCE_BARRIER transition。

#include "RenderD3D12Private.inl"
#include "RenderD3D12Core.inl"
#include "RenderD3D12Resources.inl"
#include "RenderD3D12Pipelines.inl"
#include "RenderD3D12SyncSwapchain.inl"
#include "RenderD3D12Frame.inl"
#include "RenderD3D12Destroy.inl"
