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
    RHIRenderGraphResourceType type = RHIRenderGraphResourceType::Texture;
    u32 index = RHI_INVALID_INDEX;

    [[nodiscard]] constexpr bool IsBuffer() const noexcept {
        return type == RHIRenderGraphResourceType::Buffer;
    }

    [[nodiscard]] constexpr bool IsTexture() const noexcept {
        return !IsBuffer();
    }
};

/// RenderGraph 编译器根据相邻用途生成的资源状态和队列转换。
struct RHIRenderGraphTransition {
    RHIRenderGraphResourceId resource{};
    RHIResourceState before = RHIResourceState::Undefined;
    RHIResourceState after = RHIResourceState::Common;
    RHIPipelineStage sourceStages = RHIPipelineStage::TopOfPipe;
    RHIPipelineStage destinationStages = RHIPipelineStage::AllCommands;
    RHIAccessFlags sourceAccess = RHIAccessFlags::None;
    RHIAccessFlags destinationAccess = RHIAccessFlags::None;
    RHIQueueType sourceQueue = RHIQueueType::Graphics;
    RHIQueueType destinationQueue = RHIQueueType::Graphics;
    bool discardContents = false;
    bool aliasingBarrier = false;
};

/// Attachment 已解析成 texture 数组索引；attachmentIndex 用于读取当前帧的 load/store/clear。
struct RHICompiledRenderGraphAttachment {
    u32 textureIndex = RHI_INVALID_INDEX;
    u32 attachmentIndex = RHI_INVALID_INDEX;
};

/**
 * 单个可执行 pass。sourcePassIndex/workloadIndex 直接索引原始帧包；dependencies 保存
 * 编译计划内的 pass 索引，可用于调试、并行录制和跨队列调度。
 */
struct RHICompiledRenderGraphPass {
    u32 sourcePassIndex = RHI_INVALID_INDEX;
    u32 workloadIndex = RHI_INVALID_INDEX;
    RHIQueueType queue = RHIQueueType::Graphics;
    std::vector<u32> dependencies;
    std::vector<RHIRenderGraphTransition> transitions;
    std::vector<RHICompiledRenderGraphAttachment> colorAttachments;
    std::optional<RHICompiledRenderGraphAttachment> depthStencilAttachment;
};

/// 编译后资源的首末使用位置；空资源保持 RHI_INVALID_INDEX。
struct RHIRenderGraphResourceLifetime {
    u32 firstPass = RHI_INVALID_INDEX;
    u32 lastPass = RHI_INVALID_INDEX;
};

/// 后端真正消费的稳定执行计划。
struct RHIRenderGraphExecutionPlan {
    u64 structureHash = 0;
    std::vector<RHICompiledRenderGraphPass> passes;
    std::vector<RHIRenderGraphResourceLifetime> bufferLifetimes;
    std::vector<RHIRenderGraphResourceLifetime> textureLifetimes;
    /// 每个逻辑资源对应的帧内物理分配槽；Imported 或未使用资源保持 RHI_INVALID_INDEX。
    std::vector<u32> bufferAllocationSlots;
    std::vector<u32> textureAllocationSlots;
    u32 bufferAllocationCount = 0;
    u32 textureAllocationCount = 0;
    std::vector<bool> culledPasses;
};

struct RHIRenderGraphCompileResult {
    RHIRenderGraphExecutionPlan plan;
    std::vector<std::string> errors;

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
