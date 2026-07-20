#include "RenderGraph/RHIRenderGraph.hpp"

#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

rhi::RHIRenderGraphResourceRef BufferRef(
    std::string name,
    rhi::RHIResourceState state,
    rhi::RHIPipelineStage stages) {
    rhi::RHIRenderGraphResourceRef reference{};
    reference.name = std::move(name);
    reference.type = rhi::RHIRenderGraphResourceType::Buffer;
    reference.state = state;
    reference.stages = stages;
    return reference;
}

rhi::RHIRenderGraphResourceRef TextureRef(
    std::string name,
    rhi::RHIResourceState state,
    rhi::RHIPipelineStage stages) {
    rhi::RHIRenderGraphResourceRef reference{};
    reference.name = std::move(name);
    reference.type = rhi::RHIRenderGraphResourceType::Texture;
    reference.state = state;
    reference.stages = stages;
    return reference;
}

void TestHazardsCullingAndLifetimes() {
    rhi::RHIRenderGraphDesc graph{};

    rhi::RHIRenderGraphBufferDesc lighting{};
    lighting.name = "Lighting";
    lighting.desc.size = 256;
    lighting.flags = rhi::RHIRenderGraphResourceFlags::Transient;
    graph.buffers.push_back(lighting);

    rhi::RHIRenderGraphBufferDesc unused{};
    unused.name = "Unused";
    unused.desc.size = 64;
    unused.flags = rhi::RHIRenderGraphResourceFlags::Transient;
    graph.buffers.push_back(unused);

    rhi::RHIRenderGraphTextureDesc output{};
    output.name = "Output";
    output.imported = true;
    output.externalHandle = rhi::RHITexture(1);
    output.desc.initialState = rhi::RHIResourceState::Present;
    graph.textures.push_back(output);

    rhi::RHIRenderGraphPassDesc lightingPass{};
    lightingPass.name = "Lighting";
    lightingPass.type = rhi::RHIRenderGraphPassType::Compute;
    lightingPass.queue = rhi::RHIQueueType::Compute;
    lightingPass.writes.push_back(BufferRef(
        "Lighting",
        rhi::RHIResourceState::ShaderWrite,
        rhi::RHIPipelineStage::ComputeShader));
    graph.passes.push_back(lightingPass);

    rhi::RHIRenderGraphPassDesc unusedPass{};
    unusedPass.name = "Unused";
    unusedPass.type = rhi::RHIRenderGraphPassType::Compute;
    unusedPass.writes.push_back(BufferRef(
        "Unused",
        rhi::RHIResourceState::ShaderWrite,
        rhi::RHIPipelineStage::ComputeShader));
    graph.passes.push_back(unusedPass);

    rhi::RHIRenderGraphPassDesc composite{};
    composite.name = "Composite";
    composite.reads.push_back(BufferRef(
        "Lighting",
        rhi::RHIResourceState::ShaderRead,
        rhi::RHIPipelineStage::FragmentShader));
    composite.writes.push_back(TextureRef(
        "Output",
        rhi::RHIResourceState::RenderTarget,
        rhi::RHIPipelineStage::ColorAttachmentOutput));
    graph.passes.push_back(composite);

    const rhi::RHIRenderGraphCompileResult result = rhi::CompileRHIRenderGraph(graph);
    Check(result.Succeeded(), result.ErrorMessage().c_str());
    Check(result.plan.passes.size() == 2, "Unused RenderGraph pass was not culled");
    Check(result.plan.culledPasses[1], "Unused pass culling flag was not recorded");
    Check(result.plan.passes[0].sourcePassIndex == 0, "Producer pass order is wrong");
    Check(result.plan.passes[1].sourcePassIndex == 2, "Consumer pass order is wrong");
    Check(
        result.plan.passes[1].dependencies.size() == 1 &&
            result.plan.passes[1].dependencies[0] == 0,
        "RAW resource dependency was not compiled");
    Check(
        result.plan.bufferLifetimes[0].firstPass == 0 &&
            result.plan.bufferLifetimes[0].lastPass == 1,
        "Buffer lifetime does not span producer and consumer");
}

void TestExplicitOrderingAndCycleDetection() {
    rhi::RHIRenderGraphDesc graph{};

    rhi::RHIRenderGraphBufferDesc intermediate{};
    intermediate.name = "Intermediate";
    intermediate.desc.size = 64;
    graph.buffers.push_back(intermediate);

    rhi::RHIRenderGraphPassDesc consumer{};
    consumer.name = "Consumer";
    consumer.dependsOnPasses.push_back("Producer");
    consumer.hasSideEffect = true;
    consumer.reads.push_back(BufferRef(
        "Intermediate",
        rhi::RHIResourceState::ShaderRead,
        rhi::RHIPipelineStage::FragmentShader));
    graph.passes.push_back(consumer);

    rhi::RHIRenderGraphPassDesc producer{};
    producer.name = "Producer";
    producer.hasSideEffect = true;
    producer.writes.push_back(BufferRef(
        "Intermediate",
        rhi::RHIResourceState::ShaderWrite,
        rhi::RHIPipelineStage::ComputeShader));
    graph.passes.push_back(producer);

    rhi::RHIRenderGraphCompileResult result = rhi::CompileRHIRenderGraph(graph);
    Check(result.Succeeded(), result.ErrorMessage().c_str());
    Check(result.plan.passes[0].sourcePassIndex == 1, "Explicit dependency did not reorder passes");
    Check(result.plan.passes[1].sourcePassIndex == 0, "Explicit dependent pass order is wrong");

    graph.passes[1].dependsOnPasses.push_back("Consumer");
    result = rhi::CompileRHIRenderGraph(graph);
    Check(!result.Succeeded(), "RenderGraph dependency cycle was accepted");
    Check(
        result.ErrorMessage().find("cycle") != std::string::npos,
        "Cycle error does not identify the dependency cycle");
}

void TestAttachmentInference() {
    rhi::RHIRenderGraphDesc graph{};
    rhi::RHIRenderGraphTextureDesc color{};
    color.name = "BackBuffer";
    color.imported = true;
    color.externalHandle = rhi::RHITexture(9);
    color.desc.initialState = rhi::RHIResourceState::Present;
    graph.textures.push_back(color);

    rhi::RHIRenderGraphPassDesc pass{};
    pass.name = "Opaque";
    rhi::RHIRenderGraphAttachmentDesc attachment{};
    attachment.resourceName = "BackBuffer";
    attachment.loadOp = rhi::RHILoadOp::Clear;
    pass.colorAttachments.push_back(attachment);
    graph.passes.push_back(pass);

    const rhi::RHIRenderGraphCompileResult result = rhi::CompileRHIRenderGraph(graph);
    Check(result.Succeeded(), result.ErrorMessage().c_str());
    Check(result.plan.passes.size() == 1, "Imported attachment pass was incorrectly culled");
    Check(
        result.plan.passes[0].colorAttachments.size() == 1 &&
            result.plan.passes[0].colorAttachments[0].textureIndex == 0,
        "Attachment name was not compiled to a texture index");
    Check(
        !result.plan.passes[0].transitions.empty() &&
            result.plan.passes[0].transitions[0].after ==
                rhi::RHIResourceState::RenderTarget,
        "Attachment did not infer a render-target transition");
}

void TestInvalidGraphs() {
    rhi::RHIRenderGraphDesc graph{};
    rhi::RHIRenderGraphBufferDesc buffer{};
    buffer.name = "Internal";
    buffer.desc.size = 64;
    graph.buffers.push_back(buffer);

    rhi::RHIRenderGraphPassDesc pass{};
    pass.name = "ReadBeforeWrite";
    pass.hasSideEffect = true;
    pass.reads.push_back(BufferRef(
        "Internal",
        rhi::RHIResourceState::ShaderRead,
        rhi::RHIPipelineStage::ComputeShader));
    graph.passes.push_back(pass);

    rhi::RHIRenderGraphCompileResult result = rhi::CompileRHIRenderGraph(graph);
    Check(!result.Succeeded(), "Read-before-write of an internal resource was accepted");
    Check(
        result.ErrorMessage().find("uninitialized") != std::string::npos,
        "Read-before-write error is not actionable");

    graph = {};
    rhi::RHIRenderGraphPassDesc known{};
    known.name = "Known";
    known.hasSideEffect = true;
    graph.passes.push_back(known);
    rhi::RHIRenderPassWorkload workload{};
    workload.passName = "Missing";
    result = rhi::CompileRHIRenderGraph(graph, std::span{&workload, 1U});
    Check(!result.Succeeded(), "Workload for an unknown pass was accepted");

    graph = {};
    rhi::RHIRenderGraphPassDesc raster{};
    raster.name = "Raster";
    raster.hasSideEffect = true;
    graph.passes.push_back(raster);
    workload = {};
    workload.passName = "Raster";
    workload.bufferCopies.push_back({});
    result = rhi::CompileRHIRenderGraph(graph, std::span{&workload, 1U});
    Check(
        !result.Succeeded(),
        "Transfer commands inside a Raster RenderGraph pass were accepted");
}

void TestStructureHash() {
    rhi::RHIRenderGraphDesc graph{};

    rhi::RHIRenderGraphTextureDesc color{};
    color.name = "Color";
    color.imported = true;
    color.externalHandle = rhi::RHITexture(1);
    color.desc.initialState = rhi::RHIResourceState::Present;
    graph.textures.push_back(color);

    rhi::RHIRenderGraphPassDesc pass{};
    pass.name = "Opaque";
    rhi::RHIRenderGraphAttachmentDesc attachment{};
    attachment.resourceName = "Color";
    attachment.clearValue.color = {0.1F, 0.2F, 0.3F, 1.0F};
    pass.colorAttachments.push_back(attachment);
    graph.passes.push_back(pass);

    rhi::RHIRenderPassWorkload workload{};
    workload.passName = "Opaque";
    const rhi::u64 originalHash = rhi::HashRHIRenderGraphStructure(
        graph,
        std::span{&workload, 1U});

    graph.passes[0].colorAttachments[0].clearValue.color =
        {0.9F, 0.8F, 0.7F, 1.0F};
    workload.viewport.width = 1920.0F;
    Check(
        rhi::HashRHIRenderGraphStructure(graph, std::span{&workload, 1U}) ==
            originalHash,
        "Dynamic frame data unexpectedly invalidated the RenderGraph plan cache");

    graph.passes[0].colorAttachments[0].loadOp = rhi::RHILoadOp::Load;
    Check(
        rhi::HashRHIRenderGraphStructure(graph, std::span{&workload, 1U}) !=
            originalHash,
        "Attachment dependency changes did not invalidate the RenderGraph plan cache");

    graph.passes[0].colorAttachments[0].loadOp = rhi::RHILoadOp::Clear;
    rhi::RHIDispatchCommand dispatch{};
    workload.dispatches.push_back(dispatch);
    Check(
        rhi::HashRHIRenderGraphStructure(graph, std::span{&workload, 1U}) !=
            originalHash,
        "Workload command category changes did not invalidate the RenderGraph plan cache");
}

void TestTransientAliasing() {
    rhi::RHIRenderGraphDesc graph{};
    for (const char* name : {"A", "B", "Incompatible"}) {
        rhi::RHIRenderGraphBufferDesc buffer{};
        buffer.name = name;
        buffer.desc.size = std::string(name) == "Incompatible" ? 128 : 64;
        buffer.desc.usage = rhi::RHIBufferUsage::Storage;
        buffer.desc.lifetime = rhi::RHIResourceLifetime::Transient;
        buffer.flags = rhi::RHIRenderGraphResourceFlags::Transient |
                       rhi::RHIRenderGraphResourceFlags::AllowAliasing;
        graph.buffers.push_back(buffer);
    }

    for (rhi::u32 index = 0; index < graph.buffers.size(); ++index) {
        rhi::RHIRenderGraphPassDesc pass{};
        pass.name = "Write" + std::to_string(index);
        pass.type = rhi::RHIRenderGraphPassType::Compute;
        pass.hasSideEffect = true;
        pass.writes.push_back(BufferRef(
            graph.buffers[index].name,
            rhi::RHIResourceState::ShaderWrite,
            rhi::RHIPipelineStage::ComputeShader));
        graph.passes.push_back(pass);
    }

    const rhi::RHIRenderGraphCompileResult result =
        rhi::CompileRHIRenderGraph(graph);
    Check(result.Succeeded(), result.ErrorMessage().c_str());
    Check(
        result.plan.bufferAllocationCount == 2,
        "Compatible non-overlapping transient buffers did not share a physical slot");
    Check(
        result.plan.bufferAllocationSlots[0] ==
            result.plan.bufferAllocationSlots[1],
        "Compatible transient buffers received different physical slots");
    Check(
        result.plan.bufferAllocationSlots[2] !=
            result.plan.bufferAllocationSlots[0],
        "Incompatible transient buffers incorrectly shared a physical slot");
    Check(
        result.plan.passes[1].dependencies.size() == 1 &&
            result.plan.passes[1].dependencies[0] == 0,
        "Physical allocation reuse did not add an execution dependency");
    Check(
        !result.plan.passes[1].transitions.empty() &&
            result.plan.passes[1].transitions[0].aliasingBarrier,
        "Physical allocation reuse did not request an aliasing barrier");
}

void TestSubmissionValidation() {
    rhi::RHIRenderGraphDesc graph{};
    rhi::RHIRenderGraphPassDesc second{};
    second.name = "Second";
    second.dependsOnPasses.push_back("First");
    second.hasSideEffect = true;
    graph.passes.push_back(second);

    rhi::RHIRenderGraphPassDesc first{};
    first.name = "First";
    first.hasSideEffect = true;
    graph.passes.push_back(first);

    const rhi::RHIRenderGraphCompileResult result =
        rhi::CompileRHIRenderGraph(graph);
    Check(result.Succeeded(), result.ErrorMessage().c_str());

    rhi::RHIQueueSubmitDesc submission{};
    submission.passNames = {"First", "Second"};
    std::string error;
    Check(
        rhi::ValidateRHIRenderGraphSubmissions(
            graph,
            result.plan,
            std::span{&submission, 1U},
            &error),
        error.c_str());

    submission.passNames = {"Second", "First"};
    Check(
        !rhi::ValidateRHIRenderGraphSubmissions(
            graph,
            result.plan,
            std::span{&submission, 1U},
            &error),
        "Out-of-order RenderGraph submission pass names were accepted");

    const std::array submissions{submission, submission};
    Check(
        !rhi::ValidateRHIRenderGraphSubmissions(
            graph,
            result.plan,
            submissions,
            &error),
        "Multiple queue submissions were silently merged");
}

} // namespace

int main() {
    try {
        TestHazardsCullingAndLifetimes();
        TestExplicitOrderingAndCycleDetection();
        TestAttachmentInference();
        TestInvalidGraphs();
        TestStructureHash();
        TestTransientAliasing();
        TestSubmissionValidation();
        std::cout << "All RHI RenderGraph tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "RHI RenderGraph test failed: " << exception.what() << '\n';
        return 1;
    }
}
