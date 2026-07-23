#pragma once

#include "../RHIDefinitions.hpp"

#include <span>
#include <string>
#include <vector>

namespace rhi {

/**
 * 编译计划中的资源引用。Buffer 的 index 索引 RHIRenderGraphDesc::buffers，Texture 和
 * SwapchainImage 的 index 都索引 textures。编译后后端不再做字符串查找。
 */
struct RHIRenderGraphResourceId {
    RHIRenderGraphResourceType type = RHIRenderGraphResourceType::Texture;  ///< 区分 buffer 数组和 texture 数组。
    u32 index = RHI_INVALID_INDEX;                                          ///< 对应 RHIRenderGraphDesc 中同类型资源的下标。

    [[nodiscard]] constexpr bool IsBuffer() const noexcept {
        return type == RHIRenderGraphResourceType::Buffer;
    }

    [[nodiscard]] constexpr bool IsTexture() const noexcept {
        return !IsBuffer();
    }
};

/**
 * RenderGraph 编译器根据相邻用途生成的资源同步需求。
 *
 * before/after 表达逻辑状态变化；stage/access 表达生产者与消费者在 GPU 管线中的
 * 可见性范围。后端必须以自己的真实资源追踪状态为准，因为 imported 资源可能在进入
 * RenderGraph 前已经被上传或被窗口系统使用过。
 */
struct RHIRenderGraphTransition {
    RHIRenderGraphResourceId resource{};                                 ///< 被转换的逻辑资源。
    RHIResourceState before = RHIResourceState::Undefined;               ///< 编译器推导的前一逻辑状态。
    RHIResourceState after = RHIResourceState::Common;                   ///< 当前 pass 要求的目标状态。
    RHIPipelineStage sourceStages = RHIPipelineStage::TopOfPipe;         ///< 必须完成的生产阶段。
    RHIPipelineStage destinationStages = RHIPipelineStage::AllCommands;  ///< 等待完成后可开始的消费阶段。
    RHIAccessFlags sourceAccess = RHIAccessFlags::None;                  ///< barrier 前必须完成并变得可见的访问。
    RHIAccessFlags destinationAccess = RHIAccessFlags::None;             ///< barrier 后允许执行的访问。
    RHIQueueType sourceQueue = RHIQueueType::Graphics;                   ///< 前一次逻辑用途所属队列。
    RHIQueueType destinationQueue = RHIQueueType::Graphics;              ///< 当前逻辑用途所属队列。
    bool discardContents = false;                                        ///< true 表示当前 pass 不需要旧内容，例如 attachment Clear。
    bool aliasingBarrier = false;                                        ///< true 表示物理槽刚从另一逻辑资源切换过来，仍需等待旧访问结束。
};

/// Attachment 已解析成 texture 数组索引；attachmentIndex 用于读取当前帧的 load/store/clear。
struct RHICompiledRenderGraphAttachment {
    u32 textureIndex = RHI_INVALID_INDEX;     ///< 已解析的 graph texture 下标。
    u32 attachmentIndex = RHI_INVALID_INDEX;  ///< 当前帧 source pass 中的 attachment 下标。
};

/**
 * 单个可执行 pass。sourcePassIndex/workloadIndex 直接索引原始帧包；dependencies 保存
 * 编译计划内的 pass 索引，可用于调试、并行录制和跨队列调度。
 */
struct RHICompiledRenderGraphPass {
    u32 sourcePassIndex = RHI_INVALID_INDEX;                                 ///< 索引 packet.graph.passes，读取本帧动态 attachment 数据。
    u32 workloadIndex = RHI_INVALID_INDEX;                                   ///< 索引 packet.workloads；无命令的 clear/present pass 可为空。
    RHIQueueType queue = RHIQueueType::Graphics;                             ///< pass 声明的目标队列类型。
    std::vector<u32> dependencies;                                           ///< 必须先完成的 compiled pass 下标，均小于当前 pass 下标。
    std::vector<RHIRenderGraphTransition> transitions;                       ///< 执行 pass 前需要完成的状态和可见性转换。
    std::vector<RHICompiledRenderGraphAttachment> colorAttachments;          ///< 已消除字符串查找的颜色附件。
    std::optional<RHICompiledRenderGraphAttachment> depthStencilAttachment;  ///< 可选深度模板附件。
};

/// 编译后资源的首末使用位置；空资源保持 RHI_INVALID_INDEX。
struct RHIRenderGraphResourceLifetime {
    u32 firstPass = RHI_INVALID_INDEX;  ///< 第一次访问该逻辑资源的 compiled pass。
    u32 lastPass = RHI_INVALID_INDEX;   ///< 最后一次访问该逻辑资源的 compiled pass。
};

/**
 * 后端真正消费的稳定执行计划。
 *
 * passes/lifetimes/allocationSlots 都只保存整数索引，不保存 clear color、viewport 或 draw
 * 参数，因此同一拓扑可跨帧缓存。逻辑资源数量可以大于 allocationCount：生命周期不重叠
 * 且描述兼容的 transient 资源会指向同一个物理槽。
 */
struct RHIRenderGraphExecutionPlan {
    u64 structureHash = 0;                                         ///< 只覆盖影响执行计划的静态结构，用于 RHIDevice 帧间缓存。
    std::vector<RHICompiledRenderGraphPass> passes;                ///< 已裁剪并按依赖排序的执行序列。
    std::vector<RHIRenderGraphResourceLifetime> bufferLifetimes;   ///< 每个逻辑 buffer 的生存区间。
    std::vector<RHIRenderGraphResourceLifetime> textureLifetimes;  ///< 每个逻辑 texture 的生存区间。
    /// 每个逻辑资源对应的帧内物理分配槽；Imported 或未使用资源保持 RHI_INVALID_INDEX。
    std::vector<u32> bufferAllocationSlots;   ///< 逻辑 buffer 下标到物理 buffer 槽的映射。
    std::vector<u32> textureAllocationSlots;  ///< 逻辑 texture 下标到物理 texture 槽的映射。
    u32 bufferAllocationCount = 0;            ///< 本帧最多需要同时创建的内部物理 buffer 数。
    u32 textureAllocationCount = 0;           ///< 本帧最多需要同时创建的内部物理 texture 数。
    std::vector<bool> culledPasses;           ///< 按 source pass 下标记录哪些声明被裁剪。
};

struct RHIRenderGraphCompileResult {
    RHIRenderGraphExecutionPlan plan;  ///< errors 为空时才可交给后端执行。
    std::vector<std::string> errors;   ///< 一次收集多个声明错误，减少修改-重跑次数。

    [[nodiscard]] bool Succeeded() const noexcept { return errors.empty(); }
    [[nodiscard]] std::string ErrorMessage() const;
};

/**
 * 把声明式 RenderGraph 编译成确定的执行计划：
 * 1. 校验资源、pass、workload 和 attachment 引用；
 * 2. 从 RAW/WAR/WAW hazard 和 dependsOnPasses 建立依赖；
 * 3. 检测循环并进行稳定拓扑排序；
 * 4. 从外部输出/副作用反向裁剪无用 pass；
 * 5. 计算资源生命周期、瞬态物理槽复用和逐 pass barrier。
 */
[[nodiscard]] RHIRenderGraphCompileResult CompileRHIRenderGraph(
    const RHIRenderGraphDesc& graph,
    std::span<const RHIRenderPassWorkload> workloads = {});

/// 仅哈希影响执行计划的静态拓扑；draw 参数、clear value、viewport 等每帧数据不参与。
[[nodiscard]] u64 HashRHIRenderGraphStructure(
    const RHIRenderGraphDesc& graph,
    std::span<const RHIRenderPassWorkload> workloads = {}) noexcept;

/**
 * 校验帧提交描述是否与编译后的 pass 顺序一致。
 * 当前三个后端都把一帧 RenderGraph 录制为一个主命令流，因此只支持零个提交描述
 * （自动提交）或一个覆盖全部存活 pass 的提交描述。这样可以避免悄悄忽略 passNames，
 * 也为以后真正拆分多队列 execution batch 保留清晰边界。
 */
[[nodiscard]] bool ValidateRHIRenderGraphSubmissions(
    const RHIRenderGraphDesc& graph,
    const RHIRenderGraphExecutionPlan& plan,
    std::span<const RHIQueueSubmitDesc> submissions,
    std::string* errorMessage = nullptr);

} // namespace rhi
