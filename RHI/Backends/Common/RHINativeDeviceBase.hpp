#pragma once

#include "RHI/RHIDevice.hpp"

namespace RHI
{

/**
 * 原生设备实现的公共基类。
 *
 * 该类只为尚未由某个后端支持的可选 RHI 功能提供一致的错误结果；资源、管线、
 * 命令和同步的实际实现必须由具体后端覆写。这样可以保证新增 RHI 接口时，后端
 * 不会悄悄遗漏实现，也不会以伪成功掩盖功能缺失。
 */
class RHINativeDeviceBase : public IRHIDevice
{
public:
    /** 使用已选择适配器的信息构造设备基础状态。 */
    explicit RHINativeDeviceBase(RHIAdapterInfo adapter)
        : m_adapter(std::move(adapter))
    {
    }

    /** 返回设备实际使用的适配器。 */
    [[nodiscard]] const RHIAdapterInfo& GetAdapterInfo() const noexcept override { return m_adapter; }

    /** 返回由具体后端填充的功能位。 */
    [[nodiscard]] const RHIDeviceFeatures& GetFeatures() const noexcept override { return m_features; }

    /** 返回由具体后端填充的数值限制。 */
    [[nodiscard]] const RHIDeviceLimits& GetLimits() const noexcept override { return m_limits; }

    /** 创建缓冲区的默认实现。 */
    RHIStatus CreateBuffer(const RHIBufferDesc&, RHIBufferHandle&) override { return Unsupported("缓冲区"); }
    /** 销毁缓冲区的默认实现。 */
    void DestroyBuffer(RHIBufferHandle) override {}
    /** 创建纹理的默认实现。 */
    RHIStatus CreateTexture(const RHITextureDesc&, RHITextureHandle&) override { return Unsupported("纹理"); }
    /** 销毁纹理的默认实现。 */
    void DestroyTexture(RHITextureHandle) override {}
    /** 创建纹理视图的默认实现。 */
    RHIStatus CreateTextureView(const RHITextureViewDesc&, RHITextureViewHandle&) override { return Unsupported("纹理视图"); }
    /** 销毁纹理视图的默认实现。 */
    void DestroyTextureView(RHITextureViewHandle) override {}
    /** 创建采样器的默认实现。 */
    RHIStatus CreateSampler(const RHISamplerDesc&, RHISamplerHandle&) override { return Unsupported("采样器"); }
    /** 销毁采样器的默认实现。 */
    void DestroySampler(RHISamplerHandle) override {}
    /** 映射缓冲区的默认实现。 */
    RHIStatus MapBuffer(RHIBufferHandle, std::uint64_t, std::uint64_t, void*&) override { return Unsupported("缓冲区映射"); }
    /** 刷新映射范围的默认实现。 */
    RHIStatus FlushMappedBufferRange(RHIBufferHandle, std::uint64_t, std::uint64_t) override { return Unsupported("映射范围刷新"); }
    /** 失效映射范围的默认实现。 */
    RHIStatus InvalidateMappedBufferRange(RHIBufferHandle, std::uint64_t, std::uint64_t) override { return Unsupported("映射范围失效"); }
    /** 取消映射的默认实现。 */
    RHIStatus UnmapBuffer(RHIBufferHandle) override { return Unsupported("缓冲区取消映射"); }
    /** 创建着色器模块的默认实现。 */
    RHIStatus CreateShaderModule(const RHIShaderModuleDesc&, RHIShaderModuleHandle&) override { return Unsupported("着色器模块"); }
    /** 销毁着色器模块的默认实现。 */
    void DestroyShaderModule(RHIShaderModuleHandle) override {}
    /** 创建绑定布局的默认实现。 */
    RHIStatus CreateBindLayout(const RHIBindLayoutDesc&, RHIBindLayoutHandle&) override { return Unsupported("绑定布局"); }
    /** 销毁绑定布局的默认实现。 */
    void DestroyBindLayout(RHIBindLayoutHandle) override {}
    /** 创建绑定集的默认实现。 */
    RHIStatus CreateBindSet(const RHIBindSetDesc&, RHIBindSetHandle&) override { return Unsupported("绑定集"); }
    /** 更新绑定集的默认实现。 */
    RHIStatus UpdateBindSet(RHIBindSetHandle, std::span<const RHIBindSetWrite>) override { return Unsupported("绑定集更新"); }
    /** 销毁绑定集的默认实现。 */
    void DestroyBindSet(RHIBindSetHandle) override {}
    /** 创建图形管线的默认实现。 */
    RHIStatus CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&, RHIGraphicsPipelineHandle&) override { return Unsupported("图形管线"); }
    /** 销毁图形管线的默认实现。 */
    void DestroyGraphicsPipeline(RHIGraphicsPipelineHandle) override {}
    /** 创建计算管线的默认实现。 */
    RHIStatus CreateComputePipeline(const RHIComputePipelineDesc&, RHIComputePipelineHandle&) override { return Unsupported("计算管线"); }
    /** 销毁计算管线的默认实现。 */
    void DestroyComputePipeline(RHIComputePipelineHandle) override {}
    /** 创建光线追踪管线的默认实现。 */
    RHIStatus CreateRayTracingPipeline(const RHIRayTracingPipelineDesc&, RHIRayTracingPipelineHandle&) override { return Unsupported("光线追踪管线"); }
    /** 销毁光线追踪管线的默认实现。 */
    void DestroyRayTracingPipeline(RHIRayTracingPipelineHandle) override {}
    /** 创建加速结构的默认实现。 */
    RHIStatus CreateAccelerationStructure(const RHIAccelerationStructureDesc&, RHIAccelerationStructureHandle&) override { return Unsupported("加速结构"); }
    /** 销毁加速结构的默认实现。 */
    void DestroyAccelerationStructure(RHIAccelerationStructureHandle) override {}
    /** 创建命令列表的默认实现。 */
    RHIStatus CreateCommandList(const RHICommandListDesc&, RHICommandListPtr&) override { return Unsupported("命令列表"); }
    /** 创建围栏的默认实现。 */
    RHIStatus CreateFence(const RHIFenceDesc&, RHIFenceHandle&) override { return Unsupported("围栏"); }
    /** 销毁围栏的默认实现。 */
    void DestroyFence(RHIFenceHandle) override {}
    /** 等待围栏的默认实现。 */
    RHIStatus WaitForFence(RHIFenceHandle, std::uint64_t) override { return Unsupported("围栏等待"); }
    /** 重置围栏的默认实现。 */
    RHIStatus ResetFence(RHIFenceHandle) override { return Unsupported("围栏重置"); }
    /** 创建信号量的默认实现。 */
    RHIStatus CreateSemaphore(const RHISemaphoreDesc&, RHISemaphoreHandle&) override { return Unsupported("信号量"); }
    /** 销毁信号量的默认实现。 */
    void DestroySemaphore(RHISemaphoreHandle) override {}
    /** 创建查询池的默认实现。 */
    RHIStatus CreateQueryPool(const RHIQueryPoolDesc&, RHIQueryPoolHandle&) override { return Unsupported("查询池"); }
    /** 销毁查询池的默认实现。 */
    void DestroyQueryPool(RHIQueryPoolHandle) override {}
    /** 创建呈现表面的默认实现。 */
    RHIStatus CreatePresentationSurface(const RHIPresentationSurfaceDesc&, RHISurfaceHandle&) override { return Unsupported("呈现表面"); }
    /** 销毁呈现表面的默认实现。 */
    void DestroyPresentationSurface(RHISurfaceHandle) override {}
    /** 创建交换链的默认实现。 */
    RHIStatus CreateSwapchain(const RHISwapchainDesc&, RHISwapchainHandle&) override { return Unsupported("交换链"); }
    /** 调整交换链的默认实现。 */
    RHIStatus ResizeSwapchain(RHISwapchainHandle, RHIExtent2D) override { return Unsupported("交换链调整"); }
    /** 销毁交换链的默认实现。 */
    void DestroySwapchain(RHISwapchainHandle) override {}
    /** 获取交换链图像的默认实现。 */
    RHIStatus AcquireNextImage(RHISwapchainHandle, std::uint64_t, RHIAcquiredImage&) override { return Unsupported("交换链图像获取"); }
    /** 提交命令的默认实现。 */
    RHIStatus Submit(RHIQueueType, const RHIQueueSubmitDesc&) override { return Unsupported("队列提交"); }
    /** 呈现图像的默认实现。 */
    RHIStatus Present(const RHIPresentDesc&) override { return Unsupported("图像呈现"); }
    /** 等待设备空闲的默认实现。 */
    RHIStatus WaitIdle() override { return RHIStatus::Ok(); }

public:
    /** 构造统一的可选功能未实现错误。 */
    [[nodiscard]] static RHIStatus Unsupported(std::string_view feature)
    {
        return RHIStatus::Error(RHIResult::Unsupported, std::string("当前后端尚未实现") + std::string(feature) + "。");
    }

    /** 具体后端填充的适配器信息。 */
    RHIAdapterInfo m_adapter;
    /** 具体后端填充的可选功能。 */
    RHIDeviceFeatures m_features{};
    /** 具体后端填充的数值限制。 */
    RHIDeviceLimits m_limits{};
};

} // namespace RHI
