#include "Renderer/RenderFrameBuilder.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace renderer {

namespace {

void AppendWorkload(
    rhi::RHIRenderPassWorkload* destination,
    const rhi::RHIRenderPassWorkload& source) {
    const auto append = [](auto* output, const auto& input) {
        output->insert(output->end(), input.begin(), input.end());
    };

    append(&destination->barriers.globals, source.barriers.globals);
    append(&destination->barriers.buffers, source.barriers.buffers);
    append(&destination->barriers.textures, source.barriers.textures);
    append(&destination->debugMarkers, source.debugMarkers);
    append(&destination->bufferCopies, source.bufferCopies);
    append(&destination->textureCopies, source.textureCopies);
    append(&destination->bufferToTextureCopies, source.bufferToTextureCopies);
    append(&destination->textureToBufferCopies, source.textureToBufferCopies);
    append(&destination->textureBlits, source.textureBlits);
    append(&destination->mipmapGenerations, source.mipmapGenerations);
    append(&destination->queryResets, source.queryResets);
    append(&destination->timestampWrites, source.timestampWrites);
    append(&destination->queryResolves, source.queryResolves);
    append(&destination->draws, source.draws);
    append(&destination->indexedDraws, source.indexedDraws);
    append(&destination->indirectDraws, source.indirectDraws);
    append(&destination->indexedIndirectDraws, source.indexedIndirectDraws);
    append(&destination->dispatches, source.dispatches);
    append(&destination->indirectDispatches, source.indirectDispatches);
}

rhi::RHIViewport TargetViewport(const RenderGraphTextureTarget& target) noexcept {
    return {
        0.0F,
        0.0F,
        static_cast<float>(target.desc.extent.width),
        static_cast<float>(target.desc.extent.height),
        0.0F,
        1.0F};
}

rhi::RHIRect2D TargetScissor(const RenderGraphTextureTarget& target) noexcept {
    return {{0, 0}, {target.desc.extent.width, target.desc.extent.height}};
}

rhi::RHIRenderGraphTextureDesc MakeGraphTexture(
    RenderGraphTextureTarget target,
    rhi::RHITextureUsage requiredUsage,
    bool exported = false) {
    target.desc.usage |= requiredUsage;

    rhi::RHIRenderGraphTextureDesc resource{};
    resource.name = std::move(target.name);
    resource.desc = std::move(target.desc);
    resource.externalHandle = target.texture;
    if (target.texture) {
        resource.imported = true;
        resource.flags = rhi::RHIRenderGraphResourceFlags::Imported;
    } else {
        resource.desc.lifetime = rhi::RHIResourceLifetime::Transient;
        resource.flags = rhi::RHIRenderGraphResourceFlags::Transient |
                         rhi::RHIRenderGraphResourceFlags::AllowAliasing;
        if (exported) {
            // 没有 Present pass 的离屏输出仍是图外可观察结果，不能被 dead-pass culling 删除。
            resource.flags |= rhi::RHIRenderGraphResourceFlags::Exported;
        }
    }
    return resource;
}

rhi::RHIRenderGraphAttachmentDesc MakeAttachment(
    const RenderGraphTextureTarget& target,
    rhi::RHILoadOp loadOp) {
    rhi::RHIRenderGraphAttachmentDesc attachment{};
    attachment.resourceName = target.name;
    attachment.aspect = target.aspect;
    attachment.loadOp = loadOp;
    attachment.storeOp = rhi::RHIStoreOp::Store;
    attachment.clearValue = target.clearValue;
    return attachment;
}

void AddRead(
    rhi::RHIRenderGraphPassDesc* pass,
    const std::string& name,
    rhi::RHIRenderGraphResourceType type,
    rhi::RHIResourceState state,
    rhi::RHIPipelineStage stages) {
    pass->reads.push_back({name, type, state, stages});
}

void AddSerialDependency(
    rhi::RHIRenderGraphPassDesc* pass,
    const std::string& previousPassName) {
    if (!previousPassName.empty()) {
        pass->dependsOnPasses.push_back(previousPassName);
    }
}

bool AppendDraw(
    const RenderScene& scene,
    const RenderDrawItem& item,
    RenderDrawPhase phase,
    const RenderFrameBuildDesc& desc,
    rhi::RHIRenderPassWorkload* workload,
    std::vector<std::string>* errors) {
    const rhi::RHIRenderObjectDesc* object = scene.FindObject(item.object);
    if (object == nullptr) {
        errors->push_back("Draw item references a stale RenderObjectHandle");
        return false;
    }

    const rhi::RHIMeshDesc* mesh = scene.FindMesh(object->mesh);
    const rhi::RHIMaterialDesc* material = scene.FindMaterial(object->material);
    if (mesh == nullptr || material == nullptr || object->submeshIndex >= mesh->submeshes.size()) {
        errors->push_back("Render object references an invalid mesh, material, or submesh");
        return false;
    }

    const rhi::RHISubmeshDesc& submesh = mesh->submeshes[object->submeshIndex];
    rhi::RHIPipeline pipeline = material->pipeline;
    std::vector<rhi::RHIBindSet> bindSets = material->bindSets;
    if (phase == RenderDrawPhase::Shadow && desc.shadowPipeline) {
        pipeline = desc.shadowPipeline;
        bindSets = desc.shadowBindSets;
    }

    if (desc.bindingResolver) {
        desc.bindingResolver(
            RenderDrawBindingContext{phase, item.object, *object, *mesh, *material, submesh},
            &pipeline,
            &bindSets);
    }
    if (!pipeline) {
        errors->push_back("Render draw has no valid pipeline: " + object->debugName);
        return false;
    }

    if (mesh->indexStream.has_value() && submesh.indexCount > 0) {
        rhi::RHIDrawIndexedCommand draw{};
        draw.pipeline = pipeline;
        draw.bindSets = std::move(bindSets);
        draw.vertexStreams = mesh->vertexStreams;
        draw.indexStream = *mesh->indexStream;
        draw.indexCount = submesh.indexCount;
        draw.instanceCount = submesh.instanceCount;
        draw.firstIndex = submesh.firstIndex;
        draw.firstInstance = submesh.firstInstance;
        workload->indexedDraws.push_back(std::move(draw));
        return true;
    }

    if (submesh.vertexCount > 0) {
        rhi::RHIDrawCommand draw{};
        draw.pipeline = pipeline;
        draw.bindSets = std::move(bindSets);
        draw.vertexStreams = mesh->vertexStreams;
        draw.vertexCount = submesh.vertexCount;
        draw.instanceCount = submesh.instanceCount;
        draw.firstVertex = submesh.firstVertex;
        draw.firstInstance = submesh.firstInstance;
        workload->draws.push_back(std::move(draw));
        return true;
    }

    errors->push_back("Submesh has neither indices nor vertices: " + object->debugName);
    return false;
}

void AppendDrawList(
    const RenderScene& scene,
    const std::vector<RenderDrawItem>& items,
    RenderDrawPhase phase,
    const RenderFrameBuildDesc& desc,
    rhi::RHIRenderPassWorkload* workload,
    std::vector<std::string>* errors) {
    for (const RenderDrawItem& item : items) {
        AppendDraw(scene, item, phase, desc, workload, errors);
    }
}

void AddSceneObjectsToPacket(const RenderScene& scene, rhi::RHIFramePacket* packet) {
    packet->objects.items.reserve(scene.ActiveObjectCount());
    for (rhi::u32 slotIndex = 0; slotIndex < scene.ObjectSlotCount(); ++slotIndex) {
        const RenderObjectSlotView slot = scene.ObjectAtSlot(slotIndex);
        if (slot.object != nullptr) {
            packet->objects.items.push_back(*slot.object);
        }
    }
}

} // namespace

std::string RenderFrameBuildResult::ErrorMessage() const {
    std::ostringstream stream;
    for (std::size_t index = 0; index < errors.size(); ++index) {
        if (index != 0) {
            stream << '\n';
        }
        stream << errors[index];
    }
    return stream.str();
}

RenderFrameBuildResult RenderFrameBuilder::Build(
    const RenderScene& scene,
    const RenderView& view,
    const RenderFrameBuildDesc& desc) {
    RenderFrameBuildResult result{};
    rhi::RHIFramePacket& packet = result.packet;
    packet.settings = desc.settings;
    packet.swapchain = desc.swapchain;
    packet.uploads = desc.uploads;
    packet.cameras = scene.Cameras();
    packet.cameras.main = view.camera;
    packet.environment = scene.Environment();
    packet.lights = scene.Lights();
    packet.submissions = desc.submissions;
    packet.present = desc.present;
    AddSceneObjectsToPacket(scene, &packet);

    if (desc.outputColor.name.empty()) {
        result.errors.emplace_back("RenderFrameBuildDesc.outputColor.name cannot be empty");
        return result;
    }
    if (desc.outputColor.desc.extent.width == 0 || desc.outputColor.desc.extent.height == 0) {
        result.errors.emplace_back("RenderFrameBuildDesc.outputColor extent cannot be zero");
        return result;
    }
    if (desc.shadowDepth.has_value() && !desc.shadowPipeline && !desc.bindingResolver) {
        result.errors.emplace_back(
            "Shadow pass requires shadowPipeline or a bindingResolver that supplies one");
        return result;
    }
    if (desc.enablePostProcess && !desc.postProcessWorkload.has_value()) {
        result.errors.emplace_back("Post-process is enabled but no postProcessWorkload was provided");
        return result;
    }
    if ((desc.addPresentPass || desc.present.has_value()) &&
        (!desc.outputColor.texture || !desc.outputColor.isSwapchainImage)) {
        result.errors.emplace_back(
            "Present pass requires outputColor to import a swapchain image texture");
        return result;
    }

    packet.graph.buffers = desc.graphBuffers;
    packet.graph.textures = desc.graphTextures;

    RenderGraphTextureTarget sceneColor = desc.outputColor;
    if (desc.enablePostProcess) {
        if (desc.sceneColor.has_value()) {
            sceneColor = *desc.sceneColor;
        } else {
            sceneColor.name = "Renderer.SceneColor";
            sceneColor.texture = {};
            sceneColor.isSwapchainImage = false;
            sceneColor.desc.debugName = "Renderer transient scene color";
            sceneColor.desc.usage = rhi::RHITextureUsage::ColorAttachment |
                                    rhi::RHITextureUsage::Sampled;
            sceneColor.desc.initialState = rhi::RHIResourceState::Undefined;
        }
        if (sceneColor.name == desc.outputColor.name) {
            result.errors.emplace_back("sceneColor and outputColor must have different RenderGraph names");
            return result;
        }
    }

    packet.graph.textures.push_back(MakeGraphTexture(
        desc.outputColor,
        rhi::RHITextureUsage::ColorAttachment |
            (desc.outputColor.isSwapchainImage ? rhi::RHITextureUsage::Present
                                               : rhi::RHITextureUsage::Sampled),
        true));
    if (desc.enablePostProcess) {
        packet.graph.textures.push_back(MakeGraphTexture(
            sceneColor,
            rhi::RHITextureUsage::ColorAttachment | rhi::RHITextureUsage::Sampled));
    }
    if (desc.depth.has_value()) {
        packet.graph.textures.push_back(MakeGraphTexture(
            *desc.depth,
            rhi::RHITextureUsage::DepthStencilAttachment));
    }
    if (desc.shadowDepth.has_value()) {
        packet.graph.textures.push_back(MakeGraphTexture(
            *desc.shadowDepth,
            rhi::RHITextureUsage::DepthStencilAttachment | rhi::RHITextureUsage::Sampled));
    }

    const RenderGraphTextureTarget& renderColor = desc.enablePostProcess ? sceneColor : desc.outputColor;
    std::string previousPass;

    if (desc.shadowDepth.has_value()) {
        rhi::RHIRenderGraphPassDesc pass{};
        pass.name = desc.passNames.shadow;
        pass.depthStencilAttachment = MakeAttachment(*desc.shadowDepth, rhi::RHILoadOp::Clear);
        packet.graph.passes.push_back(pass);

        rhi::RHIRenderPassWorkload workload{};
        workload.passName = pass.name;
        workload.viewport = TargetViewport(*desc.shadowDepth);
        workload.scissor = TargetScissor(*desc.shadowDepth);
        AppendDrawList(
            scene,
            view.draws.shadowCasters,
            RenderDrawPhase::Shadow,
            desc,
            &workload,
            &result.errors);
        packet.workloads.push_back(std::move(workload));
        previousPass = pass.name;
    }

    bool colorWasCleared = false;
    bool depthWasCleared = false;
    const auto addScenePass = [&]<typename AppendFunction>(
                                  const std::string& passName,
                                  AppendFunction appendDraws) {
        rhi::RHIRenderGraphPassDesc pass{};
        pass.name = passName;
        AddSerialDependency(&pass, previousPass);
        pass.colorAttachments.push_back(MakeAttachment(
            renderColor,
            colorWasCleared ? rhi::RHILoadOp::Load : rhi::RHILoadOp::Clear));
        colorWasCleared = true;
        if (desc.depth.has_value()) {
            pass.depthStencilAttachment = MakeAttachment(
                *desc.depth,
                depthWasCleared ? rhi::RHILoadOp::Load : rhi::RHILoadOp::Clear);
            depthWasCleared = true;
        }
        if (desc.shadowDepth.has_value()) {
            AddRead(
                &pass,
                desc.shadowDepth->name,
                rhi::RHIRenderGraphResourceType::Texture,
                rhi::RHIResourceState::ShaderRead,
                rhi::RHIPipelineStage::FragmentShader);
        }
        packet.graph.passes.push_back(pass);

        rhi::RHIRenderPassWorkload workload{};
        workload.passName = pass.name;
        workload.viewport = desc.settings.viewport;
        workload.scissor = desc.settings.scissor;
        appendDraws(&workload);
        packet.workloads.push_back(std::move(workload));
        previousPass = pass.name;
    };

    if (!view.draws.background.empty()) {
        addScenePass(desc.passNames.background, [&](rhi::RHIRenderPassWorkload* workload) {
            AppendDrawList(
                scene,
                view.draws.background,
                RenderDrawPhase::Background,
                desc,
                workload,
                &result.errors);
        });
    }

    // Opaque pass 即使没有物体也保留，用于确定地清理颜色/深度目标。
    addScenePass(desc.passNames.opaque, [&](rhi::RHIRenderPassWorkload* workload) {
        AppendDrawList(
            scene,
            view.draws.opaque,
            RenderDrawPhase::Opaque,
            desc,
            workload,
            &result.errors);
        AppendDrawList(
            scene,
            view.draws.alphaTest,
            RenderDrawPhase::AlphaTest,
            desc,
            workload,
            &result.errors);
    });

    if (!view.draws.transparent.empty()) {
        addScenePass(desc.passNames.transparent, [&](rhi::RHIRenderPassWorkload* workload) {
            AppendDrawList(
                scene,
                view.draws.transparent,
                RenderDrawPhase::Transparent,
                desc,
                workload,
                &result.errors);
        });
    }

    if (desc.enablePostProcess) {
        rhi::RHIRenderGraphPassDesc pass{};
        pass.name = desc.passNames.postProcess;
        AddSerialDependency(&pass, previousPass);
        AddRead(
            &pass,
            sceneColor.name,
            rhi::RHIRenderGraphResourceType::Texture,
            rhi::RHIResourceState::ShaderRead,
            rhi::RHIPipelineStage::FragmentShader);
        pass.colorAttachments.push_back(MakeAttachment(desc.outputColor, rhi::RHILoadOp::Clear));
        packet.graph.passes.push_back(pass);

        rhi::RHIRenderPassWorkload workload = *desc.postProcessWorkload;
        workload.passName = pass.name;
        workload.viewport = desc.settings.viewport;
        workload.scissor = desc.settings.scissor;
        packet.workloads.push_back(std::move(workload));
        previousPass = pass.name;
    }

    if (!view.draws.overlay.empty() || desc.overlayWorkload.has_value()) {
        rhi::RHIRenderGraphPassDesc pass{};
        pass.name = desc.passNames.overlay;
        AddSerialDependency(&pass, previousPass);
        pass.colorAttachments.push_back(MakeAttachment(desc.outputColor, rhi::RHILoadOp::Load));
        packet.graph.passes.push_back(pass);

        rhi::RHIRenderPassWorkload workload{};
        workload.passName = pass.name;
        workload.viewport = desc.settings.viewport;
        workload.scissor = desc.settings.scissor;
        AppendDrawList(
            scene,
            view.draws.overlay,
            RenderDrawPhase::Overlay,
            desc,
            &workload,
            &result.errors);
        if (desc.overlayWorkload.has_value()) {
            AppendWorkload(&workload, *desc.overlayWorkload);
        }
        packet.workloads.push_back(std::move(workload));
        previousPass = pass.name;
    }

    if (desc.addPresentPass || desc.present.has_value()) {
        rhi::RHIRenderGraphPassDesc pass{};
        pass.name = desc.passNames.present;
        pass.type = rhi::RHIRenderGraphPassType::Present;
        pass.queue = rhi::RHIQueueType::Present;
        AddSerialDependency(&pass, previousPass);
        AddRead(
            &pass,
            desc.outputColor.name,
            rhi::RHIRenderGraphResourceType::SwapchainImage,
            rhi::RHIResourceState::Present,
            rhi::RHIPipelineStage::BottomOfPipe);
        packet.graph.passes.push_back(pass);
    }

    if (!result.errors.empty()) {
        return result;
    }

    // 在调用 RHIDevice 前先编译一次图，把重名、读未初始化、环依赖和 pass/workload
    // 类型不匹配等问题变成构建错误。RHIDevice 会利用结构 hash 缓存同一份编译计划。
    const rhi::RHIRenderGraphCompileResult compileResult =
        rhi::CompileRHIRenderGraph(packet.graph, packet.workloads);
    if (!compileResult.Succeeded()) {
        result.errors = compileResult.errors;
    }
    return result;
}

} // namespace renderer
