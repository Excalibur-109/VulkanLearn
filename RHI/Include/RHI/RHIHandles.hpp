#pragma once

#include "RHI/RHICommon.hpp"

#include <cstdint>
#include <functional>

namespace RHI
{

/** 实现由后端拥有的强类型对象句柄。*/
template <typename TTag>
struct RHIHandle final
{
    /** 无效句柄值。*/
    static constexpr std::uint64_t InvalidValue = 0;

    /** 由所属设备分配的不透明值。*/
    std::uint64_t Value = InvalidValue;

    /** 当前句柄标识存活的后端对象时返回 true。*/
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Value != InvalidValue; }

    /** 两个句柄在同一设备中标识同一对象时返。true。*/
    [[nodiscard]] constexpr bool operator==(const RHIHandle& other) const noexcept { return Value == other.Value; }
};

/** 缓冲区句柄标签。*/
struct RHIBufferTag final {};
/** 纹理句柄标签。*/
struct RHITextureTag final {};
/** 纹理视图句柄标签。*/
struct RHITextureViewTag final {};
/** 采样器句柄标签。*/
struct RHISamplerTag final {};
/** 着色器模块句柄标签。*/
struct RHIShaderModuleTag final {};
/** 绑定布局句柄标签。*/
struct RHIBindLayoutTag final {};
/** 绑定集句柄标签。*/
struct RHIBindSetTag final {};
/** 图形管线句柄标签。*/
struct RHIGraphicsPipelineTag final {};
/** 计算管线句柄标签。*/
struct RHIComputePipelineTag final {};
/** 光线追踪管线句柄标签。*/
struct RHIRayTracingPipelineTag final {};
/** 命令列表句柄标签。*/
struct RHICommandListTag final {};
/** 命令分配器句柄标签。*/
struct RHICommandAllocatorTag final {};
/** 围栏句柄标签。*/
struct RHIFenceTag final {};
/** 信号量句柄标签。*/
struct RHISemaphoreTag final {};
/** 呈现表面句柄标签。*/
struct RHISurfaceTag final {};
/** 交换链句柄标签。*/
struct RHISwapchainTag final {};
/** 加速结构句柄标签。*/
struct RHIAccelerationStructureTag final {};
/** 查询池句柄标签。*/
struct RHIQueryPoolTag final {};

/** 缓冲区资源的不透明句柄。*/
using RHIBufferHandle = RHIHandle<RHIBufferTag>;
/** 纹理资源的不透明句柄。*/
using RHITextureHandle = RHIHandle<RHITextureTag>;
/** 纹理视图的不透明句柄。*/
using RHITextureViewHandle = RHIHandle<RHITextureViewTag>;
/** 采样器状态的不透明句柄。*/
using RHISamplerHandle = RHIHandle<RHISamplerTag>;
/** 已编译着色器模块的不透明句柄。*/
using RHIShaderModuleHandle = RHIHandle<RHIShaderModuleTag>;
/** 绑定组布局的不透明句柄。*/
using RHIBindLayoutHandle = RHIHandle<RHIBindLayoutTag>;
/** 绑定组实例的不透明句柄。*/
using RHIBindSetHandle = RHIHandle<RHIBindSetTag>;
/** 图形管线的不透明句柄。*/
using RHIGraphicsPipelineHandle = RHIHandle<RHIGraphicsPipelineTag>;
/** 计算管线的不透明句柄。*/
using RHIComputePipelineHandle = RHIHandle<RHIComputePipelineTag>;
/** 光线追踪管线的不透明句柄。*/
using RHIRayTracingPipelineHandle = RHIHandle<RHIRayTracingPipelineTag>;
/** 命令列表的不透明句柄。*/
using RHICommandListHandle = RHIHandle<RHICommandListTag>;
/** 命令分配器的不透明句柄。*/
using RHICommandAllocatorHandle = RHIHandle<RHICommandAllocatorTag>;
/** GPU 围栏的不透明句柄。*/
using RHIFenceHandle = RHIHandle<RHIFenceTag>;
/** GPU 信号量的不透明句柄。*/
using RHISemaphoreHandle = RHIHandle<RHISemaphoreTag>;
/** 操作系统呈现表面的不透明句柄。*/
using RHISurfaceHandle = RHIHandle<RHISurfaceTag>;
/** 呈现交换链的不透明句柄。*/
using RHISwapchainHandle = RHIHandle<RHISwapchainTag>;
/** 加速结构的不透明句柄。*/
using RHIAccelerationStructureHandle = RHIHandle<RHIAccelerationStructureTag>;
/** 时间戳或遮挡查询池的不透明句柄。*/
using RHIQueryPoolHandle = RHIHandle<RHIQueryPoolTag>;

} // namespace RHI

/** 按不透明值哈希强类型 RHI 句柄。*/
template <typename TTag>
struct std::hash<RHI::RHIHandle<TTag>> final
{
    /** 计算强类。RHI 句柄的哈希值。*/
    [[nodiscard]] std::size_t operator()(const RHI::RHIHandle<TTag>& handle) const noexcept
    {
        return std::hash<std::uint64_t>{}(handle.Value);
    }
};
