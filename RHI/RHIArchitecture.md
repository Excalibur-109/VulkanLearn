# Standalone RHI

`RHI/` 是完全独立的跨图形 API 接口和后端实现，不依赖根目录旧 `Render*` 代码。

## 当前结构

```text
RHI/
├── RHIDefinitions.hpp              # 独立句柄、枚举、资源描述、命令和帧数据
├── RHI.hpp                         # 上层唯一入口
├── Core/
│   ├── RHIDevice.hpp               # 唯一的非虚设备门面
│   ├── RHIDevice.cpp               # 使用 variant 分发到当前 Backend
│   ├── RHIDeviceDesc.hpp           # 跨 API 初始化参数
│   └── RHIDeviceFactory.*          # 创建和初始化 RHIDevice
├── Vulkan/
│   ├── RHIVulkanBackend.hpp/.cpp
│   ├── RHIVulkanBackendPrivate.inl
│   ├── RHIVulkanBackendCore.inl
│   ├── RHIVulkanBackendResources.inl
│   ├── RHIVulkanBackendPipelines.inl
│   ├── RHIVulkanBackendFrame.inl
│   ├── RHIVulkanBackendSyncSwapchain.inl
│   └── RHIVulkanBackendDestroy.inl
├── D3D11/                          # 与 Vulkan 相同的职责拆分
└── D3D12/                          # 与 Vulkan 相同的职责拆分
```

## 为什么只有一个 RHIDevice

三个原生 Backend 的公共方法签名一致，但它们不继承同一个虚基类。`RHIDevice` 内部使用：

```cpp
std::variant<
    std::monostate,
    RHIVulkanBackend,
    RHID3D11Backend,
    RHID3D12Backend
>;
```

上层只调用一次 `RHIDevice::createBuffer()`。`RHIDevice.cpp` 根据当前 variant 中保存的 Backend 直接转发，不再维护三套内容几乎相同的 Device 头文件，也不使用 `virtual/override`。

## 数据流

```text
Scene / ECS
    -> RenderCollector
    -> FrameRenderData
    -> RenderQueue
    -> RenderGraph
    -> RHIDevice
       -> RHIVulkanBackend
       -> RHID3D11Backend
       -> RHID3D12Backend
```

`RHIDevice` 只消费 `RHIFramePacket`，不访问游戏对象、相机、灯光或 ECS。

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