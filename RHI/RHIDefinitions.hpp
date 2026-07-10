#pragma once

// Unified RHI 的唯一公共数据定义入口。
//
// 当前阶段底层 Vulkan/D3D11/D3D12 驱动仍使用旧 RenderDefinitions 类型。这里通过
// 类型别名建立零成本兼容层：上层和新 RHI 代码只使用 RHI* 名称，适配器可以直接把
// 描述传给旧驱动。后续迁移底层实现时，只需替换本文件内部定义，不影响上层接口。
#include "../RenderDefinitions.hpp"

namespace rhi {

// 基础数值和通用配置。
using RHIUInt32 = u32;
using RHIInt32 = i32;
using RHIUInt64 = u64;
using RHIGraphicsAPI = GraphicsApi;
using RHIBackendDesc = RenderBackendDesc;
using RHIQueueDesc = QueueDesc;
using RHICapabilities = RenderCapabilities;
using RHIFormat = Format;
using RHIExtent2D = Extent2D;

// 资源句柄。RHI* 句柄是逻辑资源 id，不直接暴露 VkBuffer 或 ID3D12Resource*。
using RHIBuffer = BufferHandle;
using RHITexture = TextureHandle;
using RHITextureView = TextureViewHandle;
using RHISampler = SamplerHandle;
using RHIShader = ShaderHandle;
using RHIPipelineLayout = PipelineLayoutHandle;
using RHIPipeline = PipelineHandle;
using RHIRenderPass = RenderPassHandle;
using RHIFramebuffer = FramebufferHandle;
using RHISwapchain = SwapchainHandle;
using RHIBindGroupLayout = BindGroupLayoutHandle;
using RHIBindGroup = BindGroupHandle;
using RHIQueryPool = QueryPoolHandle;
using RHIPipelineCache = PipelineCacheHandle;
using RHISemaphore = SemaphoreHandle;
using RHIFence = FenceHandle;

// 内存策略。RHIMemory 表示资源内存访问方向，不是可直接解引用的 CPU 指针。
using RHIMemory = MemoryUsage;
using RHIMemoryUsage = MemoryUsage;

// 资源创建描述。
using RHIBufferDesc = BufferDesc;
using RHITextureDesc = TextureDesc;
using RHITextureViewDesc = TextureViewDesc;
using RHISamplerDesc = SamplerDesc;
using RHIShaderDesc = ShaderDesc;
using RHIBindGroupLayoutDesc = BindGroupLayoutDesc;
using RHIBindGroupDesc = BindGroupDesc;
using RHIPipelineLayoutDesc = PipelineLayoutDesc;
using RHIPipelineCacheDesc = PipelineCacheDesc;
using RHIGraphicsPipelineDesc = GraphicsPipelineDesc;
using RHIComputePipelineDesc = ComputePipelineDesc;
using RHIQueryPoolDesc = QueryPoolDesc;
using RHISemaphoreDesc = SemaphoreDesc;
using RHIFenceDesc = FenceDesc;
using RHISwapchainDesc = SwapchainDesc;

// 命令、提交和整帧描述。
using RHIQueueSubmitDesc = QueueSubmitDesc;
using RHIPresentDesc = PresentDesc;
using RHIFramePacket = FramePacket;
using RHIDrawCommand = DrawCommand;
using RHIDrawIndexedCommand = DrawIndexedCommand;
using RHIDispatchCommand = DispatchCommand;
using RHIRenderPassWorkload = RenderPassWorkload;

} // namespace rhi