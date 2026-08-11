#pragma once

#include "RHI/RHIPipeline.hpp"

#include <memory>
#include <optional>
#include <span>

namespace RHI
{

/** 定义命令列表的录制能力。*/
enum class RHICommandListLevel : std::uint8_t
{
    /** 可直接提交到队列。*/
    Primary,
    /** 可由主命令列表执行。*/
    Secondary,
};

/** 定义命令列表生命周期状态。*/
enum class RHICommandListState : std::uint8_t
{
    /** 命令列表尚未开始录制。*/
    Initial,
    /** 可以录制命令。*/
    Recording,
    /** 录制完成，列表可以提交。*/
    Executable,
    /** 命令列表等待队列执行。*/
    Pending,
    /** 命令列表重置前不可使用。*/
    Invalid,
};

/** 定义多重采样颜色数据解析到单采样目标的方式。*/
enum class RHIResolveMode : std::uint8_t
{
    /** 对所有覆盖采样求平均。*/
    Average,
    /** 选择采样零。*/
    SampleZero,
    /** 选择最小采样值。*/
    Min,
    /** 选择最大采样值。*/
    Max,
};

/** 描述动态渲染中的一个颜色附件。*/
struct RHIRenderingColorAttachment final
{
    /** 作为颜色目标的纹理视图。*/
    RHITextureViewHandle View{};
    /** 可选的单采样解析目标。*/
    RHITextureViewHandle ResolveView{};
    /** ResolveView 有效时采用的解析操作。*/
    RHIResolveMode ResolveMode = RHIResolveMode::Average;
    /** 附件初始化行为。*/
    RHILoadOp LoadOp = RHILoadOp::Load;
    /** 附件最终内容处理行为。*/
    RHIStoreOp StoreOp = RHIStoreOp::Store;
    /** LoadOp 为 Clear 时应用的颜色。*/
    RHIClearColorValue ClearValue{};
};

/** 描述动态渲染中的一个深。模板附件。*/
struct RHIRenderingDepthStencilAttachment final
{
    /** 作为深度/模板目标的纹理视图。*/
    RHITextureViewHandle View{};
    /** 深度初始化行为。*/
    RHILoadOp DepthLoadOp = RHILoadOp::Load;
    /** 深度最终内容处理行为。*/
    RHIStoreOp DepthStoreOp = RHIStoreOp::Store;
    /** 模板初始化行为。*/
    RHILoadOp StencilLoadOp = RHILoadOp::Load;
    /** 模板最终内容处理行为。*/
    RHIStoreOp StencilStoreOp = RHIStoreOp::Store;
    /** 加载操作。Clear 时应用的值。*/
    RHIClearDepthStencilValue ClearValue{};
    /** 禁用深度读写时为 true。*/
    bool DepthReadOnly = false;
    /** 禁用模板读写时为 true。*/
    bool StencilReadOnly = false;
};

/** 描述一个动态渲染区域。*/
struct RHIRenderingDesc final
{
    /** 此渲染区域覆盖的像素矩形。*/
    RHIScissor RenderArea{};
    /** 同时渲染的视图层数量。*/
    std::uint32_t LayerCount = 1;
    /** 颜色附件槽位。*/
    std::vector<RHIRenderingColorAttachment> ColorAttachments;
    /** 可选深。模板附件。*/
    std::optional<RHIRenderingDepthStencilAttachment> DepthStencilAttachment;
};

/** 描述一次绘制的一个顶点缓冲区绑定。*/
struct RHIVertexBufferBinding final
{
    /** 包含顶点元素的缓冲区。*/
    RHIBufferHandle Buffer{};
    /** 到首元素的字节偏移。*/
    std::uint64_t Offset = 0;
};

/** 描述索引绘制使用的索引缓冲区绑定。*/
struct RHIIndexBufferBinding final
{
    /** 包含索引元素的缓冲区。*/
    RHIBufferHandle Buffer{};
    /** 到首索引的字节偏移。*/
    std::uint64_t Offset = 0;
    /** 每个索引元素的编码。*/
    RHIIndexType Type = RHIIndexType::UInt32;
};

/** 描述命令列表创建参数。*/
struct RHICommandListDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** 执行该列表的队列类别。*/
    RHIQueueType QueueType = RHIQueueType::Graphics;
    /** 提交或嵌套能力。*/
    RHICommandListLevel Level = RHICommandListLevel::Primary;
};

/** 定义可录制到抽象 RHI 命令列表中的所有命令。*/
class IRHICommandList
{
public:
    /** 释放命令列表实现。*/
    virtual ~IRHICommandList() = default;

    /** 返回创建命令列表时选择的队列类别。*/
    [[nodiscard]] virtual RHIQueueType GetQueueType() const noexcept = 0;

    /** 返回当前录制生命周期状态。*/
    [[nodiscard]] virtual RHICommandListState GetState() const noexcept = 0;

    /** 返回命令列表创建描述。*/
    [[nodiscard]] virtual const RHICommandListDesc& GetDesc() const noexcept = 0;

    /** 完成后重置该列表，使其可再次录制。*/
    virtual RHIStatus Reset() = 0;

    /** 开始向初始状态的命令列表录制命令。*/
    virtual RHIStatus Begin() = 0;

    /** 结束命令录制并使列表变为可执行状态。*/
    virtual RHIStatus End() = 0;

    /** 插入用于诊断和捕获工具的调试标签区域。*/
    virtual RHIStatus PushDebugLabel(std::string_view label, const RHIColor& color) = 0;

    /** 关闭最近打开的调试标签区域。*/
    virtual RHIStatus PopDebugLabel() = 0;

    /** 应用资源状态转换和显式内存排序屏障。*/
    virtual RHIStatus ResourceBarriers(std::span<const RHIBarrier> barriers) = 0;

    /** 开始一个动态渲染区域。*/
    virtual RHIStatus BeginRendering(const RHIRenderingDesc& desc) = 0;

    /** 结束当前动态渲染区域。*/
    virtual RHIStatus EndRendering() = 0;

    /** 为后续绘制命令绑定图形管线。*/
    virtual RHIStatus BindGraphicsPipeline(RHIGraphicsPipelineHandle pipeline) = 0;

    /** 为后续调度命令绑定计算管线。*/
    virtual RHIStatus BindComputePipeline(RHIComputePipelineHandle pipeline) = 0;

    /** 为后续追踪命令绑定光线追踪管线。*/
    virtual RHIStatus BindRayTracingPipeline(RHIRayTracingPipelineHandle pipeline) = 0;

    /** 将资源集绑定到当前激活的管线布局。*/
    virtual RHIStatus BindSet(std::uint32_t setIndex, RHIBindSetHandle bindSet, std::span<const std::uint32_t> dynamicOffsets) = 0;

    /** 向当前激活的管线布局写入内联常量。*/
    virtual RHIStatus PushConstants(RHIShaderStage visibility, std::uint32_t offset, std::span<const std::byte> data) = 0;

    /** 。firstBinding 开始绑定连续的顶点缓冲区。*/
    virtual RHIStatus BindVertexBuffers(std::uint32_t firstBinding, std::span<const RHIVertexBufferBinding> bindings) = 0;

    /** 绑定索引绘制命令使用的索引缓冲区。*/
    virtual RHIStatus BindIndexBuffer(const RHIIndexBufferBinding& binding) = 0;

    /** 设置一个或多个动态视口。*/
    virtual RHIStatus SetViewports(std::uint32_t firstViewport, std::span<const RHIViewport> viewports) = 0;

    /** 设置一个或多个动态裁剪矩形。*/
    virtual RHIStatus SetScissors(std::uint32_t firstScissor, std::span<const RHIScissor> scissors) = 0;

    /** 设置动。RGBA 混合常量。*/
    virtual RHIStatus SetBlendConstant(const RHIColor& color) = 0;

    /** 设置动态深度偏移参数。*/
    virtual RHIStatus SetDepthBias(float constantFactor, float clamp, float slopeFactor) = 0;

    /** 设置动态模板参考值。*/
    virtual RHIStatus SetStencilReference(std::uint32_t reference) = 0;

    /** 设置动态模板比较掩码。*/
    virtual RHIStatus SetStencilCompareMask(std::uint32_t compareMask) = 0;

    /** 设置动态模板写入掩码。*/
    virtual RHIStatus SetStencilWriteMask(std::uint32_t writeMask) = 0;

    /** 设置动态最小和最大深度边界。*/
    virtual RHIStatus SetDepthBounds(float minimum, float maximum) = 0;

    /** 设置动态光栅化线宽。*/
    virtual RHIStatus SetLineWidth(float width) = 0;

    /** 录制一次非索引绘制。*/
    virtual RHIStatus Draw(std::uint32_t vertexCount, std::uint32_t instanceCount = 1, std::uint32_t firstVertex = 0, std::uint32_t firstInstance = 0) = 0;

    /** 录制一次索引绘制。*/
    virtual RHIStatus DrawIndexed(std::uint32_t indexCount, std::uint32_t instanceCount = 1, std::uint32_t firstIndex = 0, std::int32_t vertexOffset = 0, std::uint32_t firstInstance = 0) = 0;

    /** 录制。GPU 缓冲区描述的一次或多次非索引绘制。*/
    virtual RHIStatus DrawIndirect(RHIBufferHandle argumentBuffer, std::uint64_t argumentOffset, std::uint32_t drawCount, std::uint32_t stride) = 0;

    /** 录制。GPU 缓冲区描述的一次或多次索引绘制。*/
    virtual RHIStatus DrawIndexedIndirect(RHIBufferHandle argumentBuffer, std::uint64_t argumentOffset, std::uint32_t drawCount, std::uint32_t stride) = 0;

    /** 后端支持网格着色时录制一次网格任务调度。*/
    virtual RHIStatus DrawMeshTasks(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) = 0;

    /** 录制。GPU 缓冲区描述的网格着色任务调度。*/
    virtual RHIStatus DrawMeshTasksIndirect(RHIBufferHandle argumentBuffer, std::uint64_t argumentOffset, std::uint32_t drawCount, std::uint32_t stride) = 0;

    /** 录制一次计算调度。*/
    virtual RHIStatus Dispatch(std::uint32_t groupCountX, std::uint32_t groupCountY, std::uint32_t groupCountZ) = 0;

    /** 录制维度存储。GPU 缓冲区中的计算调度。*/
    virtual RHIStatus DispatchIndirect(RHIBufferHandle argumentBuffer, std::uint64_t argumentOffset) = 0;

    /** 在两个缓冲区间复制字节。*/
    virtual RHIStatus CopyBuffer(RHIBufferHandle source, RHIBufferHandle destination, std::span<const RHIBufferCopyRegion> regions) = 0;

    /** 将纹素从缓冲区复制到纹理。*/
    virtual RHIStatus CopyBufferToTexture(RHIBufferHandle source, RHITextureHandle destination, std::span<const RHITextureBufferCopyRegion> regions) = 0;

    /** 将纹素从纹理复制到缓冲区。*/
    virtual RHIStatus CopyTextureToBuffer(RHITextureHandle source, RHIBufferHandle destination, std::span<const RHITextureBufferCopyRegion> regions) = 0;

    /** 在两个纹理间复制纹素。*/
    virtual RHIStatus CopyTexture(RHITextureHandle source, RHITextureHandle destination, std::span<const RHITextureCopyRegion> regions) = 0;

    /** 将多重采样纹理解析到单采样纹理。*/
    virtual RHIStatus ResolveTexture(RHITextureHandle source, RHITextureHandle destination, std::span<const RHITextureCopyRegion> regions, RHIResolveMode mode) = 0;

    /** 清除颜色纹理子资源范围。*/
    virtual RHIStatus ClearColorTexture(RHITextureHandle texture, const RHIClearColorValue& value, std::span<const RHISubresourceRange> ranges) = 0;

    /** 清除深度/模板纹理子资源范围。*/
    virtual RHIStatus ClearDepthStencilTexture(RHITextureHandle texture, const RHIClearDepthStencilValue& value, std::span<const RHISubresourceRange> ranges) = 0;

    /** 开始一次遮挡或管线统计查询。*/
    virtual RHIStatus BeginQuery(RHIQueryPoolHandle pool, std::uint32_t queryIndex) = 0;

    /** 结束一次遮挡或管线统计查询。*/
    virtual RHIStatus EndQuery(RHIQueryPoolHandle pool, std::uint32_t queryIndex) = 0;

    /** 将时间戳写入查询槽位。*/
    virtual RHIStatus WriteTimestamp(RHIQueryPoolHandle pool, std::uint32_t queryIndex, RHIPipelineStage stage) = 0;

    /** 将查询结果复制到目标缓冲区。*/
    virtual RHIStatus ResolveQueryData(RHIQueryPoolHandle pool, std::uint32_t firstQuery, std::uint32_t queryCount, RHIBufferHandle destination, std::uint64_t destinationOffset) = 0;

    /** 构建或更新加速结构。*/
    virtual RHIStatus BuildAccelerationStructure(const RHIAccelerationStructureBuildDesc& desc) = 0;

    /** 使用当前绑定的光线追踪管线调度光线。*/
    virtual RHIStatus TraceRays(const RHIShaderTableDesc& shaderTable, std::uint32_t width, std::uint32_t height, std::uint32_t depth) = 0;

    /** 从主命令列表执行次级命令列表。*/
    virtual RHIStatus ExecuteSecondary(std::span<IRHICommandList* const> secondaryLists) = 0;
};

/** 持有后端命令列表实现。*/
using RHICommandListPtr = std::shared_ptr<IRHICommandList>;

} // namespace RHI
