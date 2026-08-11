#include "RHI/RHIRenderGraph.hpp"

#include <utility>

namespace RHI
{

RHIBufferHandle RHIRenderGraphResources::GetBuffer(const RHIGraphBuffer buffer) const noexcept
{
    return buffer.IsValid() && buffer.Index < m_buffers.size() ? m_buffers[buffer.Index] : RHIBufferHandle{};
}

RHITextureHandle RHIRenderGraphResources::GetTexture(const RHIGraphTexture texture) const noexcept
{
    return texture.IsValid() && texture.Index < m_textures.size() ? m_textures[texture.Index] : RHITextureHandle{};
}

RHIRenderGraphPassBuilder::RHIRenderGraphPassBuilder(RHIRenderGraph& graph, const std::uint32_t passIndex) noexcept
    : m_graph(graph)
    , m_passIndex(passIndex)
{
}

RHIRenderGraphPassBuilder& RHIRenderGraphPassBuilder::ReadBuffer(const RHIGraphBuffer buffer, const RHIResourceState state)
{
    m_graph.DeclareUse(m_passIndex, true, buffer.Index, state, RHIRenderGraph::Access::Read);
    return *this;
}

RHIRenderGraphPassBuilder& RHIRenderGraphPassBuilder::WriteBuffer(const RHIGraphBuffer buffer, const RHIResourceState state)
{
    m_graph.DeclareUse(m_passIndex, true, buffer.Index, state, RHIRenderGraph::Access::Write);
    return *this;
}

RHIRenderGraphPassBuilder& RHIRenderGraphPassBuilder::ReadWriteBuffer(const RHIGraphBuffer buffer, const RHIResourceState state)
{
    m_graph.DeclareUse(m_passIndex, true, buffer.Index, state, RHIRenderGraph::Access::ReadWrite);
    return *this;
}

RHIRenderGraphPassBuilder& RHIRenderGraphPassBuilder::ReadTexture(const RHIGraphTexture texture, const RHIResourceState state)
{
    m_graph.DeclareUse(m_passIndex, false, texture.Index, state, RHIRenderGraph::Access::Read);
    return *this;
}

RHIRenderGraphPassBuilder& RHIRenderGraphPassBuilder::WriteTexture(const RHIGraphTexture texture, const RHIResourceState state)
{
    m_graph.DeclareUse(m_passIndex, false, texture.Index, state, RHIRenderGraph::Access::Write);
    return *this;
}

RHIRenderGraphPassBuilder& RHIRenderGraphPassBuilder::ReadWriteTexture(const RHIGraphTexture texture, const RHIResourceState state)
{
    m_graph.DeclareUse(m_passIndex, false, texture.Index, state, RHIRenderGraph::Access::ReadWrite);
    return *this;
}

RHIGraphBuffer RHIRenderGraph::CreateBuffer(const RHIBufferDesc& desc)
{
    BufferNode node;
    node.Desc = desc;
    node.InitialState = desc.InitialState;
    node.FinalState = desc.InitialState;
    m_buffers.push_back(std::move(node));
    return {static_cast<std::uint32_t>(m_buffers.size() - 1u)};
}

RHIGraphBuffer RHIRenderGraph::ImportBuffer(const RHIBufferHandle buffer, const RHIResourceState initialState, const RHIResourceState finalState)
{
    BufferNode node;
    node.Imported = buffer;
    node.InitialState = initialState;
    node.FinalState = finalState;
    node.IsImported = true;
    m_buffers.push_back(std::move(node));
    return {static_cast<std::uint32_t>(m_buffers.size() - 1u)};
}

RHIGraphTexture RHIRenderGraph::CreateTexture(const RHITextureDesc& desc)
{
    TextureNode node;
    node.Desc = desc;
    node.InitialState = desc.InitialState;
    node.FinalState = desc.InitialState;
    m_textures.push_back(std::move(node));
    return {static_cast<std::uint32_t>(m_textures.size() - 1u)};
}

RHIGraphTexture RHIRenderGraph::ImportTexture(const RHITextureHandle texture, const RHIResourceState initialState, const RHIResourceState finalState)
{
    TextureNode node;
    node.Imported = texture;
    node.InitialState = initialState;
    node.FinalState = finalState;
    node.IsImported = true;
    m_textures.push_back(std::move(node));
    return {static_cast<std::uint32_t>(m_textures.size() - 1u)};
}

RHIRenderGraphPassBuilder RHIRenderGraph::AddPass(std::string name, const RHIQueueType queue, RHIRenderGraphPassCallback callback)
{
    m_passes.push_back({std::move(name), queue, std::move(callback), {}});
    return {*this, static_cast<std::uint32_t>(m_passes.size() - 1u)};
}

void RHIRenderGraph::DeclareUse(const std::uint32_t passIndex, const bool isBuffer, const std::uint32_t resourceIndex, const RHIResourceState state, const Access access)
{
    if (passIndex >= m_passes.size())
    {
        return;
    }
    m_passes[passIndex].Uses.push_back({isBuffer, resourceIndex, state, access});
}

RHICompiledRenderGraph::RHICompiledRenderGraph(RHIDevicePtr device)
    : m_device(std::move(device))
{
}

RHICompiledRenderGraph::~RHICompiledRenderGraph()
{
    (void)Release();
}

RHIStatus RHIRenderGraph::Compile(RHIDevicePtr device, std::unique_ptr<RHICompiledRenderGraph>& outCompiled) const
{
    if (device == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "A device is required to compile a render graph.");
    }

    for (const PassNode& pass : m_passes)
    {
        if (pass.Name.empty())
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "Every render-graph pass requires a name.");
        }
        if (!pass.Callback)
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "Every render-graph pass requires a recording callback.");
        }
        for (const ResourceUse& use : pass.Uses)
        {
            const std::size_t resourceCount = use.IsBuffer ? m_buffers.size() : m_textures.size();
            if (use.ResourceIndex >= resourceCount || use.State == RHIResourceState::Undefined)
            {
                return RHIStatus::Error(RHIResult::InvalidArgument, "A render-graph pass contains an invalid resource declaration.");
            }
        }
    }

    for (const BufferNode& node : m_buffers)
    {
        if (node.IsImported && !node.Imported.IsValid())
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "An imported graph buffer has an invalid handle.");
        }
        if (!node.IsImported && node.Desc.SizeInBytes == 0u)
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "A transient graph buffer has zero size.");
        }
    }
    for (const TextureNode& node : m_textures)
    {
        if (node.IsImported && !node.Imported.IsValid())
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "An imported graph texture has an invalid handle.");
        }
        if (!node.IsImported && (!node.Desc.Extent.IsValid() || node.Desc.Format == RHIFormat::Unknown))
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "A transient graph texture has an invalid extent or format.");
        }
    }

    auto compiled = std::unique_ptr<RHICompiledRenderGraph>(new RHICompiledRenderGraph(std::move(device)));
    compiled->m_resources.m_buffers.resize(m_buffers.size());
    compiled->m_resources.m_textures.resize(m_textures.size());

    const auto cleanup = [&compiled]()
    {
        for (const RHIBufferHandle buffer : compiled->m_transientBuffers)
        {
            compiled->m_device->DestroyBuffer(buffer);
        }
        for (const RHITextureHandle texture : compiled->m_transientTextures)
        {
            compiled->m_device->DestroyTexture(texture);
        }
        compiled->m_transientBuffers.clear();
        compiled->m_transientTextures.clear();
    };

    for (std::size_t index = 0; index < m_buffers.size(); ++index)
    {
        const BufferNode& node = m_buffers[index];
        if (node.IsImported)
        {
            compiled->m_resources.m_buffers[index] = node.Imported;
            continue;
        }

        RHIBufferHandle buffer;
        RHIStatus status = compiled->m_device->CreateBuffer(node.Desc, buffer);
        if (!status.Succeeded())
        {
            cleanup();
            return status;
        }
        compiled->m_resources.m_buffers[index] = buffer;
        compiled->m_transientBuffers.push_back(buffer);
    }

    for (std::size_t index = 0; index < m_textures.size(); ++index)
    {
        const TextureNode& node = m_textures[index];
        if (node.IsImported)
        {
            compiled->m_resources.m_textures[index] = node.Imported;
            continue;
        }

        RHITextureHandle texture;
        RHIStatus status = compiled->m_device->CreateTexture(node.Desc, texture);
        if (!status.Succeeded())
        {
            cleanup();
            return status;
        }
        compiled->m_resources.m_textures[index] = texture;
        compiled->m_transientTextures.push_back(texture);
    }

    std::vector<RHIResourceState> bufferStates;
    std::vector<RHIResourceState> textureStates;
    bufferStates.reserve(m_buffers.size());
    textureStates.reserve(m_textures.size());
    for (const BufferNode& node : m_buffers)
    {
        bufferStates.push_back(node.InitialState);
    }
    for (const TextureNode& node : m_textures)
    {
        textureStates.push_back(node.InitialState);
    }

    for (const PassNode& pass : m_passes)
    {
        RHICompiledRenderGraph::CompiledPass compiledPass;
        compiledPass.Name = pass.Name;
        compiledPass.Queue = pass.Queue;
        compiledPass.Callback = pass.Callback;
        for (const ResourceUse& use : pass.Uses)
        {
            if (use.IsBuffer)
            {
                const RHIResourceState before = bufferStates[use.ResourceIndex];
                if (before != use.State || use.Access != Access::Read)
                {
                    compiledPass.Barriers.emplace_back(RHIBufferBarrier{compiled->m_resources.m_buffers[use.ResourceIndex], before, use.State});
                }
                bufferStates[use.ResourceIndex] = use.State;
            }
            else
            {
                const RHIResourceState before = textureStates[use.ResourceIndex];
                if (before != use.State || use.Access != Access::Read)
                {
                    compiledPass.Barriers.emplace_back(RHITextureBarrier{compiled->m_resources.m_textures[use.ResourceIndex], before, use.State});
                }
                textureStates[use.ResourceIndex] = use.State;
            }
        }
        compiled->m_passes.push_back(std::move(compiledPass));
    }

    for (std::size_t index = 0; index < m_buffers.size(); ++index)
    {
        const BufferNode& node = m_buffers[index];
        if (node.IsImported && bufferStates[index] != node.FinalState)
        {
            compiled->m_finalBarriers.emplace_back(RHIBufferBarrier{compiled->m_resources.m_buffers[index], bufferStates[index], node.FinalState});
        }
    }
    for (std::size_t index = 0; index < m_textures.size(); ++index)
    {
        const TextureNode& node = m_textures[index];
        if (node.IsImported && textureStates[index] != node.FinalState)
        {
            compiled->m_finalBarriers.emplace_back(RHITextureBarrier{compiled->m_resources.m_textures[index], textureStates[index], node.FinalState});
        }
    }

    outCompiled = std::move(compiled);
    return RHIStatus::Ok();
}

RHIStatus RHICompiledRenderGraph::Execute()
{
    std::lock_guard lock(m_mutex);
    if (m_released)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "This compiled render graph has been released.");
    }
    if (m_executed)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "A compiled render graph is one-shot and has already executed.");
    }
    m_executed = true;

    std::vector<RHICommandListPtr> submittedLists;
    std::optional<RHIQueueType> previousQueue;
    const auto failAfterIdle = [this](const RHIStatus& status)
    {
        (void)m_device->WaitIdle();
        return status;
    };

    for (const CompiledPass& pass : m_passes)
    {
        if (previousQueue.has_value() && previousQueue.value() != pass.Queue)
        {
            RHIStatus status = m_device->WaitIdle();
            if (!status.Succeeded())
            {
                return status;
            }
            submittedLists.clear();
        }

        RHICommandListPtr commandList;
        RHIStatus status = m_device->CreateCommandList({pass.Name, pass.Queue, RHICommandListLevel::Primary}, commandList);
        if (!status.Succeeded())
        {
            return failAfterIdle(status);
        }
        status = commandList->Begin();
        if (!status.Succeeded())
        {
            return failAfterIdle(status);
        }
        if (!pass.Barriers.empty())
        {
            status = commandList->ResourceBarriers(pass.Barriers);
            if (!status.Succeeded())
            {
                return failAfterIdle(status);
            }
        }
        status = pass.Callback(*commandList, m_resources);
        if (!status.Succeeded())
        {
            return failAfterIdle(status);
        }
        status = commandList->End();
        if (!status.Succeeded())
        {
            return failAfterIdle(status);
        }
        RHIQueueSubmitDesc submit;
        submit.CommandLists.push_back(commandList);
        status = m_device->Submit(pass.Queue, submit);
        if (!status.Succeeded())
        {
            return failAfterIdle(status);
        }
        submittedLists.push_back(std::move(commandList));
        previousQueue = pass.Queue;
    }

    if (!m_finalBarriers.empty())
    {
        RHIStatus status = m_device->WaitIdle();
        if (!status.Succeeded())
        {
            return status;
        }
        submittedLists.clear();
        RHICommandListPtr commandList;
        status = m_device->CreateCommandList({"RenderGraph.FinalTransitions", RHIQueueType::Graphics, RHICommandListLevel::Primary}, commandList);
        if (!status.Succeeded())
        {
            return status;
        }
        status = commandList->Begin();
        if (!status.Succeeded())
        {
            return status;
        }
        status = commandList->ResourceBarriers(m_finalBarriers);
        if (!status.Succeeded())
        {
            return status;
        }
        status = commandList->End();
        if (!status.Succeeded())
        {
            return status;
        }
        RHIQueueSubmitDesc submit;
        submit.CommandLists.push_back(commandList);
        status = m_device->Submit(RHIQueueType::Graphics, submit);
        if (!status.Succeeded())
        {
            return failAfterIdle(status);
        }
        submittedLists.push_back(std::move(commandList));
    }

    return m_device->WaitIdle();
}

RHIStatus RHICompiledRenderGraph::Release()
{
    std::lock_guard lock(m_mutex);
    if (m_released)
    {
        return RHIStatus::Ok();
    }

    RHIStatus status = m_device->WaitIdle();
    if (!status.Succeeded())
    {
        return status;
    }
    for (const RHIBufferHandle buffer : m_transientBuffers)
    {
        m_device->DestroyBuffer(buffer);
    }
    for (const RHITextureHandle texture : m_transientTextures)
    {
        m_device->DestroyTexture(texture);
    }
    m_transientBuffers.clear();
    m_transientTextures.clear();
    m_released = true;
    return RHIStatus::Ok();
}

} // namespace RHI
