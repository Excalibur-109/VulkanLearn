# Unified RHI

`RHI/` 是基于 `RHIDefinitions.hpp` 的统一图形接口。新代码的文件名、类名、结构体名和句柄名统一使用 `RHI` 前缀，避免与旧 `Render*` 实现混用。

## 目录职责

```text
RHI/
├── RHIDefinitions.hpp          # RHI 公共句柄、描述、命令和帧数据类型
├── RHI.hpp                     # 上层唯一入口
├── Core/
│   ├── RHIDevice.hpp           # 统一设备接口
│   ├── RHIDeviceDesc.hpp       # 跨 API 初始化描述
│   └── RHIDeviceFactory.*      # 后端创建工厂
├── Vulkan/
│   ├── RHIVulkanDevice.*       # 初始化、关闭、能力
│   ├── RHIVulkanResources.cpp  # Buffer/Texture/Sampler/Shader/BindGroup
│   ├── RHIVulkanPipelines.cpp  # PipelineLayout/Graphics/Compute Pipeline
│   ├── RHIVulkanFrame.cpp      # Submit/FramePacket/WaitIdle
│   └── RHIVulkanSwapchain.cpp  # Swapchain/Acquire/Present/Sync/Query
├── D3D11/
└── D3D12/
```

三个后端使用相同的职责拆分，可以逐文件比较 Vulkan、D3D11 和 D3D12 的差异。

## RHI 数据流

```text
Scene / ECS
    -> RenderCollector
    -> FrameRenderData
    -> RenderQueue
    -> RenderGraph
    -> RHIDevice
       -> RHIVulkanDevice
       -> RHID3D11Device
       -> RHID3D12Device
```

`RHIDevice` 不访问游戏对象、相机或灯光，只消费已经整理好的 `RHIFramePacket`。

## 命名示例

- `RHIBuffer`：Buffer 逻辑句柄。
- `RHITexture`：Texture 逻辑句柄。
- `RHIMemory`：资源内存访问策略。
- `RHIBufferDesc`：Buffer 创建描述。
- `RHIGraphicsPipelineDesc`：图形管线创建描述。
- `RHIFramePacket`：提交给后端的一帧渲染工作。

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
rhi::RHIBuffer vertexBuffer = device->createBuffer(bufferDesc);
```

## 当前兼容边界

新 RHI 公共接口只暴露 `RHI*` 名称。三个 `RHI*Private.hpp` 在实现内部临时复用旧 Renderer，保证现有功能稳定。后续将底层实现迁入 RHI 时，上层接口不需要变化。