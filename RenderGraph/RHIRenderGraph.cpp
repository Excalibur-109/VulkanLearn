#include "RHIRenderGraph.hpp"

#include <algorithm>
#include <deque>
#include <limits>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace rhi {
namespace {

constexpr u64 FNV_OFFSET = 14695981039346656037ULL;
constexpr u64 FNV_PRIME = 1099511628211ULL;

// FNV-1a 足够快，适合每帧判断 graph 拓扑是否变化；它不是资源内容哈希，也不用于持久化。
void HashBytes(u64& hash, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= FNV_PRIME;
    }
}

template <typename Type>
void HashValue(u64& hash, const Type& value) noexcept {
    HashBytes(hash, &value, sizeof(Type));
}

void HashString(u64& hash, const std::string& value) noexcept {
    HashBytes(hash, value.data(), value.size());
    const u8 terminator = 0xFF;
    HashValue(hash, terminator);
}

struct ResourceKey {
    bool buffer = false;
    u32 index = RHI_INVALID_INDEX;

    friend bool operator==(ResourceKey lhs, ResourceKey rhs) noexcept = default;
};

struct ResourceKeyHash {
    [[nodiscard]] std::size_t operator()(ResourceKey key) const noexcept {
        return (static_cast<std::size_t>(key.index) << 1U) |
               static_cast<std::size_t>(key.buffer);
    }
};

struct PassUsage {
    // 同一 pass 对同一资源的 reads/writes/attachment 会在这里合并。后续 hazard 和
    // barrier 分析只看一条规范化用途，避免重复引用产生重复依赖边。
    RHIRenderGraphResourceId resource{};
    RHIResourceState state = RHIResourceState::Undefined;
    RHIPipelineStage stages = RHIPipelineStage::None;
    RHIAccessFlags access = RHIAccessFlags::None;
    bool reads = false;
    bool writes = false;
    bool discardContents = false;
};

struct PassBuildData {
    // 编译期临时数据，不进入最终 ExecutionPlan。dependencies 先装显式边，再追加
    // RAW/WAR/WAW 边；workloadIndex 消除后端逐 pass 的字符串搜索。
    std::vector<PassUsage> usages;
    std::unordered_map<ResourceKey, u32, ResourceKeyHash> usageIndices;
    std::unordered_set<u32> dependencies;
    u32 workloadIndex = RHI_INVALID_INDEX;
    bool root = false;
};

struct HazardState {
    // 对一个资源扫描 pass 序列时的访问历史。一个资源只能有一个最近 writer，
    // 但可以同时存在多个尚未被后续 write 截断的 readers。
    u32 lastWriter = RHI_INVALID_INDEX;
    std::vector<u32> readers;
    bool initialized = false;
};

struct TrackedState {
    // 用于生成 barrier 的逻辑状态机。它与后端的真实 native 状态追踪互相补充：
    // 编译器负责“应该变成什么”，后端负责“当前实际上是什么”。
    RHIResourceState state = RHIResourceState::Undefined;
    RHIPipelineStage stages = RHIPipelineStage::TopOfPipe;
    RHIAccessFlags access = RHIAccessFlags::None;
    RHIQueueType queue = RHIQueueType::Graphics;
    bool initialized = false;
    bool lastAccessWrote = false;
};

[[nodiscard]] bool IsImported(const RHIRenderGraphBufferDesc& resource) noexcept {
    return resource.imported || RHIHasAny(resource.flags, RHIRenderGraphResourceFlags::Imported);
}

[[nodiscard]] bool IsImported(const RHIRenderGraphTextureDesc& resource) noexcept {
    return resource.imported || RHIHasAny(resource.flags, RHIRenderGraphResourceFlags::Imported);
}

[[nodiscard]] bool IsOutput(RHIRenderGraphResourceFlags flags) noexcept {
    return RHIHasAny(
        flags,
        RHIRenderGraphResourceFlags::Exported |
            RHIRenderGraphResourceFlags::NeverCull);
}

[[nodiscard]] bool CanAlias(const RHIRenderGraphBufferDesc& resource) noexcept {
    return !IsImported(resource) &&
           RHIHasAny(resource.flags, RHIRenderGraphResourceFlags::AllowAliasing) &&
           (RHIHasAny(resource.flags, RHIRenderGraphResourceFlags::Transient) ||
            resource.desc.lifetime == RHIResourceLifetime::Transient) &&
           !IsOutput(resource.flags);
}

[[nodiscard]] bool CanAlias(const RHIRenderGraphTextureDesc& resource) noexcept {
    return !IsImported(resource) &&
           RHIHasAny(resource.flags, RHIRenderGraphResourceFlags::AllowAliasing) &&
           (RHIHasAny(resource.flags, RHIRenderGraphResourceFlags::Transient) ||
            resource.desc.lifetime == RHIResourceLifetime::Transient) &&
           !IsOutput(resource.flags);
}

[[nodiscard]] bool AreCompatible(
    const RHIBufferDesc& lhs,
    const RHIBufferDesc& rhs) noexcept {
    return lhs.size == rhs.size &&
           lhs.usage == rhs.usage &&
           lhs.flags == rhs.flags &&
           lhs.memoryUsage == rhs.memoryUsage &&
           lhs.lifetime == rhs.lifetime &&
           lhs.persistentlyMapped == rhs.persistentlyMapped;
}

[[nodiscard]] bool AreCompatible(
    const RHITextureDesc& lhs,
    const RHITextureDesc& rhs) noexcept {
    return lhs.dimension == rhs.dimension &&
           lhs.extent.width == rhs.extent.width &&
           lhs.extent.height == rhs.extent.height &&
           lhs.extent.depth == rhs.extent.depth &&
           lhs.arrayLayers == rhs.arrayLayers &&
           lhs.mipLevels == rhs.mipLevels &&
           lhs.format == rhs.format &&
           lhs.samples == rhs.samples &&
           lhs.usage == rhs.usage &&
           lhs.flags == rhs.flags &&
           lhs.lifetime == rhs.lifetime &&
           lhs.initialState == rhs.initialState;
}

[[nodiscard]] RHIRenderGraphResourceType NormalizeType(
    RHIRenderGraphResourceType type) noexcept {
    return type == RHIRenderGraphResourceType::Buffer
               ? RHIRenderGraphResourceType::Buffer
               : RHIRenderGraphResourceType::Texture;
}

[[nodiscard]] ResourceKey MakeKey(RHIRenderGraphResourceId resource) noexcept {
    return ResourceKey{resource.IsBuffer(), resource.index};
}

void AddUnique(std::vector<u32>& values, u32 value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(value);
    }
}

/// 对当前依赖集合执行稳定拓扑排序。可执行 pass 总是优先选择原始下标较小者，
/// 因此不含依赖关系的 pass 会保持声明顺序，便于调试和 GPU capture 对照源码。
[[nodiscard]] std::vector<u32> BuildStableTopologicalOrder(
    const std::vector<PassBuildData>& passData) {
    std::vector<std::vector<u32>> dependents(passData.size());
    std::vector<u32> indegrees(passData.size(), 0);
    std::deque<u32> ready;

    for (u32 passIndex = 0; passIndex < passData.size(); ++passIndex) {
        indegrees[passIndex] =
            static_cast<u32>(passData[passIndex].dependencies.size());
        for (const u32 dependency : passData[passIndex].dependencies) {
            AddUnique(dependents[dependency], passIndex);
        }
        if (indegrees[passIndex] == 0) {
            ready.push_back(passIndex);
        }
    }

    std::vector<u32> order;
    order.reserve(passData.size());
    while (!ready.empty()) {
        const u32 passIndex = ready.front();
        ready.pop_front();
        order.push_back(passIndex);

        for (const u32 dependent : dependents[passIndex]) {
            if (--indegrees[dependent] == 0) {
                const auto position =
                    std::upper_bound(ready.begin(), ready.end(), dependent);
                ready.insert(position, dependent);
            }
        }
    }
    return order;
}

} // namespace

std::string RHIRenderGraphCompileResult::ErrorMessage() const {
    std::ostringstream message;
    for (std::size_t index = 0; index < errors.size(); ++index) {
        if (index != 0) {
            message << '\n';
        }
        message << errors[index];
    }
    return message.str();
}

bool ValidateRHIRenderGraphSubmissions(
    const RHIRenderGraphDesc& graph,
    const RHIRenderGraphExecutionPlan& plan,
    std::span<const RHIQueueSubmitDesc> submissions,
    std::string* errorMessage) {
    const auto fail = [&](std::string message) {
        if (errorMessage != nullptr) {
            *errorMessage = std::move(message);
        }
        return false;
    };

    // 空 submissions 表示把同步和提交策略交给后端；这是普通 RenderGraph 帧的推荐路径。
    if (submissions.empty()) {
        return true;
    }
    if (submissions.size() != 1) {
        return fail(
            "RenderGraph currently records one command stream per frame; multiple queue submissions are not supported yet");
    }

    const std::vector<std::string>& passNames = submissions[0].passNames;
    if (passNames.empty()) {
        return true;
    }
    if (passNames.size() != plan.passes.size()) {
        return fail(
            "RHIQueueSubmitDesc::passNames must contain every live RenderGraph pass exactly once");
    }

    for (u32 compiledIndex = 0; compiledIndex < plan.passes.size(); ++compiledIndex) {
        const u32 sourcePassIndex = plan.passes[compiledIndex].sourcePassIndex;
        if (sourcePassIndex >= graph.passes.size()) {
            return fail("Compiled RenderGraph pass index is out of range");
        }
        const std::string& expectedName = graph.passes[sourcePassIndex].name;
        if (passNames[compiledIndex] != expectedName) {
            return fail(
                "RHIQueueSubmitDesc::passNames must follow compiled dependency order; expected '" +
                expectedName + "' at position " + std::to_string(compiledIndex));
        }
    }
    return true;
}

u64 HashRHIRenderGraphStructure(
    const RHIRenderGraphDesc& graph,
    std::span<const RHIRenderPassWorkload> workloads) noexcept {
    // 只加入会影响校验、依赖、裁剪、barrier 或物理槽分配的数据。外部句柄数值、
    // clear value、viewport 和具体 draw 参数每帧可变，不应让缓存失效。
    u64 hash = FNV_OFFSET;
    HashValue(hash, graph.buffers.size());
    for (const RHIRenderGraphBufferDesc& resource : graph.buffers) {
        HashString(hash, resource.name);
        HashValue(hash, resource.flags);
        HashValue(hash, resource.imported);
        const bool hasExternalHandle = static_cast<bool>(resource.externalHandle);
        HashValue(hash, hasExternalHandle);
        HashValue(hash, resource.desc.size);
        HashValue(hash, resource.desc.usage);
        HashValue(hash, resource.desc.flags);
        HashValue(hash, resource.desc.memoryUsage);
        HashValue(hash, resource.desc.lifetime);
        HashValue(hash, resource.desc.persistentlyMapped);
    }
    HashValue(hash, graph.textures.size());
    for (const RHIRenderGraphTextureDesc& resource : graph.textures) {
        HashString(hash, resource.name);
        HashValue(hash, resource.flags);
        HashValue(hash, resource.imported);
        const bool hasExternalHandle = static_cast<bool>(resource.externalHandle);
        HashValue(hash, hasExternalHandle);
        HashValue(hash, resource.desc.dimension);
        HashValue(hash, resource.desc.extent.width);
        HashValue(hash, resource.desc.extent.height);
        HashValue(hash, resource.desc.extent.depth);
        HashValue(hash, resource.desc.arrayLayers);
        HashValue(hash, resource.desc.mipLevels);
        HashValue(hash, resource.desc.format);
        HashValue(hash, resource.desc.samples);
        HashValue(hash, resource.desc.usage);
        HashValue(hash, resource.desc.flags);
        HashValue(hash, resource.desc.lifetime);
        HashValue(hash, resource.desc.initialState);
    }
    HashValue(hash, graph.passes.size());
    for (const RHIRenderGraphPassDesc& pass : graph.passes) {
        HashString(hash, pass.name);
        HashValue(hash, pass.type);
        HashValue(hash, pass.queue);
        HashValue(hash, pass.allowAsyncCompute);
        HashValue(hash, pass.cullable);
        HashValue(hash, pass.hasSideEffect);
        HashValue(hash, pass.dependsOnPasses.size());
        for (const std::string& dependency : pass.dependsOnPasses) {
            HashString(hash, dependency);
        }
        const auto hashReferences = [&](const auto& references) {
            HashValue(hash, references.size());
            for (const RHIRenderGraphResourceRef& reference : references) {
                HashString(hash, reference.name);
                HashValue(hash, reference.type);
                HashValue(hash, reference.state);
                HashValue(hash, reference.stages);
                HashValue(hash, reference.access);
            }
        };
        hashReferences(pass.reads);
        hashReferences(pass.writes);
        HashValue(hash, pass.colorAttachments.size());
        for (const RHIRenderGraphAttachmentDesc& attachment : pass.colorAttachments) {
            HashString(hash, attachment.resourceName);
            HashValue(hash, attachment.aspect);
            HashValue(hash, attachment.mipLevel);
            HashValue(hash, attachment.arrayLayer);
            HashValue(hash, attachment.loadOp);
            HashValue(hash, attachment.storeOp);
        }
        const bool hasDepth = pass.depthStencilAttachment.has_value();
        HashValue(hash, hasDepth);
        if (hasDepth) {
            const RHIRenderGraphAttachmentDesc& attachment =
                *pass.depthStencilAttachment;
            HashString(hash, attachment.resourceName);
            HashValue(hash, attachment.aspect);
            HashValue(hash, attachment.mipLevel);
            HashValue(hash, attachment.arrayLayer);
            HashValue(hash, attachment.loadOp);
            HashValue(hash, attachment.storeOp);
        }
    }
    HashValue(hash, workloads.size());
    for (const RHIRenderPassWorkload& workload : workloads) {
        HashString(hash, workload.passName);

        // 编译器只需要知道命令的类别来校验 pass 类型。命令数量和参数可以每帧变化，
        // 但“是否包含 draw/dispatch”会改变编译结果，因此必须进入结构哈希。
        const bool hasDraws = !workload.draws.empty() ||
                              !workload.indexedDraws.empty() ||
                              !workload.indirectDraws.empty() ||
                              !workload.indexedIndirectDraws.empty();
        const bool hasDispatches = !workload.dispatches.empty() ||
                                   !workload.indirectDispatches.empty();
        const bool hasTransfers = !workload.bufferCopies.empty() ||
                                  !workload.textureCopies.empty() ||
                                  !workload.bufferToTextureCopies.empty() ||
                                  !workload.textureToBufferCopies.empty() ||
                                  !workload.textureBlits.empty() ||
                                  !workload.mipmapGenerations.empty();
        const bool hasQueries = !workload.queryResets.empty() ||
                                !workload.timestampWrites.empty() ||
                                !workload.queryResolves.empty();
        HashValue(hash, hasDraws);
        HashValue(hash, hasDispatches);
        HashValue(hash, hasTransfers);
        HashValue(hash, hasQueries);
    }
    return hash;
}

RHIRenderGraphCompileResult CompileRHIRenderGraph(
    const RHIRenderGraphDesc& graph,
    std::span<const RHIRenderPassWorkload> workloads) {
    // 阶段 0：预分配最终计划中与声明数量一一对应的数组。
    RHIRenderGraphCompileResult result{};
    result.plan.structureHash = HashRHIRenderGraphStructure(graph, workloads);
    result.plan.bufferLifetimes.resize(graph.buffers.size());
    result.plan.textureLifetimes.resize(graph.textures.size());
    result.plan.bufferAllocationSlots.resize(
        graph.buffers.size(),
        RHI_INVALID_INDEX);
    result.plan.textureAllocationSlots.resize(
        graph.textures.size(),
        RHI_INVALID_INDEX);
    result.plan.culledPasses.resize(graph.passes.size(), false);

    // 阶段 1：建立唯一名称表。名称只在编译期使用，后端只消费整数下标。
    std::unordered_map<std::string, u32> bufferIndices;
    std::unordered_map<std::string, u32> textureIndices;
    std::unordered_map<std::string, u32> passIndices;
    std::unordered_map<std::string, u32> workloadIndices;

    const auto addNamedIndex = [&](auto& indices, const std::string& name, u32 index, const char* kind) {
        if (name.empty()) {
            result.errors.emplace_back(std::string(kind) + " name cannot be empty");
            return;
        }
        if (!indices.emplace(name, index).second) {
            result.errors.emplace_back(std::string("Duplicate ") + kind + " name: " + name);
        }
    };

    for (u32 index = 0; index < graph.buffers.size(); ++index) {
        const RHIRenderGraphBufferDesc& resource = graph.buffers[index];
        addNamedIndex(bufferIndices, resource.name, index, "RenderGraph buffer");
        if (IsImported(resource) && !resource.externalHandle) {
            result.errors.emplace_back(
                "Imported RenderGraph buffer has no external handle: " + resource.name);
        }
    }
    for (u32 index = 0; index < graph.textures.size(); ++index) {
        const RHIRenderGraphTextureDesc& resource = graph.textures[index];
        addNamedIndex(textureIndices, resource.name, index, "RenderGraph texture");
        if (IsImported(resource) && !resource.externalHandle) {
            result.errors.emplace_back(
                "Imported RenderGraph texture has no external handle: " + resource.name);
        }
    }
    for (u32 index = 0; index < graph.passes.size(); ++index) {
        addNamedIndex(passIndices, graph.passes[index].name, index, "RenderGraph pass");
    }
    for (u32 index = 0; index < workloads.size(); ++index) {
        const std::string& name = workloads[index].passName;
        if (name.empty()) {
            result.errors.emplace_back("RenderGraph workload passName cannot be empty");
        } else if (!workloadIndices.emplace(name, index).second) {
            result.errors.emplace_back("Duplicate RenderGraph workload for pass: " + name);
        } else if (!passIndices.contains(name)) {
            result.errors.emplace_back("RenderGraph workload references unknown pass: " + name);
        }
    }

    if (!result.errors.empty()) {
        return result;
    }

    // 阶段 2：校验 pass/workload 类型，并把字符串资源引用规范化为整数 ResourceId。
    // reads、writes 和 attachment 可能重复描述同一资源，因此必须先合并；否则同一 pass
    // 会产生重复 hazard 边，甚至为同一个资源生成互相矛盾的目标状态。
    std::vector<PassBuildData> passData(graph.passes.size());

    const auto resolveResource = [&](const RHIRenderGraphResourceRef& reference)
        -> std::optional<RHIRenderGraphResourceId> {
        if (reference.type == RHIRenderGraphResourceType::Buffer) {
            const auto iterator = bufferIndices.find(reference.name);
            if (iterator == bufferIndices.end()) {
                return std::nullopt;
            }
            return RHIRenderGraphResourceId{RHIRenderGraphResourceType::Buffer, iterator->second};
        }
        const auto iterator = textureIndices.find(reference.name);
        if (iterator == textureIndices.end()) {
            return std::nullopt;
        }
        return RHIRenderGraphResourceId{NormalizeType(reference.type), iterator->second};
    };

    const auto mergeUsage = [&](u32 passIndex, PassUsage incoming, const char* source) {
        PassBuildData& build = passData[passIndex];
        const ResourceKey key = MakeKey(incoming.resource);
        const auto [iterator, inserted] =
            build.usageIndices.emplace(key, static_cast<u32>(build.usages.size()));
        if (inserted) {
            build.usages.push_back(incoming);
            return;
        }

        PassUsage& existing = build.usages[iterator->second];
        if (existing.state != incoming.state) {
            result.errors.emplace_back(
                "RenderGraph pass '" + graph.passes[passIndex].name +
                "' uses one resource with incompatible states while merging " + source);
            return;
        }
        existing.reads = existing.reads || incoming.reads;
        existing.writes = existing.writes || incoming.writes;
        existing.discardContents = existing.discardContents || incoming.discardContents;
        existing.stages |= incoming.stages;
        existing.access |= incoming.access;
    };

    for (u32 passIndex = 0; passIndex < graph.passes.size(); ++passIndex) {
        const RHIRenderGraphPassDesc& pass = graph.passes[passIndex];
        if (pass.type != RHIRenderGraphPassType::Raster &&
            (!pass.colorAttachments.empty() || pass.depthStencilAttachment.has_value())) {
            result.errors.emplace_back(
                "Only Raster RenderGraph passes may declare attachments: " + pass.name);
        }
        if (const auto workload = workloadIndices.find(pass.name);
            workload != workloadIndices.end()) {
            passData[passIndex].workloadIndex = workload->second;
            const RHIRenderPassWorkload& commands = workloads[workload->second];
            const bool hasDraws = !commands.draws.empty() ||
                                  !commands.indexedDraws.empty() ||
                                  !commands.indirectDraws.empty() ||
                                  !commands.indexedIndirectDraws.empty();
            const bool hasDispatches = !commands.dispatches.empty() ||
                                       !commands.indirectDispatches.empty();
            const bool hasTransfers = !commands.bufferCopies.empty() ||
                                      !commands.textureCopies.empty() ||
                                      !commands.bufferToTextureCopies.empty() ||
                                      !commands.textureToBufferCopies.empty() ||
                                      !commands.textureBlits.empty() ||
                                      !commands.mipmapGenerations.empty();
            const bool hasQueries = !commands.queryResets.empty() ||
                                    !commands.timestampWrites.empty() ||
                                    !commands.queryResolves.empty();
            const bool hasAttachments = !pass.colorAttachments.empty() ||
                                        pass.depthStencilAttachment.has_value();
            if (pass.type == RHIRenderGraphPassType::Raster &&
                (hasDispatches || hasTransfers)) {
                result.errors.emplace_back(
                    "Raster RenderGraph pass contains compute/transfer commands: " +
                    pass.name);
            }
            if (pass.type == RHIRenderGraphPassType::Compute &&
                (hasDraws || hasTransfers || hasAttachments)) {
                result.errors.emplace_back(
                    "Compute RenderGraph pass contains raster/transfer commands or attachments: " +
                    pass.name);
            }
            if (pass.type == RHIRenderGraphPassType::Copy &&
                (hasDraws || hasDispatches || hasAttachments)) {
                result.errors.emplace_back(
                    "Copy RenderGraph pass contains draw, dispatch, or attachment work: " +
                    pass.name);
            }
            if (pass.type == RHIRenderGraphPassType::Present &&
                (hasDraws || hasDispatches || hasTransfers || hasQueries ||
                 hasAttachments)) {
                result.errors.emplace_back(
                    "Present RenderGraph pass contains executable commands or attachments: " +
                    pass.name);
            }
            if (pass.type == RHIRenderGraphPassType::Raster && hasDraws &&
                !hasAttachments) {
                result.errors.emplace_back(
                    "Raster RenderGraph pass with draw commands has no attachments: " +
                    pass.name);
            }
        }

        for (const std::string& dependencyName : pass.dependsOnPasses) {
            const auto dependency = passIndices.find(dependencyName);
            if (dependency == passIndices.end()) {
                result.errors.emplace_back(
                    "RenderGraph pass '" + pass.name +
                    "' depends on unknown pass: " + dependencyName);
            } else if (dependency->second == passIndex) {
                result.errors.emplace_back(
                    "RenderGraph pass cannot depend on itself: " + pass.name);
            } else {
                passData[passIndex].dependencies.insert(dependency->second);
            }
        }

        const auto addReferences = [&](const auto& references, bool writes) {
            for (const RHIRenderGraphResourceRef& reference : references) {
                const std::optional<RHIRenderGraphResourceId> resource =
                    resolveResource(reference);
                if (!resource.has_value()) {
                    result.errors.emplace_back(
                        "RenderGraph pass '" + pass.name + "' references unknown resource '" +
                        reference.name + "'");
                    continue;
                }
                mergeUsage(
                    passIndex,
                    PassUsage{
                        *resource,
                        reference.state,
                        reference.stages,
                        reference.access,
                        !writes,
                        writes,
                        false},
                    writes ? "writes" : "reads");
            }
        };
        addReferences(pass.reads, false);
        addReferences(pass.writes, true);

        const auto addAttachment = [&](
            const RHIRenderGraphAttachmentDesc& attachment,
            bool depthStencil) {
            const auto texture = textureIndices.find(attachment.resourceName);
            if (texture == textureIndices.end()) {
                result.errors.emplace_back(
                    "RenderGraph pass '" + pass.name + "' attachment references unknown texture '" +
                    attachment.resourceName + "'");
                return;
            }

            const bool reads = attachment.loadOp == RHILoadOp::Load;
            const RHIRenderGraphResourceId resource{
                RHIRenderGraphResourceType::Texture,
                texture->second};
            mergeUsage(
                passIndex,
                PassUsage{
                    resource,
                    depthStencil ? RHIResourceState::DepthWrite : RHIResourceState::RenderTarget,
                    depthStencil
                        ? RHIPipelineStage::EarlyFragmentTests |
                              RHIPipelineStage::LateFragmentTests
                        : RHIPipelineStage::ColorAttachmentOutput,
                    depthStencil
                        ? (reads ? RHIAccessFlags::DepthStencilRead : RHIAccessFlags::None) |
                              RHIAccessFlags::DepthStencilWrite
                        : (reads ? RHIAccessFlags::ColorAttachmentRead : RHIAccessFlags::None) |
                              RHIAccessFlags::ColorAttachmentWrite,
                    reads,
                    true,
                    attachment.loadOp != RHILoadOp::Load},
                depthStencil ? "depth attachment" : "color attachment");
        };

        for (const RHIRenderGraphAttachmentDesc& attachment : pass.colorAttachments) {
            addAttachment(attachment, false);
        }
        if (pass.depthStencilAttachment.has_value()) {
            addAttachment(*pass.depthStencilAttachment, true);
        }
    }

    if (!result.errors.empty()) {
        return result;
    }

    // 阶段 3：只使用 dependsOnPasses 建立第一版顺序。
    //
    // 资源 hazard 不能直接按声明顺序分析。消费者可能写在生产者前面，并通过
    // dependsOnPasses 显式指出真实先后关系。先只对显式边排序，再沿该顺序建立
    // RAW/WAR/WAW 边，最后还会对完整依赖图进行第二次拓扑排序。
    const std::vector<u32> explicitOrder = BuildStableTopologicalOrder(passData);
    if (explicitOrder.size() != passData.size()) {
        result.errors.emplace_back("RenderGraph contains an explicit dependency cycle");
        return result;
    }

    // 阶段 4：沿显式顺序扫描每个资源的访问历史，补齐隐式数据依赖：
    //   RAW（Read After Write） ：当前 reader 等待 lastWriter；
    //   WAR（Write After Read） ：当前 writer 等待所有尚未被截断的 readers；
    //   WAW（Write After Write）：当前 writer 等待 lastWriter。
    // Imported 资源已经由图外初始化；内部资源在首个 writer 之前读取则是声明错误。
    std::vector<HazardState> bufferHazards(graph.buffers.size());
    std::vector<HazardState> textureHazards(graph.textures.size());
    for (u32 index = 0; index < graph.buffers.size(); ++index) {
        bufferHazards[index].initialized = IsImported(graph.buffers[index]);
    }
    for (u32 index = 0; index < graph.textures.size(); ++index) {
        textureHazards[index].initialized = IsImported(graph.textures[index]) ||
                                            graph.textures[index].desc.initialState !=
                                                RHIResourceState::Undefined;
    }

    for (const u32 passIndex : explicitOrder) {
        for (const PassUsage& usage : passData[passIndex].usages) {
            HazardState& hazard = usage.resource.IsBuffer()
                                      ? bufferHazards[usage.resource.index]
                                      : textureHazards[usage.resource.index];

            if (usage.reads && hazard.lastWriter == RHI_INVALID_INDEX && !hazard.initialized) {
                const std::string& resourceName = usage.resource.IsBuffer()
                                                      ? graph.buffers[usage.resource.index].name
                                                      : graph.textures[usage.resource.index].name;
                result.errors.emplace_back(
                    "RenderGraph pass '" + graph.passes[passIndex].name +
                    "' reads uninitialized internal resource '" + resourceName + "'");
            }
            if (usage.reads && hazard.lastWriter != RHI_INVALID_INDEX) {
                passData[passIndex].dependencies.insert(hazard.lastWriter);
            }

            if (usage.writes) {
                if (hazard.lastWriter != RHI_INVALID_INDEX) {
                    passData[passIndex].dependencies.insert(hazard.lastWriter);
                }
                for (const u32 reader : hazard.readers) {
                    passData[passIndex].dependencies.insert(reader);
                }
                hazard.readers.clear();
                hazard.lastWriter = passIndex;
                hazard.initialized = true;
            } else if (usage.reads) {
                AddUnique(hazard.readers, passIndex);
            }
        }
    }

    if (!result.errors.empty()) {
        return result;
    }

    // 阶段 5：对“显式边 + hazard 边”做最终稳定拓扑排序。稳定表示多个 pass 同时
    // 可执行时保留原声明顺序，使抓帧、测试结果和调试日志具有可重复性。
    const std::vector<u32> topologicalOrder =
        BuildStableTopologicalOrder(passData);
    if (topologicalOrder.size() != passData.size()) {
        result.errors.emplace_back("RenderGraph contains a dependency cycle");
        return result;
    }

    // 阶段 6：从不可裁剪、具有副作用、Present、写入 imported/output 的根 pass
    // 反向遍历 dependencies。不能抵达任何根的 pass，其结果不会影响帧输出，可以裁剪。
    for (u32 passIndex = 0; passIndex < graph.passes.size(); ++passIndex) {
        const RHIRenderGraphPassDesc& pass = graph.passes[passIndex];
        PassBuildData& build = passData[passIndex];
        build.root = !pass.cullable || pass.hasSideEffect ||
                     pass.type == RHIRenderGraphPassType::Present;
        for (const PassUsage& usage : build.usages) {
            if (!usage.writes) {
                continue;
            }
            if (usage.resource.IsBuffer()) {
                const RHIRenderGraphBufferDesc& resource =
                    graph.buffers[usage.resource.index];
                build.root = build.root || IsImported(resource) || IsOutput(resource.flags);
            } else {
                const RHIRenderGraphTextureDesc& resource =
                    graph.textures[usage.resource.index];
                build.root = build.root || IsImported(resource) || IsOutput(resource.flags);
            }
        }
    }

    std::vector<bool> live(passData.size(), false);
    std::vector<u32> stack;
    for (u32 passIndex = 0; passIndex < passData.size(); ++passIndex) {
        if (passData[passIndex].root) {
            stack.push_back(passIndex);
        }
    }
    while (!stack.empty()) {
        const u32 passIndex = stack.back();
        stack.pop_back();
        if (live[passIndex]) {
            continue;
        }
        live[passIndex] = true;
        for (const u32 dependency : passData[passIndex].dependencies) {
            stack.push_back(dependency);
        }
    }
    for (u32 passIndex = 0; passIndex < live.size(); ++passIndex) {
        result.plan.culledPasses[passIndex] = !live[passIndex];
    }

    // 阶段 7：删除 dead pass，并把 source pass 下标转换成紧凑 compiled pass 下标。
    // 后端按 plan.passes 顺序线性执行，不再参与拓扑排序或字符串资源查找；同时保留
    // sourcePassIndex，以读取本帧可能变化的 clear value、viewport 和 draw workload。
    std::vector<u32> sourceToCompiled(passData.size(), RHI_INVALID_INDEX);
    for (const u32 sourcePassIndex : topologicalOrder) {
        if (!live[sourcePassIndex]) {
            continue;
        }
        sourceToCompiled[sourcePassIndex] = static_cast<u32>(result.plan.passes.size());
        RHICompiledRenderGraphPass compiled{};
        compiled.sourcePassIndex = sourcePassIndex;
        compiled.workloadIndex = passData[sourcePassIndex].workloadIndex;
        compiled.queue = graph.passes[sourcePassIndex].queue;

        const auto& sourceAttachments =
            graph.passes[sourcePassIndex].colorAttachments;
        for (u32 attachmentIndex = 0;
             attachmentIndex < sourceAttachments.size();
             ++attachmentIndex) {
            const RHIRenderGraphAttachmentDesc& attachment =
                sourceAttachments[attachmentIndex];
            compiled.colorAttachments.push_back(
                RHICompiledRenderGraphAttachment{
                    textureIndices.at(attachment.resourceName),
                    attachmentIndex});
        }
        if (graph.passes[sourcePassIndex].depthStencilAttachment.has_value()) {
            const RHIRenderGraphAttachmentDesc& attachment =
                *graph.passes[sourcePassIndex].depthStencilAttachment;
            compiled.depthStencilAttachment = RHICompiledRenderGraphAttachment{
                textureIndices.at(attachment.resourceName),
                0};
        }
        result.plan.passes.push_back(std::move(compiled));
    }

    for (u32 compiledIndex = 0; compiledIndex < result.plan.passes.size(); ++compiledIndex) {
        RHICompiledRenderGraphPass& compiled = result.plan.passes[compiledIndex];
        for (const u32 sourceDependency : passData[compiled.sourcePassIndex].dependencies) {
            if (sourceToCompiled[sourceDependency] != RHI_INVALID_INDEX) {
                compiled.dependencies.push_back(sourceToCompiled[sourceDependency]);
            }
        }
        std::sort(compiled.dependencies.begin(), compiled.dependencies.end());
    }

    // 阶段 8：计算逻辑资源生命周期，并以贪心算法分配 transient 物理槽。
    //
    // 先独立计算逻辑资源的生存区间。后续物理槽分配只允许复用 lastPass < firstPass
    // 的资源，等号表示两个逻辑资源仍在同一个 pass 中同时存活，不能 alias。
    for (u32 compiledIndex = 0; compiledIndex < result.plan.passes.size(); ++compiledIndex) {
        const RHICompiledRenderGraphPass& compiled = result.plan.passes[compiledIndex];
        const PassBuildData& build = passData[compiled.sourcePassIndex];
        for (const PassUsage& usage : build.usages) {
            RHIRenderGraphResourceLifetime& lifetime = usage.resource.IsBuffer()
                                                           ? result.plan.bufferLifetimes[usage.resource.index]
                                                           : result.plan.textureLifetimes[usage.resource.index];
            if (lifetime.firstPass == RHI_INVALID_INDEX) {
                lifetime.firstPass = compiledIndex;
            }
            lifetime.lastPass = compiledIndex;
        }
    }

    // 一个槽代表后端本帧真正创建的一份 native resource。representativeResource
    // 保存描述模板；当前实现要求描述完全兼容，避免尺寸、格式、usage 或 memory
    // 属性不同却误用同一 native allocation。
    struct AllocationSlot {
        u32 representativeResource = RHI_INVALID_INDEX;
        u32 lastPass = RHI_INVALID_INDEX;
        bool reusable = false;
    };

    std::vector<u32> bufferAliasPredecessors(
        graph.buffers.size(),
        RHI_INVALID_INDEX);
    std::vector<u32> bufferOrder(graph.buffers.size());
    for (u32 index = 0; index < bufferOrder.size(); ++index) {
        bufferOrder[index] = index;
    }
    std::stable_sort(
        bufferOrder.begin(),
        bufferOrder.end(),
        [&](u32 lhs, u32 rhs) {
            return result.plan.bufferLifetimes[lhs].firstPass <
                   result.plan.bufferLifetimes[rhs].firstPass;
        });

    std::vector<AllocationSlot> bufferSlots;
    for (const u32 resourceIndex : bufferOrder) {
        const RHIRenderGraphResourceLifetime lifetime =
            result.plan.bufferLifetimes[resourceIndex];
        const RHIRenderGraphBufferDesc& resource = graph.buffers[resourceIndex];
        if (lifetime.firstPass == RHI_INVALID_INDEX || IsImported(resource)) {
            continue;
        }

        u32 selectedSlot = RHI_INVALID_INDEX;
        if (CanAlias(resource)) {
            for (u32 slotIndex = 0; slotIndex < bufferSlots.size(); ++slotIndex) {
                const AllocationSlot& slot = bufferSlots[slotIndex];
                if (slot.reusable && slot.lastPass < lifetime.firstPass &&
                    AreCompatible(
                        graph.buffers[slot.representativeResource].desc,
                        resource.desc)) {
                    selectedSlot = slotIndex;
                    break;
                }
            }
        }

        if (selectedSlot == RHI_INVALID_INDEX) {
            selectedSlot = static_cast<u32>(bufferSlots.size());
            bufferSlots.push_back(AllocationSlot{
                resourceIndex,
                lifetime.lastPass,
                CanAlias(resource)});
        } else {
            bufferAliasPredecessors[resourceIndex] =
                bufferSlots[selectedSlot].lastPass;
            bufferSlots[selectedSlot].lastPass = lifetime.lastPass;
        }
        result.plan.bufferAllocationSlots[resourceIndex] = selectedSlot;
    }
    result.plan.bufferAllocationCount = static_cast<u32>(bufferSlots.size());

    std::vector<u32> textureAliasPredecessors(
        graph.textures.size(),
        RHI_INVALID_INDEX);
    std::vector<u32> textureOrder(graph.textures.size());
    for (u32 index = 0; index < textureOrder.size(); ++index) {
        textureOrder[index] = index;
    }
    std::stable_sort(
        textureOrder.begin(),
        textureOrder.end(),
        [&](u32 lhs, u32 rhs) {
            return result.plan.textureLifetimes[lhs].firstPass <
                   result.plan.textureLifetimes[rhs].firstPass;
        });

    std::vector<AllocationSlot> textureSlots;
    for (const u32 resourceIndex : textureOrder) {
        const RHIRenderGraphResourceLifetime lifetime =
            result.plan.textureLifetimes[resourceIndex];
        const RHIRenderGraphTextureDesc& resource = graph.textures[resourceIndex];
        if (lifetime.firstPass == RHI_INVALID_INDEX || IsImported(resource)) {
            continue;
        }

        u32 selectedSlot = RHI_INVALID_INDEX;
        if (CanAlias(resource)) {
            for (u32 slotIndex = 0; slotIndex < textureSlots.size(); ++slotIndex) {
                const AllocationSlot& slot = textureSlots[slotIndex];
                if (slot.reusable && slot.lastPass < lifetime.firstPass &&
                    AreCompatible(
                        graph.textures[slot.representativeResource].desc,
                        resource.desc)) {
                    selectedSlot = slotIndex;
                    break;
                }
            }
        }

        if (selectedSlot == RHI_INVALID_INDEX) {
            selectedSlot = static_cast<u32>(textureSlots.size());
            textureSlots.push_back(AllocationSlot{
                resourceIndex,
                lifetime.lastPass,
                CanAlias(resource)});
        } else {
            textureAliasPredecessors[resourceIndex] =
                textureSlots[selectedSlot].lastPass;
            textureSlots[selectedSlot].lastPass = lifetime.lastPass;
        }
        result.plan.textureAllocationSlots[resourceIndex] = selectedSlot;
    }
    result.plan.textureAllocationCount = static_cast<u32>(textureSlots.size());

    // 阶段 9：为物理槽复用补 allocation dependency，再生成每个 pass 的 transition。
    //
    // 即使两个逻辑资源没有数据依赖，复用同一片物理内存时也必须等待上一个资源的
    // 最后访问结束。这里补的是 allocation dependency，后端再据 aliasingBarrier
    // 发出真正的内存可见性屏障。
    for (u32 resourceIndex = 0;
         resourceIndex < bufferAliasPredecessors.size();
         ++resourceIndex) {
        if (bufferAliasPredecessors[resourceIndex] != RHI_INVALID_INDEX) {
            AddUnique(
                result.plan.passes[result.plan.bufferLifetimes[resourceIndex].firstPass]
                    .dependencies,
                bufferAliasPredecessors[resourceIndex]);
        }
    }
    for (u32 resourceIndex = 0;
         resourceIndex < textureAliasPredecessors.size();
         ++resourceIndex) {
        if (textureAliasPredecessors[resourceIndex] != RHI_INVALID_INDEX) {
            AddUnique(
                result.plan.passes[result.plan.textureLifetimes[resourceIndex].firstPass]
                    .dependencies,
                textureAliasPredecessors[resourceIndex]);
        }
    }
    for (RHICompiledRenderGraphPass& compiled : result.plan.passes) {
        std::sort(compiled.dependencies.begin(), compiled.dependencies.end());
    }

    // 编译器状态记录“图内上一次逻辑用途”，用于确定哪些 pass 之间需要同步；后端还会
    // 维护 native 当前状态，因为 imported 资源可能刚经历 upload、acquire 或图外操作。
    std::vector<TrackedState> bufferStates(graph.buffers.size());
    std::vector<TrackedState> textureStates(graph.textures.size());
    for (u32 index = 0; index < graph.buffers.size(); ++index) {
        bufferStates[index].state = RHIResourceState::Common;
        bufferStates[index].initialized = IsImported(graph.buffers[index]);
    }
    for (u32 index = 0; index < graph.textures.size(); ++index) {
        textureStates[index].state = graph.textures[index].desc.initialState;
        textureStates[index].initialized = IsImported(graph.textures[index]) ||
                                           textureStates[index].state !=
                                               RHIResourceState::Undefined;
    }

    for (u32 compiledIndex = 0; compiledIndex < result.plan.passes.size(); ++compiledIndex) {
        RHICompiledRenderGraphPass& compiled = result.plan.passes[compiledIndex];
        const PassBuildData& build = passData[compiled.sourcePassIndex];
        for (const PassUsage& usage : build.usages) {
            TrackedState& tracked = usage.resource.IsBuffer()
                                        ? bufferStates[usage.resource.index]
                                        : textureStates[usage.resource.index];
            // 状态、队列变化需要转换；即使状态相同，只要前后任一访问会写，也需要
            // memory dependency。纯 read -> read 且状态/队列不变时可以省略 barrier。
            const bool needsBarrier = !tracked.initialized ||
                                      tracked.state != usage.state ||
                                      tracked.queue != compiled.queue ||
                                      tracked.lastAccessWrote || usage.writes;
            if (needsBarrier) {
                // discardContents 允许丢弃当前逻辑资源的旧内容；aliasingBarrier 则表示
                // 同一物理槽刚由另一个逻辑资源使用过。二者同时为 true 时仍必须等待
                // 前任资源的访问完成，只是不要求保留其像素/字节内容。
                const bool aliasingBarrier = usage.resource.IsBuffer()
                    ? bufferAliasPredecessors[usage.resource.index] != RHI_INVALID_INDEX &&
                          result.plan.bufferLifetimes[usage.resource.index].firstPass ==
                              compiledIndex
                    : textureAliasPredecessors[usage.resource.index] != RHI_INVALID_INDEX &&
                          result.plan.textureLifetimes[usage.resource.index].firstPass ==
                              compiledIndex;
                compiled.transitions.push_back(RHIRenderGraphTransition{
                    usage.resource,
                    tracked.initialized ? tracked.state : RHIResourceState::Undefined,
                    usage.state,
                    tracked.initialized ? tracked.stages : RHIPipelineStage::TopOfPipe,
                    usage.stages == RHIPipelineStage::None
                        ? RHIPipelineStage::AllCommands
                        : usage.stages,
                    tracked.initialized ? tracked.access : RHIAccessFlags::None,
                    usage.access,
                    tracked.queue,
                    compiled.queue,
                    usage.discardContents || !tracked.initialized,
                    aliasingBarrier});
            }

            tracked.state = usage.state;
            tracked.stages = usage.stages == RHIPipelineStage::None
                                 ? RHIPipelineStage::AllCommands
                                 : usage.stages;
            tracked.access = usage.access;
            tracked.queue = compiled.queue;
            tracked.initialized = true;
            tracked.lastAccessWrote = usage.writes;

        }
    }

    return result;
}

} // namespace rhi
