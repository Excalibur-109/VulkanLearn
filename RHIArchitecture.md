# Standalone RHI

`RHI/` 是完全独立的跨图形 API 接口和后端实现，不依赖根目录旧 `Render*` 代码。

## 当前结构

```text
RHI/
├── RHIDefinitions.hpp              # 独立句柄、枚举、资源描述、命令和帧数据
├── RHI.hpp                         # 上层唯一入口
├── Core/
│   ├── RHIDevice.hpp               # 唯一的非虚设备门面
│   ├── RHIDevice.cpp               # 使用 variant 分发到当前 API 实现
│   ├── RHIDeviceDesc.hpp           # 跨 API 初始化参数
│   └── RHIDeviceFactory.*          # 创建和初始化 RHIDevice
├── Vulkan/
│   ├── RHIVulkan.hpp/.cpp
│   ├── RHIVulkanPrivate.inl
│   ├── RHIVulkanCore.inl
│   ├── RHIVulkanResources.inl
│   ├── RHIVulkanPipelines.inl
│   ├── RHIVulkanFrame.inl
│   ├── RHIVulkanSyncSwapchain.inl
│   └── RHIVulkanDestroy.inl
├── D3D11/                          # 与 Vulkan 相同的职责拆分
└── D3D12/                          # 与 Vulkan 相同的职责拆分
```

## 为什么只有一个 RHIDevice

三个原生 API 实现 的公共方法签名一致，但它们不继承同一个虚基类。`RHIDevice` 内部使用：

```cpp
std::variant<
    std::monostate,
    RHIVulkan,
    RHID3D11,
    RHID3D12
>;
```

上层只调用一次 `RHIDevice::CreateBuffer()`。`RHIDevice.cpp` 根据当前 variant 中保存的 API 实现 直接转发，不再维护三套内容几乎相同的 Device 头文件，也不使用 `virtual/override`。

## 数据流

```text
Scene / ECS
    -> RenderCollector
    -> FrameRenderData
    -> RenderQueue
    -> RenderGraph
    -> RHIDevice
       -> RHIVulkan
       -> RHID3D11
       -> RHID3D12
```

`RHIDevice` 只消费 `RHIFramePacket`，不访问游戏对象、相机、灯光或 ECS。

## Vulkan 帧生命周期

Vulkan 后端按 `RHIBackendDesc::framesInFlight` 创建并轮转复用 `FrameContext`：

```text
FrameContext
├── VkCommandBuffer
├── timeline completionValue
└── 本帧 staging resources
```

Vulkan 后端创建一个全局 Timeline Semaphore。每次 frame submit 都 signal 一个
递增值，FrameContext 只记录自己的 completionValue；CPU 在 `AcquireNextImage`
前通过 `vkWaitSemaphores` 只等待即将复用的帧槽位，不会在每次
`vkQueueSubmit` 后立即等待 GPU。Buffer、Texture、Pipeline 等普通资源销毁时，
RHI 先让逻辑句柄失效，再按 submission serial 延迟释放 Vulkan 对象。

`vkAcquireNextImageKHR` 和传统 `vkQueuePresentKHR` 属于 WSI 边界，仍使用
Binary Semaphore。Timeline 用于内部 frame completion 和普通 queue 依赖，不能把
WSI 的 Binary Signal 机械替换成 Timeline。

低层 `Submit` 可能使用 graphics 之外的队列，无法由 FrameContext fence 跟踪；
这种情况下销毁会退化为 device-idle，以正确性优先。同步对象和 swapchain 也使用
device-idle 销毁路径，避免和 present 操作发生生命周期冲突。

Texture 状态跟踪同时保存 state、pipeline stage 和 access mask。RenderGraph 没有
显式给出 stage/access 时，Vulkan 后端会根据 `RHIResourceState` 推导，不再默认
把所有 transition 都扩大为 `ALL_COMMANDS -> ALL_COMMANDS`。

## 最小使用方式

```cpp
#include "RHI/RHI.hpp"

rhi::RHIDeviceCreateDesc desc{};
desc.backend.applicationName = "MyEngine";
desc.backend.preferredApi = rhi::RHIGraphicsAPI::Vulkan;

std::string error;
std::unique_ptr<rhi::RHIDevice> device = rhi::CreateInitializedRHIDevice(desc, &error);
if (device == nullptr) {
    throw std::runtime_error(error);
}

rhi::RHIBufferDesc bufferDesc{};
bufferDesc.size = 4096;
bufferDesc.memoryUsage = rhi::RHIMemory::GpuOnly;
rhi::RHIBuffer buffer = device->CreateBuffer(bufferDesc);
```






