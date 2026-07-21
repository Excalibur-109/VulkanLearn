#include "Renderer/Renderer.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

rhi::RHICameraData IdentityCamera() {
    rhi::RHICameraData camera{};
    camera.view = float4x4(1.0F);
    camera.projection = float4x4(1.0F);
    camera.viewProjection = float4x4(1.0F);
    camera.position = {0.0F, 0.0F, 0.0F};
    return camera;
}

rhi::RHIMeshDesc IndexedMesh(rhi::u64 vertexBuffer, rhi::u64 indexBuffer) {
    rhi::RHIMeshDesc mesh{};
    mesh.debugName = "IndexedMesh";
    mesh.vertexStreams.push_back({rhi::RHIBuffer(vertexBuffer), 0, 0, 32});
    mesh.indexStream = rhi::RHIIndexStream{rhi::RHIBuffer(indexBuffer), rhi::RHIIndexType::UInt32, 0, 36};
    rhi::RHISubmeshDesc submesh{};
    submesh.name = "IndexedSubmesh";
    submesh.firstIndex = 3;
    submesh.indexCount = 12;
    submesh.instanceCount = 2;
    mesh.submeshes.push_back(submesh);
    return mesh;
}

rhi::RHIMeshDesc NonIndexedMesh(rhi::u64 vertexBuffer) {
    rhi::RHIMeshDesc mesh{};
    mesh.debugName = "NonIndexedMesh";
    mesh.vertexStreams.push_back({rhi::RHIBuffer(vertexBuffer), 0, 16, 24});
    rhi::RHISubmeshDesc submesh{};
    submesh.name = "NonIndexedSubmesh";
    submesh.firstVertex = 4;
    submesh.vertexCount = 6;
    mesh.submeshes.push_back(submesh);
    return mesh;
}

rhi::RHIMaterialDesc Material(rhi::u64 pipeline, rhi::u64 bindSet = 0) {
    rhi::RHIMaterialDesc material{};
    material.debugName = "TestMaterial";
    material.pipeline = rhi::RHIPipeline(pipeline);
    if (bindSet != 0) {
        material.bindSets.push_back(rhi::RHIBindSet(bindSet));
    }
    return material;
}

rhi::RHIRenderObjectDesc Object(
    const char* name,
    rhi::RHIMesh mesh,
    rhi::RHIMaterial material,
    rhi::RHIRenderQueue queue,
    const float3& center,
    rhi::u32 layerMask = 1) {
    rhi::RHIRenderObjectDesc object{};
    object.debugName = name;
    object.mesh = mesh;
    object.material = material;
    object.queue = queue;
    object.layerMask = layerMask;
    object.worldBoundsSphere = {center, 0.05F};
    object.worldBounds = {center - float3(0.05F), center + float3(0.05F)};
    return object;
}

const rhi::RHIRenderPassWorkload* FindWorkload(
    const rhi::RHIFramePacket& packet,
    const std::string& name) {
    const auto iterator = std::find_if(
        packet.workloads.begin(),
        packet.workloads.end(),
        [&](const rhi::RHIRenderPassWorkload& workload) {
            return workload.passName == name;
        });
    return iterator == packet.workloads.end() ? nullptr : &*iterator;
}

renderer::RenderGraphTextureTarget OutputTarget() {
    renderer::RenderGraphTextureTarget target{};
    target.name = "BackBuffer";
    target.texture = rhi::RHITexture(9001);
    target.desc.debugName = "TestBackBuffer";
    target.desc.extent = {128, 96, 1};
    target.desc.format = rhi::RHIFormat::RGBA8_UNorm;
    target.desc.usage = rhi::RHITextureUsage::ColorAttachment |
                        rhi::RHITextureUsage::Present;
    target.desc.initialState = rhi::RHIResourceState::Present;
    target.clearValue.color = {0.1F, 0.2F, 0.3F, 1.0F};
    target.isSwapchainImage = true;
    return target;
}

renderer::RenderFrameBuildDesc BasicBuildDesc() {
    renderer::RenderFrameBuildDesc desc{};
    desc.settings.drawableSize = {128, 96};
    desc.settings.viewport = {0.0F, 0.0F, 128.0F, 96.0F, 0.0F, 1.0F};
    desc.settings.scissor = {{0, 0}, {128, 96}};
    desc.outputColor = OutputTarget();
    return desc;
}

void TestFrustumClassification() {
    const renderer::RenderFrustum frustum =
        renderer::RenderFrustum::FromViewProjectionZO(float4x4(1.0F));

    Check(
        frustum.Classify({{-0.25F, -0.25F, 0.25F}, {0.25F, 0.25F, 0.75F}}) ==
            renderer::RenderFrustumResult::Inside,
        "An inside AABB was not classified as inside");
    Check(
        frustum.Classify({{1.1F, -0.1F, 0.2F}, {1.3F, 0.1F, 0.4F}}) ==
            renderer::RenderFrustumResult::Outside,
        "An outside AABB was not culled");
    Check(
        frustum.Classify({{0.9F, -0.1F, 0.2F}, {1.1F, 0.1F, 0.4F}}) ==
            renderer::RenderFrustumResult::Intersecting,
        "An intersecting AABB was not classified correctly");
    Check(
        frustum.Classify({{0.0F, 0.0F, 0.5F}, 0.1F}) ==
            renderer::RenderFrustumResult::Inside,
        "An inside sphere was not classified as inside");
    Check(
        frustum.Classify({{0.0F, 0.0F, -0.2F}, 0.05F}) ==
            renderer::RenderFrustumResult::Outside,
        "ZO near-plane sphere culling is incorrect");
}

void TestSceneHandleGeneration() {
    renderer::RenderScene scene;
    const rhi::RHIMesh first = scene.RegisterMesh(IndexedMesh(1, 2));
    Check(scene.UnregisterMesh(first), "Failed to unregister a valid mesh");
    const rhi::RHIMesh second = scene.RegisterMesh(IndexedMesh(3, 4));
    Check(first != second, "A reused mesh slot did not advance its generation");
    Check(scene.FindMesh(first) == nullptr, "A stale mesh handle resolved to a new resource");
    Check(scene.FindMesh(second) != nullptr, "A current mesh handle did not resolve");

    const rhi::RHIMaterial material = scene.RegisterMaterial(Material(10));
    const renderer::RenderObjectHandle object = scene.CreateObject(
        Object("OldObject", second, material, rhi::RHIRenderQueue::Opaque, {0.0F, 0.0F, 0.5F}));
    Check(scene.DestroyObject(object), "Failed to destroy a valid object");
    const renderer::RenderObjectHandle replacement = scene.CreateObject(
        Object("NewObject", second, material, rhi::RHIRenderQueue::Opaque, {0.0F, 0.0F, 0.5F}));
    Check(object != replacement, "A reused object slot did not advance its generation");
    Check(scene.FindObject(object) == nullptr, "A stale object handle resolved to a new object");
}

void TestCollectionFilteringAndSorting() {
    renderer::RenderScene scene;
    const rhi::RHIMesh mesh = scene.RegisterMesh(IndexedMesh(10, 11));
    const rhi::RHIMaterial slowMaterial = scene.RegisterMaterial(Material(30));
    const rhi::RHIMaterial fastMaterial = scene.RegisterMaterial(Material(10));

    const renderer::RenderObjectHandle opaqueSlow = scene.CreateObject(
        Object("OpaqueSlow", mesh, slowMaterial, rhi::RHIRenderQueue::Opaque, {-0.2F, 0.0F, 0.3F}));
    const renderer::RenderObjectHandle opaqueFast = scene.CreateObject(
        Object("OpaqueFast", mesh, fastMaterial, rhi::RHIRenderQueue::Opaque, {0.2F, 0.0F, 0.3F}));
    const renderer::RenderObjectHandle transparentNear = scene.CreateObject(
        Object("TransparentNear", mesh, fastMaterial, rhi::RHIRenderQueue::Transparent, {0.0F, 0.0F, 0.2F}));
    const renderer::RenderObjectHandle transparentFar = scene.CreateObject(
        Object("TransparentFar", mesh, fastMaterial, rhi::RHIRenderQueue::Transparent, {0.0F, 0.0F, 0.8F}));
    static_cast<void>(scene.CreateObject(
        Object("Background", mesh, fastMaterial, rhi::RHIRenderQueue::Background, {0.0F, 0.0F, 0.5F})));
    static_cast<void>(scene.CreateObject(
        Object("Overlay", mesh, fastMaterial, rhi::RHIRenderQueue::Overlay, {0.0F, 0.0F, 0.5F})));
    static_cast<void>(scene.CreateObject(
        Object("AlphaTest", mesh, fastMaterial, rhi::RHIRenderQueue::AlphaTest, {0.0F, 0.2F, 0.5F})));
    static_cast<void>(scene.CreateObject(
        Object("OutsideCaster", mesh, fastMaterial, rhi::RHIRenderQueue::Opaque, {2.0F, 0.0F, 0.5F})));

    rhi::RHIRenderObjectDesc invisible =
        Object("Invisible", mesh, fastMaterial, rhi::RHIRenderQueue::Opaque, {0.0F, 0.0F, 0.5F});
    invisible.visible = false;
    static_cast<void>(scene.CreateObject(invisible));
    static_cast<void>(scene.CreateObject(
        Object("WrongLayer", mesh, fastMaterial, rhi::RHIRenderQueue::Opaque, {0.0F, 0.0F, 0.5F}, 2)));

    renderer::RenderCollectOptions options{};
    options.layerMask = 1;
    const renderer::RenderView view =
        renderer::RenderCollector::Collect(scene, IdentityCamera(), options);

    Check(view.visibleObjectCount == 7, "Visibility or layer filtering produced the wrong count");
    Check(view.frustumCulledCount == 1, "Frustum culling produced the wrong count");
    Check(view.draws.background.size() == 1, "Background queue classification failed");
    Check(view.draws.overlay.size() == 1, "Overlay queue classification failed");
    Check(view.draws.alphaTest.size() == 1, "Alpha-test queue classification failed");
    Check(view.draws.opaque.size() == 2, "Opaque queue classification failed");
    Check(view.draws.opaque[0].object == opaqueFast, "Opaque draws were not sorted by pipeline state");
    Check(view.draws.opaque[1].object == opaqueSlow, "Opaque stable state sorting is incorrect");
    Check(view.draws.transparent.size() == 2, "Transparent queue classification failed");
    Check(
        view.draws.transparent[0].object == transparentFar &&
            view.draws.transparent[1].object == transparentNear,
        "Transparent draws were not sorted back-to-front");
    Check(
        view.draws.shadowCasters.size() == 4,
        "Shadow caster collection must include off-camera opaque objects and alpha-test objects");
}

void TestDrawCommandGeneration() {
    renderer::RenderScene scene;
    const rhi::RHIMesh indexed = scene.RegisterMesh(IndexedMesh(100, 101));
    const rhi::RHIMesh nonIndexed = scene.RegisterMesh(NonIndexedMesh(102));
    const rhi::RHIMaterial material = scene.RegisterMaterial(Material(200, 201));
    static_cast<void>(scene.CreateObject(
        Object("Indexed", indexed, material, rhi::RHIRenderQueue::Opaque, {-0.2F, 0.0F, 0.5F})));
    static_cast<void>(scene.CreateObject(
        Object("NonIndexed", nonIndexed, material, rhi::RHIRenderQueue::Opaque, {0.2F, 0.0F, 0.5F})));

    const renderer::RenderView view =
        renderer::RenderCollector::Collect(scene, IdentityCamera());
    const renderer::RenderFrameBuildResult result =
        renderer::RenderFrameBuilder::Build(scene, view, BasicBuildDesc());
    Check(result.Succeeded(), result.ErrorMessage().c_str());

    const rhi::RHIRenderPassWorkload* opaque = FindWorkload(result.packet, "Renderer.Opaque");
    Check(opaque != nullptr, "Opaque workload was not generated");
    Check(opaque->indexedDraws.size() == 1, "Indexed submesh did not produce an indexed draw");
    Check(opaque->draws.size() == 1, "Non-indexed submesh did not produce a draw");
    Check(opaque->indexedDraws[0].indexCount == 12, "Indexed draw count is incorrect");
    Check(opaque->indexedDraws[0].firstIndex == 3, "Indexed draw firstIndex is incorrect");
    Check(opaque->indexedDraws[0].instanceCount == 2, "Indexed draw instanceCount is incorrect");
    Check(opaque->draws[0].vertexCount == 6, "Non-indexed draw vertexCount is incorrect");
    Check(opaque->draws[0].firstVertex == 4, "Non-indexed draw firstVertex is incorrect");
}

void TestStandardRenderGraph() {
    renderer::RenderScene scene;
    const rhi::RHIMesh mesh = scene.RegisterMesh(IndexedMesh(300, 301));
    const rhi::RHIMaterial material = scene.RegisterMaterial(Material(302));
    static_cast<void>(scene.CreateObject(
        Object("Background", mesh, material, rhi::RHIRenderQueue::Background, {0.0F, 0.0F, 0.5F})));
    static_cast<void>(scene.CreateObject(
        Object("Opaque", mesh, material, rhi::RHIRenderQueue::Opaque, {0.0F, 0.0F, 0.4F})));
    static_cast<void>(scene.CreateObject(
        Object("Transparent", mesh, material, rhi::RHIRenderQueue::Transparent, {0.0F, 0.0F, 0.7F})));
    static_cast<void>(scene.CreateObject(
        Object("UI", mesh, material, rhi::RHIRenderQueue::Overlay, {0.0F, 0.0F, 0.2F})));

    const renderer::RenderView view =
        renderer::RenderCollector::Collect(scene, IdentityCamera());
    renderer::RenderFrameBuildDesc desc = BasicBuildDesc();

    renderer::RenderGraphTextureTarget depth{};
    depth.name = "Depth";
    depth.desc.debugName = "TestDepth";
    depth.desc.extent = {128, 96, 1};
    depth.desc.format = rhi::RHIFormat::D32_Float;
    depth.aspect = rhi::RHITextureAspect::Depth;
    desc.depth = depth;

    renderer::RenderGraphTextureTarget shadow = depth;
    shadow.name = "ShadowDepth";
    shadow.desc.debugName = "TestShadowDepth";
    shadow.desc.extent = {256, 256, 1};
    desc.shadowDepth = shadow;
    desc.shadowPipeline = rhi::RHIPipeline(400);

    rhi::RHIRenderPassWorkload postProcess{};
    rhi::RHIDrawCommand fullScreenTriangle{};
    fullScreenTriangle.pipeline = rhi::RHIPipeline(401);
    fullScreenTriangle.vertexCount = 3;
    postProcess.draws.push_back(fullScreenTriangle);
    desc.enablePostProcess = true;
    desc.postProcessWorkload = postProcess;
    desc.addPresentPass = true;

    const renderer::RenderFrameBuildResult result =
        renderer::RenderFrameBuilder::Build(scene, view, desc);
    Check(result.Succeeded(), result.ErrorMessage().c_str());

    const std::vector<std::string> expectedNames = {
        "Renderer.Shadow",
        "Renderer.Background",
        "Renderer.Opaque",
        "Renderer.Transparent",
        "Renderer.PostProcess",
        "Renderer.UI",
        "Renderer.Present"};
    Check(result.packet.graph.passes.size() == expectedNames.size(), "Standard pass count is incorrect");
    for (std::size_t index = 0; index < expectedNames.size(); ++index) {
        const rhi::RHIRenderGraphPassDesc& pass = result.packet.graph.passes[index];
        Check(pass.name == expectedNames[index], "Standard pass order is incorrect");
        if (index > 0) {
            Check(
                pass.dependsOnPasses.size() == 1 &&
                    pass.dependsOnPasses[0] == expectedNames[index - 1],
                "A standard pass is missing its explicit predecessor dependency");
        }
    }

    const rhi::RHIRenderGraphCompileResult compiled =
        rhi::CompileRHIRenderGraph(result.packet.graph, result.packet.workloads);
    Check(compiled.Succeeded(), compiled.ErrorMessage().c_str());
    Check(compiled.plan.passes.size() == expectedNames.size(), "RenderGraph unexpectedly culled a standard pass");
    for (std::size_t index = 1; index < compiled.plan.passes.size(); ++index) {
        Check(
            std::find(
                compiled.plan.passes[index].dependencies.begin(),
                compiled.plan.passes[index].dependencies.end(),
                static_cast<rhi::u32>(index - 1)) != compiled.plan.passes[index].dependencies.end(),
            "Compiled RenderGraph lost a standard pass dependency");
    }
}

} // namespace

int main() {
    try {
        TestFrustumClassification();
        TestSceneHandleGeneration();
        TestCollectionFilteringAndSorting();
        TestDrawCommandGeneration();
        TestStandardRenderGraph();
        std::cout << "All Renderer tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
