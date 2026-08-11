#include "RHI/RHI.hpp"

#include <type_traits>

int main()
{
    static_assert(std::is_same_v<decltype(RHI::RHIBufferHandle{}.Value), std::uint64_t>);

    RHI::RHIRenderGraph graph;
    RHI::RHIBufferDesc bufferDesc;
    bufferDesc.DebugName = "CompileCheck.Buffer";
    bufferDesc.SizeInBytes = 256;
    bufferDesc.Usage = RHI::RHIBufferUsage::ShaderRead | RHI::RHIBufferUsage::ShaderWrite;
    const RHI::RHIGraphBuffer buffer = graph.CreateBuffer(bufferDesc);

    graph.AddPass("CompileCheck.Pass", RHI::RHIQueueType::Compute,
        [](RHI::IRHICommandList&, const RHI::RHIRenderGraphResources&)
        {
            return RHI::RHIStatus::Ok();
        })
        .ReadWriteBuffer(buffer);

    return buffer.IsValid() ? 0 : 1;
}
