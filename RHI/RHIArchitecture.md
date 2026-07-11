# Standalone RHI

`RHI/` 是一套完全独立的跨图形 API 接口和后端实现。它不包含、不链接、不调用根目录中的旧 `Render*` 定义或后端。

## 目录职责

```text
RHI/
├── RHIDefinitions.hpp              # 独立句柄、枚举、资源描述、命令和帧数据
├── RHI.hpp                         # 上层唯一公共入口
├── Core/
│   ├── RHIDevice.hpp               # 跨 API 纯虚接口
│   ├── RHIDeviceDesc.hpp           # 跨 API 初始化参数
│   └── RHIDeviceFactory.*          # 按 RHIGraphicsAPI 创建设备
├── Vulkan/
│   ├── RHIVulkanDevice.*           # 公共门面
│   ├── RHIVulkanBackend.*          # 原生 Vulkan 后端主体
│   ├── RHIVulkanBackendPrivate.inl # 类型转换、资源池和内部状态
│   ├── RHIVulkanBackendCore.inl    # Instance/Device/Queue
│   ├── RHIVulkanBackendResources.inl
│   ├── RHIVulkanBackendPipelines.inl
│   ├── RHIVulkanBackendFrame.inl
│   ├── RHIVulkanBackendSyncSwapchain.inl
│   └── RHIVulkanBackendDestroy.inl
├── D3D11/                          # 与 Vulkan 相同的职责拆分
└── D3D12/                          # 与 Vulkan 相同的职责拆分
```

## 数据流

```text
Scene / ECS
    -> RenderCollector
    -> FrameRenderData
    -> RenderQueue
    -> RenderGraph
    -> RHIDevice
       -> RHIVulkanDevice -> RHIVulkanBackend -> Vulkan
       -> RHID3D11Device  -> RHID3D11Backend  -> D3D11
       -> RHID3D12Device  -> RHID3D12Backend  -> D3D12
```

`RHIDevice` 只消费 `RHIFramePacket`。它不访问游戏物体、相机、灯光和 ECS。

## 类型示例

- `RHIBuffer`、`RHITexture`：类型安全的逻辑资源句柄。
- `RHIMemory`、`RHIMemoryUsage`：GPU/CPU 内存访问方向。
- `RHIBufferDesc`、`RHITextureDesc`：资源创建描述。
- `RHIGraphicsPipelineDesc`：图形管线描述。
- `RHIFramePacket`：一帧已经整理好的 GPU 工作。

## 最小使用方式

```cpp
#include "RHI/RHI.hpp"

rhi::RHIDeviceCreateDesc desc{};
desc.backend.applicationName = "MyEngine";
desc.backend.preferredApi = rhi::RHIGraphicsAPI::Vulkan;

std::string error;
std::unique_ptr<rhi::RHIDevice> device = rhi::createInitializedRHIDevice(desc, &error);
if (device == nullptr) {
    throw std::runtime_error(error);
}

rhi::RHIBufferDesc bufferDesc{};
bufferDesc.size = 4096;
bufferDesc.memoryUsage = rhi::RHIMemory::GpuOnly;
rhi::RHIBuffer buffer = device->createBuffer(bufferDesc);
```