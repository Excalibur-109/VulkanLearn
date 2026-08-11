#pragma once

#include "RHI/RHIDevice.hpp"

#include <functional>
#include <mutex>
#include <optional>

namespace RHI
{

/** Identifies a logical buffer owned or imported by a render graph. */
struct RHIGraphBuffer final
{
    /** Invalid graph-resource index. */
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

    /** Zero-based graph-resource index. */
    std::uint32_t Index = InvalidIndex;

    /** Returns true when this identifier refers to a graph buffer. */
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }

    /** Returns true when two identifiers select the same graph buffer. */
    [[nodiscard]] constexpr bool operator==(const RHIGraphBuffer& other) const noexcept { return Index == other.Index; }
};

/** Identifies a logical texture owned or imported by a render graph. */
struct RHIGraphTexture final
{
    /** Invalid graph-resource index. */
    static constexpr std::uint32_t InvalidIndex = std::numeric_limits<std::uint32_t>::max();

    /** Zero-based graph-resource index. */
    std::uint32_t Index = InvalidIndex;

    /** Returns true when this identifier refers to a graph texture. */
    [[nodiscard]] constexpr bool IsValid() const noexcept { return Index != InvalidIndex; }

    /** Returns true when two identifiers select the same graph texture. */
    [[nodiscard]] constexpr bool operator==(const RHIGraphTexture& other) const noexcept { return Index == other.Index; }
};

/** Resolves logical graph resources to concrete handles while a graph pass records commands. */
class RHIRenderGraphResources final
{
public:
    /** Returns the concrete buffer backing a logical graph buffer, or an invalid handle on invalid input. */
    [[nodiscard]] RHIBufferHandle GetBuffer(RHIGraphBuffer buffer) const noexcept;

    /** Returns the concrete texture backing a logical graph texture, or an invalid handle on invalid input. */
    [[nodiscard]] RHITextureHandle GetTexture(RHIGraphTexture texture) const noexcept;

private:
    /** Allows the graph compiler to assemble a resolver. */
    friend class RHICompiledRenderGraph;
    /** Allows the graph compiler to assemble a resolver before execution. */
    friend class RHIRenderGraph;

    /** Concrete handles indexed by logical graph-buffer index. */
    std::vector<RHIBufferHandle> m_buffers;
    /** Concrete handles indexed by logical graph-texture index. */
    std::vector<RHITextureHandle> m_textures;
};

/** Receives a recording command list and resolves graph resources for one render-graph pass. */
using RHIRenderGraphPassCallback = std::function<RHIStatus(IRHICommandList&, const RHIRenderGraphResources&)>;

/** Owns a compiled graph and all temporary resources created for it. */
class RHICompiledRenderGraph;

/** Configures resource dependencies for one render-graph pass. */
class RHIRenderGraphPassBuilder final
{
public:
    /** Declares read-only buffer access in the stated resource state. */
    RHIRenderGraphPassBuilder& ReadBuffer(RHIGraphBuffer buffer, RHIResourceState state = RHIResourceState::ShaderRead);

    /** Declares write-only buffer access in the stated resource state. */
    RHIRenderGraphPassBuilder& WriteBuffer(RHIGraphBuffer buffer, RHIResourceState state = RHIResourceState::UnorderedAccess);

    /** Declares read/write buffer access in the stated resource state. */
    RHIRenderGraphPassBuilder& ReadWriteBuffer(RHIGraphBuffer buffer, RHIResourceState state = RHIResourceState::UnorderedAccess);

    /** Declares read-only texture access in the stated resource state. */
    RHIRenderGraphPassBuilder& ReadTexture(RHIGraphTexture texture, RHIResourceState state = RHIResourceState::ShaderRead);

    /** Declares write-only texture access in the stated resource state. */
    RHIRenderGraphPassBuilder& WriteTexture(RHIGraphTexture texture, RHIResourceState state = RHIResourceState::RenderTarget);

    /** Declares read/write texture access in the stated resource state. */
    RHIRenderGraphPassBuilder& ReadWriteTexture(RHIGraphTexture texture, RHIResourceState state = RHIResourceState::UnorderedAccess);

private:
    /** Restricts construction to the render graph that owns the selected pass. */
    friend class RHIRenderGraph;

    /** Constructs a builder targeting one graph pass. */
    RHIRenderGraphPassBuilder(class RHIRenderGraph& graph, std::uint32_t passIndex) noexcept;

    /** Graph mutated by this builder. */
    class RHIRenderGraph& m_graph;
    /** Zero-based target pass index. */
    std::uint32_t m_passIndex;
};

/** Builds a dependency-aware sequence of backend-neutral rendering passes. */
class RHI_API RHIRenderGraph final
{
public:
    /** Creates a transient buffer owned by the compiled graph. */
    [[nodiscard]] RHIGraphBuffer CreateBuffer(const RHIBufferDesc& desc);

    /** Imports an application-owned buffer with declared initial and final states. */
    [[nodiscard]] RHIGraphBuffer ImportBuffer(RHIBufferHandle buffer, RHIResourceState initialState, RHIResourceState finalState);

    /** Creates a transient texture owned by the compiled graph. */
    [[nodiscard]] RHIGraphTexture CreateTexture(const RHITextureDesc& desc);

    /** Imports an application-owned texture with declared initial and final states. */
    [[nodiscard]] RHIGraphTexture ImportTexture(RHITextureHandle texture, RHIResourceState initialState, RHIResourceState finalState);

    /** Adds a pass and returns a builder for declaring its resource dependencies. */
    [[nodiscard]] RHIRenderGraphPassBuilder AddPass(std::string name, RHIQueueType queue, RHIRenderGraphPassCallback callback);

    /** Creates resources, derives state transitions, and prepares an executable graph. */
    RHIStatus Compile(RHIDevicePtr device, std::unique_ptr<RHICompiledRenderGraph>& outCompiled) const;

private:
    /** Grants the pass builder access to dependency declaration internals. */
    friend class RHIRenderGraphPassBuilder;
    /** Grants the compiled graph access to compiled records. */
    friend class RHICompiledRenderGraph;

    /** Defines whether a pass reads, writes, or both reads and writes a resource. */
    enum class Access : std::uint8_t
    {
        /** The pass only reads the resource. */
        Read,
        /** The pass only writes the resource. */
        Write,
        /** The pass both reads and writes the resource. */
        ReadWrite,
    };

    /** Records one buffer or texture state requirement for a pass. */
    struct ResourceUse final
    {
        /** True for a buffer use and false for a texture use. */
        bool IsBuffer = true;
        /** Index in the corresponding resource vector. */
        std::uint32_t ResourceIndex = 0;
        /** Requested resource state. */
        RHIResourceState State = RHIResourceState::Undefined;
        /** Declared access mode. */
        Access Access = Access::Read;
    };

    /** Stores one logical buffer and its lifetime declarations. */
    struct BufferNode final
    {
        /** Buffer description used for transient creation. */
        RHIBufferDesc Desc{};
        /** Concrete application handle for imported resources. */
        RHIBufferHandle Imported{};
        /** State expected before the first pass. */
        RHIResourceState InitialState = RHIResourceState::Undefined;
        /** State restored after the final pass for imported resources. */
        RHIResourceState FinalState = RHIResourceState::Undefined;
        /** True when Imported is application-owned rather than graph-created. */
        bool IsImported = false;
    };

    /** Stores one logical texture and its lifetime declarations. */
    struct TextureNode final
    {
        /** Texture description used for transient creation. */
        RHITextureDesc Desc{};
        /** Concrete application handle for imported resources. */
        RHITextureHandle Imported{};
        /** State expected before the first pass. */
        RHIResourceState InitialState = RHIResourceState::Undefined;
        /** State restored after the final pass for imported resources. */
        RHIResourceState FinalState = RHIResourceState::Undefined;
        /** True when Imported is application-owned rather than graph-created. */
        bool IsImported = false;
    };

    /** Stores callback and dependencies for one graph pass. */
    struct PassNode final
    {
        /** Debug-only pass name. */
        std::string Name;
        /** Queue requested by this pass. */
        RHIQueueType Queue = RHIQueueType::Graphics;
        /** Application command-recording callback. */
        RHIRenderGraphPassCallback Callback;
        /** Resource access declarations. */
        std::vector<ResourceUse> Uses;
    };

    /** Appends a declared resource use to a pass. */
    void DeclareUse(std::uint32_t passIndex, bool isBuffer, std::uint32_t resourceIndex, RHIResourceState state, Access access);

    /** Logical buffers indexed by RHIGraphBuffer. */
    std::vector<BufferNode> m_buffers;
    /** Logical textures indexed by RHIGraphTexture. */
    std::vector<TextureNode> m_textures;
    /** Passes executed in insertion order after compilation. */
    std::vector<PassNode> m_passes;
};

/** Contains executable pass metadata, generated barriers, and transient resource ownership. */
class RHI_API RHICompiledRenderGraph final
{
public:
    /** Releases graph-owned resources after pending work completes. */
    ~RHICompiledRenderGraph();

    /** Prevents copying graph-owned resources and execution metadata. */
    RHICompiledRenderGraph(const RHICompiledRenderGraph&) = delete;

    /** Prevents copying graph-owned resources and execution metadata. */
    RHICompiledRenderGraph& operator=(const RHICompiledRenderGraph&) = delete;

    /** Records and submits all passes synchronously, including generated barriers. */
    RHIStatus Execute();

    /** Waits for device work and destroys graph-owned temporary resources. */
    RHIStatus Release();

private:
    /** Restricts construction to the graph compiler. */
    friend class RHIRenderGraph;

    /** Describes generated work submitted for one pass. */
    struct CompiledPass final
    {
        /** Debug-only pass name. */
        std::string Name;
        /** Queue used by this pass. */
        RHIQueueType Queue = RHIQueueType::Graphics;
        /** Application command-recording callback. */
        RHIRenderGraphPassCallback Callback;
        /** Barriers emitted before the callback. */
        std::vector<RHIBarrier> Barriers;
    };

    /** Creates a compiled graph retaining the device used for allocations and submissions. */
    explicit RHICompiledRenderGraph(RHIDevicePtr device);

    /** Device that owns every concrete resource in this graph. */
    RHIDevicePtr m_device;
    /** Resolver mapping logical resources to concrete handles. */
    RHIRenderGraphResources m_resources;
    /** Graph-created buffers destroyed by Release. */
    std::vector<RHIBufferHandle> m_transientBuffers;
    /** Graph-created textures destroyed by Release. */
    std::vector<RHITextureHandle> m_transientTextures;
    /** Generated executable passes. */
    std::vector<CompiledPass> m_passes;
    /** Barriers returning imported resources to their required final states. */
    std::vector<RHIBarrier> m_finalBarriers;
    /** Prevents concurrent execution and release. */
    std::mutex m_mutex;
    /** Tracks whether temporary resources have been released. */
    bool m_released = false;
    /** Tracks whether this one-shot graph has already attempted execution. */
    bool m_executed = false;
};

} // namespace RHI
