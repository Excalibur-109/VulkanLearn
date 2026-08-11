# RHI

这是一个 C++20 的后端无关渲染硬件接口。抽象层不包含、链接或引用具体图形 API；Vulkan、D3D12 和 D3D11 作为可选的原生后端模块提供。

代码以功能边界拆分：

- `RHICommon`、`RHIHandles`：结果、格式、状态、强类型句柄和基础几何类型。
- `RHIResources`、`RHISynchronization`：资源、呈现、交换链、同步、查询与显式屏障。
- `RHIDescriptors`、`RHIShaders`、`RHIPipeline`：资源绑定、着色器、图形/计算/光线追踪/网格着色管线。
- `RHICommands`、`RHIDevice`：命令录制与完整设备对象生命周期。
- `RHIBackend`、`RHIContext`：后端工厂注册与事务式运行时切换。
- `RHIRenderGraph`：pass 资源依赖、自动状态转换、临时资源和保守跨队列同步。
- `PBR/RHIPBRRenderer`：共享的金属度-粗糙度前向 PBR 渲染器，包含 UV 球网格、Cook-Torrance BRDF、GGX、Smith 和 Schlick 项。

## 学习入口

建议先阅读 [Docs/学习路线.md](Docs/学习路线.md)。文档给出了推荐的源码顺序、一次 PBR 绘制从示例到 GPU 的调用链，以及 RHI 概念与 D3D11、D3D12、Vulkan 原生对象的对应关系。

本仓库当前刻意聚焦于一条完整、可验证的最小离屏 PBR 路径。它是学习和接入自定义引擎的起点，而不是声称已经包含窗口交换链、IBL、阴影、资源流送或完整渲染器功能的成品引擎。

## PBR 验证

在 Windows 上启用三个原生后端且系统可找到 Vulkan SDK 的 `glslc` 时，`rhi_pbr_offscreen_check` 会依次在 D3D11、D3D12 与 Vulkan 上完成一次真实的离屏 PBR 绘制。它覆盖设备创建、几何上传、HLSL 编译或 SPIR-V 加载、颜色附件、图形管线、命令提交和等待。

```powershell
cmake --build Build --config Debug --target rhi_pbr_offscreen_check
.\Build\Debug\rhi_pbr_offscreen_check.exe
```

当前 PBR 前端是无纹理的金属度-粗糙度直接光照实现。环境贴图 IBL、glTF 资源加载、阴影、后处理与窗口交换链是建立在这条已验证图形路径之上的下一层功能，尚未声称已经实现。

## 后端接入

实现 `IRHIBackendFactory`、`IRHIBackend` 和 `IRHIDevice`，再将工厂注册到 `RHIBackendRegistry`。后端 ID 是调用方自定义的稳定字符串，不受 RHI 限制。

```cpp
RHI::RHIBackendRegistry registry;
registry.RegisterFactory(CreateMyBackendFactory());

RHI::RHIContext context(registry);
context.Initialize({.Backend = {.Value = "my.backend"}});

// 切换会先创建新后端和新设备；任一创建失败时旧设备保持可用。
context.SwitchBackend({.Backend = {.Value = "another.backend"}});
```

使用 `GetActiveDevice()` 获取设备快照。快照包含 generation；后端切换成功后 generation 会增加。不要跨 generation 混用资源或句柄。默认切换策略会等待旧设备空闲，因此切换完成后可以安全释放旧资源。

## 构建

```powershell
cmake -S . -B Build -DRHI_BUILD_EXAMPLES=ON
cmake --build Build --config Debug
```

`rhi_compile_check` 是一个不依赖任何后端的编译验证程序。实际渲染需要应用注册至少一个后端实现。
