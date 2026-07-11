#include "RHID3D11Backend.hpp"

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
// 这个翻译单元仍然只生成一个 Direct3D 11 后端实现，但内部代码已经按 RHIVulkanBackend 的方式拆成
// private/core/resources/pipelines/sync-swapchain/frame/destroy 几个片段。这样可以保留 RHID3D11Backend 的私有 Impl，
// 同时避免一个大文件把 DXGI 设备选择、资源创建、pipeline 创建、RHIFramePacket 执行和销毁逻辑全部混在一起。
//
// 阅读顺序建议：
// 1. RHID3D11BackendPrivate.inl       - 通用句柄、HRESULT/UTF 转换、格式映射、Impl 资源结构和 adapter/capability 查询。
// 2. RHID3D11BackendCore.inl          - initialize/shutdown/capabilities/native handle。
// 3. RHID3D11BackendResources.inl     - buffer/texture/view/sampler/shader/bind group/layout 资源创建。
// 4. RHID3D11BackendPipelines.inl     - graphics/compute pipeline 创建。
// 5. RHID3D11BackendSyncSwapchain.inl - query/semaphore/fence/swapchain/acquire/submit/present。
// 6. RHID3D11BackendFrame.inl         - RHIFramePacket 到 D3D11 immediate context 调用的执行路径。
// 7. RHID3D11BackendDestroy.inl       - 所有 D3D11 COM 资源释放路径。
//
// 和 Vulkan 的最大区别：D3D11 没有显式 queue submit、image layout 和 descriptor set；
// 所以这里的 RHIFramePacket 执行重点是“设置 context 状态并立即发出 Draw/Dispatch”，而不是录制 VkCommandBuffer。

#include "RHID3D11BackendPrivate.inl"
#include "RHID3D11BackendCore.inl"
#include "RHID3D11BackendResources.inl"
#include "RHID3D11BackendPipelines.inl"
#include "RHID3D11BackendSyncSwapchain.inl"
#include "RHID3D11BackendFrame.inl"
#include "RHID3D11BackendDestroy.inl"

} // namespace rhi
