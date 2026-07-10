#include "RenderD3D11.hpp"

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
// 这个翻译单元仍然只生成一个 Direct3D 11 后端实现，但内部代码已经按 RenderVulkan 的方式拆成
// private/core/resources/pipelines/sync-swapchain/frame/destroy 几个片段。这样可以保留 D3D11Renderer 的私有 Impl，
// 同时避免一个大文件把 DXGI 设备选择、资源创建、pipeline 创建、FramePacket 执行和销毁逻辑全部混在一起。
//
// 阅读顺序建议：
// 1. RenderD3D11Private.inl       - 通用句柄、HRESULT/UTF 转换、格式映射、Impl 资源结构和 adapter/capability 查询。
// 2. RenderD3D11Core.inl          - initialize/shutdown/capabilities/native handle。
// 3. RenderD3D11Resources.inl     - buffer/texture/view/sampler/shader/bind group/layout 资源创建。
// 4. RenderD3D11Pipelines.inl     - graphics/compute pipeline 创建。
// 5. RenderD3D11SyncSwapchain.inl - query/semaphore/fence/swapchain/acquire/submit/present。
// 6. RenderD3D11Frame.inl         - FramePacket 到 D3D11 immediate context 调用的执行路径。
// 7. RenderD3D11Destroy.inl       - 所有 D3D11 COM 资源释放路径。
//
// 和 Vulkan 的最大区别：D3D11 没有显式 queue submit、image layout 和 descriptor set；
// 所以这里的 FramePacket 执行重点是“设置 context 状态并立即发出 Draw/Dispatch”，而不是录制 VkCommandBuffer。

#include "RenderD3D11Private.inl"
#include "RenderD3D11Core.inl"
#include "RenderD3D11Resources.inl"
#include "RenderD3D11Pipelines.inl"
#include "RenderD3D11SyncSwapchain.inl"
#include "RenderD3D11Frame.inl"
#include "RenderD3D11Destroy.inl"
