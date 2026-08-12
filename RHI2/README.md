# RHI PBR Demo

这是一个只依赖当前目录的 C++17 渲染硬件接口学习工程。`include/rhi/RHI.h` 是唯一面向应用的合同：资源描述、着色器、图形管线、命令列表、提交/呈现和后端选择都在这里定义，完全不暴露 Vulkan、D3D11、D3D12 或 Win32 类型。

## 当前后端状态

| 后端 | 状态 | 说明 |
| --- | --- | --- |
| D3D11 | 已实现原生呈现 | `HWND`、DXGI 交换链、RTV/DSV/SRV、HLSL、输入布局、状态对象、阴影贴图、`DrawIndexed` 与 `Present`。 |
| Software | 参考路径 | 仅生成 CPU 参考图像，不创建 GPU 窗口，也不自称图形 API 后端。 |
| Vulkan | 未实现 | 选择该后端会明确失败，不会伪装为 Vulkan 或回退到 software。 |
| D3D12 | 未实现 | 选择该后端会明确失败，不会伪装为 D3D12 或回退到 software。 |

因此当前仓库是“D3D11 原生后端 + 跨 API RHI 的演进起点”，不是已经完成的三后端引擎。Vulkan 和 D3D12 必须分别完成真实设备、队列、交换链、资源视图、着色器、描述符和同步实现后，才能标记为支持。

## 目录

```text
include/rhi/RHI.h              跨 API 合同
src/rhi/RHI.cpp                后端工厂
src/backends/SoftwareBackend   可运行的验证后端（无 GPU/窗口依赖）
src/backends/VulkanBackend      Vulkan 适配边界
src/backends/D3D11Backend       D3D11 适配边界
src/backends/D3D12Backend       D3D12 适配边界
examples/pbr_demo/PbrMath.*     向量数学和 Cook-Torrance/GGX BRDF
examples/pbr_demo/SphereMesh.*  UV 球顶点/索引生成
examples/pbr_demo/SoftwareRenderer.* CPU 参考光栅化和 BMP/PPM 输出
examples/pbr_demo/Win32Window.* HWND、WM_PAINT 和驻留消息循环
examples/pbr_demo/main.cpp      RHI 资源创建、命令录制和程序入口
```

## 构建和运行

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\bin\pbr_demo.exe software
.\build\bin\pbr_demo.exe vulkan
.\build\bin\pbr_demo.exe d3d11
.\build\bin\pbr_demo.exe d3d12
```

命令行参数选择后端；Windows 下省略参数等价于 `d3d11`。D3D11 运行时会创建驻留的原生 GPU 窗口；软件参考路径使用 `software --no-wait`。

双击 `run_pbr_demo.bat` 或直接运行 exe 会创建一个真正驻留的 Win32 `HWND` 窗口。D3D11 后端通过 DXGI 交换链持续呈现球体、地面、阴影贴图和移动点光源，按 `Esc` 关闭窗口。`--no-wait` 仅用于 software 参考图像生成。

程序同时生成 `pbr_sphere.bmp` 和 `pbr_sphere.ppm` 作为离线验证输出。场景包含 1024x1024 D32 阴影贴图描述、阴影深度 pass、地面 pass 和球体 pass；软件验证后端会报告 draw、transition 和 binding 数量。

## 代码风格

公共 RHI 类型采用一项一行的字段布局：枚举值、结构体字段和虚函数都不压缩到同一行。每个模块的头文件先说明职责，再声明最小接口；实现文件按“辅助函数、公共函数、类方法”的顺序组织。注释解释 API 设计意图和跨后端映射，不重复描述显而易见的赋值操作。源码统一使用 UTF-8 编码，MSVC 构建配置已经显式启用 `/utf-8`。

## 后端边界

三个 API 后端都实现完整的 `Device`/`CommandList` 合同，因此应用层不需要条件编译。当前仓库提供的是可移植验证适配器：它将 native API 的生命周期边界集中在对应 `*Backend.cpp`，在没有 SDK 或窗口系统时委托给软件后端。接入真实设备时，只需在这些文件中替换：实例/工厂、队列与交换链、资源分配与视图、着色器编译、管线状态、栅栏/栅格化状态以及 `submit/present`；`RHI.h` 和 PBR 示例无需修改。

## PBR 要点

示例包含金属度、粗糙度、环境遮蔽、GGX 法线分布、Schlick 菲涅尔和 Smith 几何遮蔽项。`evaluatePBR` 是 CPU 参考实现，同时也是移植到 GLSL/HLSL 的逐项规范；顶点缓冲、帧常量、采样器、颜色/深度目标和图形管线均通过 RHI 创建。
