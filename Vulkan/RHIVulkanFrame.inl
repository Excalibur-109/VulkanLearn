#pragma once

#include "RHIVulkanPrivate.inl"

namespace rhi {

bool RHIVulkan::RecordAndSubmitFrame(
    const RHIFramePacket& packet,
    const RHIRenderGraphExecutionPlan& graphPlan,
    std::string* errorMessage) {
    Impl::FrameContext* frame = nullptr;
    bool frameSubmitted = false;

    // 这些句柄只代表本次 RenderGraph 为内部 transient 资源创建的 RHI 对象。
    // 成功提交后调用 Destroy 不会立刻销毁仍被 GPU 使用的 native 对象：Vulkan 资源层
    // 会按 submission serial 延迟回收。异常发生在提交前时则可以直接清理。
    std::vector<RHIBuffer> transientBuffers;
    std::vector<RHITexture> transientTextures;
    std::vector<RHITextureView> transientTextureViews;

    const auto releaseTransientResources = [&]() noexcept {
        for (auto view = transientTextureViews.rbegin();
             view != transientTextureViews.rend();
             ++view) {
            Destroy(*view);
        }
        for (auto texture = transientTextures.rbegin();
             texture != transientTextures.rend();
             ++texture) {
            Destroy(*texture);
        }
        for (auto buffer = transientBuffers.rbegin();
             buffer != transientBuffers.rend();
             ++buffer) {
            Destroy(*buffer);
        }
        transientTextureViews.clear();
        transientTextures.clear();
        transientBuffers.clear();
    };

    try {
        if (!IsInitialized() || impl_->frameContexts.empty()) {
            throw std::runtime_error("RHIVulkan is not initialized or has no frame contexts");
        }

        frame = &impl_->prepareNextFrameContext();
        VkCommandBuffer commandBuffer = frame->commandBuffer;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("vkBeginCommandBuffer failed");
        }

        const auto bufferDstAccess = [](RHIBufferUsage usage) {
            VkAccessFlags access = 0;
            if (RHIHasAny(usage, RHIBufferUsage::Vertex))   access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
            if (RHIHasAny(usage, RHIBufferUsage::Index))    access |= VK_ACCESS_INDEX_READ_BIT;
            if (RHIHasAny(usage, RHIBufferUsage::Uniform))  access |= VK_ACCESS_UNIFORM_READ_BIT;
            if (RHIHasAny(usage, RHIBufferUsage::Storage))  access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            if (RHIHasAny(usage, RHIBufferUsage::Indirect)) access |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
            if (RHIHasAny(usage, RHIBufferUsage::TransferSource))      access |= VK_ACCESS_TRANSFER_READ_BIT;
            if (RHIHasAny(usage, RHIBufferUsage::TransferDestination)) access |= VK_ACCESS_TRANSFER_WRITE_BIT;
            return access == 0 ? VK_ACCESS_MEMORY_READ_BIT : access;
        };

        const auto bufferDstStages = [](RHIBufferUsage usage) {
            VkPipelineStageFlags stages = 0;
            if (RHIHasAny(usage, RHIBufferUsage::Vertex) ||
                RHIHasAny(usage, RHIBufferUsage::Index)) {
                stages |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
            }
            if (RHIHasAny(usage, RHIBufferUsage::Uniform) ||
                RHIHasAny(usage, RHIBufferUsage::Storage)) {
                stages |= VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT |
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            }
            if (RHIHasAny(usage, RHIBufferUsage::Indirect)) {
                stages |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
            }
            if (RHIHasAny(
                    usage,
                    RHIBufferUsage::TransferSource |
                        RHIBufferUsage::TransferDestination)) {
                stages |= VK_PIPELINE_STAGE_TRANSFER_BIT;
            }
            return stages == 0 ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : stages;
        };

        std::vector<VkBufferMemoryBarrier> uploadBarriers;
        std::vector<RHIBuffer> uploadedBuffers;
        VkPipelineStageFlags uploadDestinationStages = 0;
        for (const RHIBufferUploadDesc& upload : packet.uploads.buffers) {
            if (upload.data.empty()) {
                continue;
            }

            Impl::BufferResource* destination = getRenderResource(impl_->buffers, upload.destination);
            if (destination == nullptr || destination->buffer == VK_NULL_HANDLE) {
                throw std::runtime_error("RHIFramePacket uploads contain an invalid destination buffer");
            }
            if (upload.destinationOffset > destination->desc.size ||
                upload.data.size() >
                    destination->desc.size - upload.destinationOffset) {
                throw std::runtime_error(
                    "RHIFramePacket buffer upload range exceeds destination buffer size");
            }

            Impl::StagingResource staging{};
            VkBufferCreateInfo stagingInfo{};
            stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingInfo.size = static_cast<VkDeviceSize>(upload.data.size());
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(impl_->native.device, &stagingInfo, nullptr, &staging.buffer) != VK_SUCCESS) {
                throw std::runtime_error("vkCreateBuffer(staging) failed");
            }

            // buffer 创建成功后立即登记所有权，后续 memory allocation 失败时也能由 cleanup 回收。
            frame->stagingResources.push_back(staging);
            Impl::StagingResource& trackedStaging = frame->stagingResources.back();

            VkMemoryRequirements requirements{};
            vkGetBufferMemoryRequirements(impl_->native.device, trackedStaging.buffer, &requirements);
            VkMemoryAllocateInfo memoryInfo{};
            memoryInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memoryInfo.allocationSize = requirements.size;
            memoryInfo.memoryTypeIndex = impl_->findMemoryType(
                requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (vkAllocateMemory(impl_->native.device, &memoryInfo, nullptr, &trackedStaging.memory) != VK_SUCCESS) {
                throw std::runtime_error("vkAllocateMemory(staging) failed");
            }
            // 后续 bind/map 都操作 trackedStaging，保证 cleanup 看到最新 buffer/memory 句柄。
            if (vkBindBufferMemory(impl_->native.device, trackedStaging.buffer, trackedStaging.memory, 0) != VK_SUCCESS) {
                throw std::runtime_error("vkBindBufferMemory(staging) failed");
            }

            void* mapped = nullptr;
            if (vkMapMemory(impl_->native.device, trackedStaging.memory, 0, stagingInfo.size, 0, &mapped) != VK_SUCCESS) {
                throw std::runtime_error("vkMapMemory(staging) failed");
            }
            std::memcpy(mapped, upload.data.data(), upload.data.size());
            vkUnmapMemory(impl_->native.device, trackedStaging.memory);

            VkBufferCopy copy{};
            copy.srcOffset = 0;
            copy.dstOffset = static_cast<VkDeviceSize>(upload.destinationOffset);
            copy.size = static_cast<VkDeviceSize>(upload.data.size());
            vkCmdCopyBuffer(commandBuffer, trackedStaging.buffer, destination->buffer, 1, &copy);

            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = bufferDstAccess(destination->desc.usage);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = destination->buffer;
            barrier.offset = upload.destinationOffset;
            barrier.size = upload.data.size();
            uploadBarriers.push_back(barrier);
            uploadedBuffers.push_back(upload.destination);
            uploadDestinationStages |= bufferDstStages(destination->desc.usage);
        }

        if (!uploadBarriers.empty()) {
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                uploadDestinationStages,
                0,
                0,
                nullptr,
                static_cast<u32>(uploadBarriers.size()),
                uploadBarriers.data(),
                0,
                nullptr);

            // 上面的 barrier 已经让 transfer 写入对 buffer 的声明用途可见。同步更新
            // 追踪状态，后续 RenderGraph transition 才会使用真实的 source stage/access，
            // 而不是误以为资源仍停留在 TopOfPipe/None。
            for (const RHIBuffer handle : uploadedBuffers) {
                Impl::BufferResource* buffer =
                    getRenderResource(impl_->buffers, handle);
                if (buffer != nullptr) {
                    buffer->currentState = RHIResourceState::Common;
                    buffer->currentStages = uploadDestinationStages;
                    buffer->currentAccess = bufferDstAccess(buffer->desc.usage);
                }
            }
        }

        // graphBuffers 是“逻辑资源下标 -> RHI 句柄”；physicalGraphBuffers 是“物理槽
        // -> RHI 句柄”。Imported 逻辑资源直接引用 packet 的外部句柄，内部资源才按
        // allocation slot 创建。多个生命周期不重叠的逻辑资源会得到同一个物理句柄。
        std::vector<RHIBuffer> graphBuffers(packet.graph.buffers.size());
        std::vector<RHIBuffer> physicalGraphBuffers(
            graphPlan.bufferAllocationCount);
        for (u32 index = 0; index < packet.graph.buffers.size(); ++index) {
            if (graphPlan.bufferLifetimes[index].firstPass == RHI_INVALID_INDEX) {
                continue;
            }
            const RHIRenderGraphBufferDesc& graphBuffer = packet.graph.buffers[index];
            if (graphBuffer.imported ||
                RHIHasAny(graphBuffer.flags, RHIRenderGraphResourceFlags::Imported)) {
                graphBuffers[index] = graphBuffer.externalHandle;
            } else {
                const u32 slot = graphPlan.bufferAllocationSlots[index];
                if (!physicalGraphBuffers[slot]) {
                    physicalGraphBuffers[slot] = CreateBuffer(graphBuffer.desc);
                    transientBuffers.push_back(physicalGraphBuffers[slot]);
                }
                graphBuffers[index] = physicalGraphBuffers[slot];
            }
        }

        // Texture 使用相同的两级映射。view 跟随物理 texture 创建一次；逻辑资源切换
        // 发生在同一物理槽上时，由后面的 aliasing barrier 处理可见性与 layout。
        std::vector<RHITexture> graphTextures(packet.graph.textures.size());
        std::vector<RHITexture> physicalGraphTextures(
            graphPlan.textureAllocationCount);
        for (u32 index = 0; index < packet.graph.textures.size(); ++index) {
            if (graphPlan.textureLifetimes[index].firstPass == RHI_INVALID_INDEX) {
                continue;
            }
            const RHIRenderGraphTextureDesc& graphTexture = packet.graph.textures[index];
            if (graphTexture.imported ||
                RHIHasAny(graphTexture.flags, RHIRenderGraphResourceFlags::Imported)) {
                graphTextures[index] = graphTexture.externalHandle;
                continue;
            }

            const u32 slot = graphPlan.textureAllocationSlots[index];
            if (physicalGraphTextures[slot]) {
                graphTextures[index] = physicalGraphTextures[slot];
                continue;
            }

            physicalGraphTextures[slot] = CreateTexture(graphTexture.desc);
            graphTextures[index] = physicalGraphTextures[slot];
            transientTextures.push_back(physicalGraphTextures[slot]);

            RHITextureViewDesc viewDesc{};
            viewDesc.debugName = graphTexture.name + ".RenderGraphView";
            viewDesc.texture = graphTextures[index];
            viewDesc.format = graphTexture.desc.format;
            viewDesc.mipLevelCount = graphTexture.desc.mipLevels;
            viewDesc.arrayLayerCount = graphTexture.desc.arrayLayers;
            if (graphTexture.desc.dimension == RHITextureDimension::Texture1D) {
                viewDesc.dimension = graphTexture.desc.arrayLayers > 1
                                         ? RHITextureViewDimension::View1DArray
                                         : RHITextureViewDimension::View1D;
            } else if (graphTexture.desc.dimension == RHITextureDimension::Texture3D) {
                viewDesc.dimension = RHITextureViewDimension::View3D;
            } else if (RHIHasAny(
                           graphTexture.desc.flags,
                           RHITextureCreateFlags::CubeCompatible)) {
                viewDesc.dimension = graphTexture.desc.arrayLayers > 6
                                         ? RHITextureViewDimension::CubeArray
                                         : RHITextureViewDimension::Cube;
            } else {
                viewDesc.dimension = graphTexture.desc.arrayLayers > 1
                                         ? RHITextureViewDimension::View2DArray
                                         : RHITextureViewDimension::View2D;
            }
            if (isDepthFormat(graphTexture.desc.format)) {
                viewDesc.aspect = RHITextureAspect::Depth;
                if (hasStencilFormat(graphTexture.desc.format)) {
                    viewDesc.aspect |= RHITextureAspect::Stencil;
                }
            }
            transientTextureViews.push_back(CreateTextureView(viewDesc));
        }

        const auto findViewForTexture = [&](RHITexture texture, RHITextureAspect aspect) -> RHITextureView {
            for (u64 index = 0; index < impl_->textureViews.size(); ++index) {
                const Impl::TextureViewResource& view = impl_->textureViews[static_cast<size_t>(index)];
                if (view.view != VK_NULL_HANDLE && view.desc.texture == texture &&
                    (aspect == RHITextureAspect::All ||
                     view.desc.aspect == RHITextureAspect::All ||
                     RHIHasAny(view.desc.aspect, aspect))) {
                    return RHITextureView(index + 1);
                }
            }
            return {};
        };

        const auto transitionTexture = [&](
            RHITexture handle,
            RHIResourceState after,
            RHIPipelineStage requestedStages = RHIPipelineStage::AllCommands,
            RHIAccessFlags requestedAccess = RHIAccessFlags::None,
            bool forceBarrier = false,
            bool discardContents = false,
            bool aliasingBarrier = false) {
            // RenderGraph 只描述 pass 读写需要的 RHIResourceState；Vulkan 同步还需要回答：
            //   stage：生产/消费发生在哪一段流水线；
            //   access：该阶段读写哪类内存；
            //   layout：image 以何种专用布局被访问。
            // 三者必须匹配，单独修改 layout 并不能保证前一次写入对后一次读取可见。
            Impl::TextureResource* texture = getRenderResource(impl_->textures, handle);
            if (texture == nullptr || texture->image == VK_NULL_HANDLE) {
                return;
            }

            const VkPipelineStageFlags destinationStages =
                requestedStages == RHIPipelineStage::AllCommands
                    ? stageFromResourceState(after)
                    : toVkPipelineStages(requestedStages);
            const VkAccessFlags destinationAccess =
                requestedAccess == RHIAccessFlags::None
                    ? accessFromResourceState(after)
                    : toVkAccessFlags(requestedAccess);
            if (!forceBarrier && texture->currentState == after &&
                texture->currentStages == destinationStages &&
                texture->currentAccess == destinationAccess) {
                return;
            }

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = discardContents && !aliasingBarrier
                                        ? 0
                                        : texture->currentAccess;
            barrier.dstAccessMask = destinationAccess;
            // 普通首次使用可从 UNDEFINED 开始；物理槽 alias 时仍需保留上一逻辑
            // 资源的真实 layout 作为同步起点，确保旧写入完成后再复用同一 VkImage。
            barrier.oldLayout = discardContents && !aliasingBarrier
                                    ? VK_IMAGE_LAYOUT_UNDEFINED
                                    : toVkImageLayout(texture->currentState);
            barrier.newLayout = toVkImageLayout(after);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture->image;
            barrier.subresourceRange.aspectMask = toVkImageAspect(RHITextureAspect::All, texture->desc.format);
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = texture->desc.mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = texture->desc.arrayLayers;

            vkCmdPipelineBarrier(
                commandBuffer,
                discardContents && !aliasingBarrier
                    ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                    : texture->currentStages,
                destinationStages,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);
            texture->currentState = after;
            texture->currentStages = destinationStages;
            texture->currentAccess = destinationAccess;
        };

        const auto transitionBuffer = [&] (
            RHIBuffer handle,
            const RHIRenderGraphTransition& transition) {
            Impl::BufferResource* buffer = getRenderResource(impl_->buffers, handle);
            if (buffer == nullptr || buffer->buffer == VK_NULL_HANDLE) {
                throw std::runtime_error("RenderGraph buffer transition has an invalid handle");
            }

            const VkPipelineStageFlags destinationStages =
                transition.destinationStages == RHIPipelineStage::AllCommands
                    ? stageFromResourceState(transition.after)
                    : toVkPipelineStages(transition.destinationStages);
            const VkAccessFlags destinationAccess =
                transition.destinationAccess == RHIAccessFlags::None
                    ? accessFromResourceState(transition.after)
                    : toVkAccessFlags(transition.destinationAccess);

            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = transition.discardContents &&
                                            !transition.aliasingBarrier
                                        ? 0
                                        : buffer->currentAccess;
            barrier.dstAccessMask = destinationAccess;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = buffer->buffer;
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(
                commandBuffer,
                transition.discardContents && !transition.aliasingBarrier
                    ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                    : buffer->currentStages,
                destinationStages,
                0,
                0,
                nullptr,
                1,
                &barrier,
                0,
                nullptr);
            buffer->currentState = transition.after;
            buffer->currentStages = destinationStages;
            buffer->currentAccess = destinationAccess;
        };

        const auto vkClearColor = [](const RHIClearColor& color) {
            VkClearValue value{};
            value.color.float32[0] = color.r;
            value.color.float32[1] = color.g;
            value.color.float32[2] = color.b;
            value.color.float32[3] = color.a;
            return value;
        };

        const auto vkClearDepthStencil = [](const RHIClearDepthStencil& clear) {
            VkClearValue value{};
            value.depthStencil.depth = clear.depth;
            value.depthStencil.stencil = clear.stencil;
            return value;
        };

        // plan.passes 已经完成拓扑排序和裁剪，因此后端只需线性录制。每个 pass 先执行
        // 编译器生成的 transition，再录制 workload；这条顺序就是依赖真正落到 GPU
        // command stream 的位置。
        for (const RHICompiledRenderGraphPass& compiledPass : graphPlan.passes) {
            const RHIRenderGraphPassDesc& sourcePass =
                packet.graph.passes[compiledPass.sourcePassIndex];
            for (const RHIRenderGraphTransition& transition : compiledPass.transitions) {
                if (transition.resource.IsBuffer()) {
                    transitionBuffer(
                        graphBuffers[transition.resource.index],
                        transition);
                } else {
                    transitionTexture(
                        graphTextures[transition.resource.index],
                        transition.after,
                        transition.destinationStages,
                        transition.destinationAccess,
                        true,
                        transition.discardContents,
                        transition.aliasingBarrier);
                }
            }

            const RHIRenderPassWorkload* workload =
                compiledPass.workloadIndex == RHI_INVALID_INDEX
                    ? nullptr
                    : &packet.workloads[compiledPass.workloadIndex];
            const bool hasAttachments = !compiledPass.colorAttachments.empty() ||
                                        compiledPass.depthStencilAttachment.has_value();
            if (workload == nullptr && !hasAttachments) {
                continue;
            }

            if (workload != nullptr) {
                // barrier 的唯一事实来源是 RenderGraph reads/writes。若同时接受 workload
                // 手写 barrier，就会出现两套状态追踪互相覆盖。尚未实现的命令也必须
                // 显式失败，不能静默跳过后得到“成功提交但画面错误”的结果。
                if (!workload->barriers.globals.empty() ||
                    !workload->barriers.textures.empty() ||
                    !workload->barriers.buffers.empty()) {
                    throw std::runtime_error(
                        "Explicit workload barriers are not supported; declare RenderGraph reads/writes instead");
                }
                if (!workload->textureCopies.empty() ||
                    !workload->bufferToTextureCopies.empty() ||
                    !workload->textureToBufferCopies.empty() ||
                    !workload->textureBlits.empty() ||
                    !workload->mipmapGenerations.empty()) {
                    throw std::runtime_error(
                        "Vulkan texture copy/blit/mipmap workloads are not implemented yet");
                }
                if (!workload->queryResets.empty() ||
                    !workload->timestampWrites.empty() ||
                    !workload->queryResolves.empty()) {
                    throw std::runtime_error(
                        "Vulkan RenderGraph query workloads are not implemented yet");
                }
                if (!workload->indirectDraws.empty() ||
                    !workload->indexedIndirectDraws.empty() ||
                    !workload->indirectDispatches.empty()) {
                    throw std::runtime_error(
                        "Vulkan RenderGraph indirect workloads are not implemented yet");
                }

                for (const RHIBufferCopyDesc& copy : workload->bufferCopies) {
                    const Impl::BufferResource* source =
                        getRenderResource(impl_->buffers, copy.source);
                    const Impl::BufferResource* destination =
                        getRenderResource(impl_->buffers, copy.destination);
                    if (source == nullptr || destination == nullptr ||
                        source->buffer == VK_NULL_HANDLE ||
                        destination->buffer == VK_NULL_HANDLE) {
                        throw std::runtime_error(
                            "Vulkan RenderGraph buffer copy resource is invalid");
                    }
                    if (copy.size == 0 ||
                        copy.sourceOffset > source->desc.size ||
                        copy.size > source->desc.size - copy.sourceOffset ||
                        copy.destinationOffset > destination->desc.size ||
                        copy.size >
                            destination->desc.size - copy.destinationOffset) {
                        throw std::runtime_error(
                            "Vulkan RenderGraph buffer copy range is invalid");
                    }
                    const VkBufferCopy region{
                        copy.sourceOffset,
                        copy.destinationOffset,
                        copy.size};
                    vkCmdCopyBuffer(
                        commandBuffer,
                        source->buffer,
                        destination->buffer,
                        1,
                        &region);
                }
            }

            if (!hasAttachments) {
                for (const RHIDispatchCommand& dispatch : workload->dispatches) {
                    const Impl::PipelineResource* pipeline =
                        getRenderResource(impl_->pipelines, dispatch.pipeline);
                    if (pipeline == nullptr || pipeline->pipeline == VK_NULL_HANDLE ||
                        pipeline->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE) {
                        throw std::runtime_error("RHIDispatchCommand pipeline is invalid");
                    }
                    vkCmdBindPipeline(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline->pipeline);

                    std::vector<VkDescriptorSet> descriptorSets;
                    descriptorSets.reserve(dispatch.bindSets.size());
                    for (RHIBindSet bindSetHandle : dispatch.bindSets) {
                        const Impl::BindSetResource* bindSet =
                            getRenderResource(impl_->bindSets, bindSetHandle);
                        if (bindSet == nullptr || bindSet->set == VK_NULL_HANDLE) {
                            throw std::runtime_error(
                                "RHIDispatchCommand bind set is invalid");
                        }
                        descriptorSets.push_back(bindSet->set);
                    }
                    if (!descriptorSets.empty()) {
                        vkCmdBindDescriptorSets(
                            commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipeline->layout,
                            0,
                            static_cast<u32>(descriptorSets.size()),
                            descriptorSets.data(),
                            0,
                            nullptr);
                    }
                    vkCmdDispatch(
                        commandBuffer,
                        dispatch.groupCountX,
                        dispatch.groupCountY,
                        dispatch.groupCountZ);
                }
                continue;
            }

            // ExecutionPlan 只缓存 attachment 的整数下标；load/store、clear value 等
            // 动态值仍从当前 packet 的 sourcePass 读取，所以清屏颜色可逐帧变化而不触发
            // RenderGraph 重新编译。
            std::vector<VkRenderingAttachmentInfo> colorAttachments;
            std::vector<VkClearValue> colorClearValues;
            colorAttachments.reserve(compiledPass.colorAttachments.size());
            colorClearValues.reserve(compiledPass.colorAttachments.size());
            for (const RHICompiledRenderGraphAttachment& compiledAttachment :
                 compiledPass.colorAttachments) {
                const RHIRenderGraphAttachmentDesc& attachment =
                    sourcePass.colorAttachments[compiledAttachment.attachmentIndex];
                const RHITexture texture = graphTextures[compiledAttachment.textureIndex];
                const RHITextureView viewHandle = findViewForTexture(texture, RHITextureAspect::Color);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || view->view == VK_NULL_HANDLE) {
                    throw std::runtime_error("RenderGraph color attachment requires a valid texture view");
                }

                colorClearValues.push_back(vkClearColor(attachment.clearValue.color));
                VkRenderingAttachmentInfo colorAttachment{};
                colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                colorAttachment.imageView = view->view;
                colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                colorAttachment.loadOp = toVkLoadOp(attachment.loadOp);
                colorAttachment.storeOp = toVkStoreOp(attachment.storeOp);
                colorAttachment.clearValue = colorClearValues.back();
                colorAttachments.push_back(colorAttachment);
            }

            VkRenderingAttachmentInfo depthAttachment{};
            VkClearValue depthClear{};
            if (compiledPass.depthStencilAttachment.has_value()) {
                const RHICompiledRenderGraphAttachment& compiledAttachment =
                    *compiledPass.depthStencilAttachment;
                const RHIRenderGraphAttachmentDesc& attachment =
                    *sourcePass.depthStencilAttachment;
                const RHITexture texture = graphTextures[compiledAttachment.textureIndex];
                const RHITextureView viewHandle = findViewForTexture(texture, RHITextureAspect::Depth);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || view->view == VK_NULL_HANDLE) {
                    throw std::runtime_error("RenderGraph depth attachment requires a valid texture view");
                }

                depthClear = vkClearDepthStencil(attachment.clearValue.depthStencil);
                depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
                depthAttachment.imageView = view->view;
                depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                depthAttachment.loadOp = toVkLoadOp(attachment.loadOp);
                depthAttachment.storeOp = toVkStoreOp(attachment.storeOp);
                depthAttachment.clearValue = depthClear;
            }

            RHIRect2D renderArea = workload == nullptr ||
                                           workload->scissor.extent.width == 0 ||
                                           workload->scissor.extent.height == 0
                                       ? packet.settings.scissor
                                       : workload->scissor;
            VkRenderingInfo renderingInfo{};
            renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
            renderingInfo.renderArea.offset = {renderArea.offset.x, renderArea.offset.y};
            renderingInfo.renderArea.extent = {renderArea.extent.width, renderArea.extent.height};
            renderingInfo.layerCount = 1;
            renderingInfo.colorAttachmentCount = static_cast<u32>(colorAttachments.size());
            renderingInfo.pColorAttachments = colorAttachments.data();
            renderingInfo.pDepthAttachment = compiledPass.depthStencilAttachment.has_value()
                                                 ? &depthAttachment
                                                 : nullptr;

            vkCmdBeginRendering(commandBuffer, &renderingInfo);

            RHIViewport viewport = workload == nullptr ||
                                           workload->viewport.width == 0.0F ||
                                           workload->viewport.height == 0.0F
                                       ? packet.settings.viewport
                                       : workload->viewport;
            VkViewport vkViewport{};
            vkViewport.x = viewport.x;
            vkViewport.y = viewport.y;
            vkViewport.width = viewport.width;
            vkViewport.height = viewport.height;
            vkViewport.minDepth = viewport.minDepth;
            vkViewport.maxDepth = viewport.maxDepth;
            vkCmdSetViewport(commandBuffer, 0, 1, &vkViewport);

            VkRect2D vkScissor{};
            vkScissor.offset = {renderArea.offset.x, renderArea.offset.y};
            vkScissor.extent = {renderArea.extent.width, renderArea.extent.height};
            vkCmdSetScissor(commandBuffer, 0, 1, &vkScissor);

            const auto recordDraw = [&](const RHIDrawIndexedCommand& draw) {
                // 一个 draw 的最小 Vulkan 绑定集合：pipeline、descriptor sets、vertex buffers、
                // index buffer，然后发出 vkCmdDrawIndexed。
                const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, draw.pipeline);
                if (pipeline == nullptr || pipeline->pipeline == VK_NULL_HANDLE) {
                    throw std::runtime_error("RHIDrawIndexedCommand pipeline is invalid");
                }
                vkCmdBindPipeline(commandBuffer, pipeline->bindPoint, pipeline->pipeline);

                std::vector<VkDescriptorSet> descriptorSets;
                descriptorSets.reserve(draw.bindSets.size());
                for (RHIBindSet bindSetHandle : draw.bindSets) {
                    const Impl::BindSetResource* bindSet = getRenderResource(impl_->bindSets, bindSetHandle);
                    if (bindSet == nullptr || bindSet->set == VK_NULL_HANDLE) {
                        throw std::runtime_error("RHIDrawIndexedCommand bind set is invalid");
                    }
                    descriptorSets.push_back(bindSet->set);
                }
                if (!descriptorSets.empty()) {
                    vkCmdBindDescriptorSets(
                        commandBuffer,
                        pipeline->bindPoint,
                        pipeline->layout,
                        0,
                        static_cast<u32>(descriptorSets.size()),
                        descriptorSets.data(),
                        0,
                        nullptr);
                }

                for (const RHIVertexStream& stream : draw.vertexStreams) {
                    const Impl::BufferResource* vertexBuffer = getRenderResource(impl_->buffers, stream.buffer);
                    if (vertexBuffer == nullptr || vertexBuffer->buffer == VK_NULL_HANDLE) {
                        throw std::runtime_error("RHIDrawIndexedCommand vertex buffer is invalid");
                    }
                    VkBuffer buffer = vertexBuffer->buffer;
                    VkDeviceSize offset = static_cast<VkDeviceSize>(stream.offset);
                    vkCmdBindVertexBuffers(commandBuffer, stream.binding, 1, &buffer, &offset);
                }

                const Impl::BufferResource* indexBuffer = getRenderResource(impl_->buffers, draw.indexStream.buffer);
                if (indexBuffer == nullptr || indexBuffer->buffer == VK_NULL_HANDLE) {
                    throw std::runtime_error("RHIDrawIndexedCommand index buffer is invalid");
                }
                const VkIndexType indexType = draw.indexStream.indexType == RHIIndexType::UInt16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                vkCmdBindIndexBuffer(commandBuffer, indexBuffer->buffer, static_cast<VkDeviceSize>(draw.indexStream.offset), indexType);
                vkCmdDrawIndexed(
                    commandBuffer,
                    draw.indexCount,
                    draw.instanceCount,
                    draw.firstIndex,
                    draw.vertexOffsetElements,
                    draw.firstInstance);
            };

            if (workload != nullptr) {
                for (const RHIDrawCommand& draw : workload->draws) {
                    const Impl::PipelineResource* pipeline =
                        getRenderResource(impl_->pipelines, draw.pipeline);
                    if (pipeline == nullptr || pipeline->pipeline == VK_NULL_HANDLE ||
                        pipeline->bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS) {
                        throw std::runtime_error("RHIDrawCommand pipeline is invalid");
                    }
                    vkCmdBindPipeline(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        pipeline->pipeline);

                    std::vector<VkDescriptorSet> descriptorSets;
                    descriptorSets.reserve(draw.bindSets.size());
                    for (RHIBindSet bindSetHandle : draw.bindSets) {
                        const Impl::BindSetResource* bindSet =
                            getRenderResource(impl_->bindSets, bindSetHandle);
                        if (bindSet == nullptr || bindSet->set == VK_NULL_HANDLE) {
                            throw std::runtime_error("RHIDrawCommand bind set is invalid");
                        }
                        descriptorSets.push_back(bindSet->set);
                    }
                    if (!descriptorSets.empty()) {
                        vkCmdBindDescriptorSets(
                            commandBuffer,
                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeline->layout,
                            0,
                            static_cast<u32>(descriptorSets.size()),
                            descriptorSets.data(),
                            0,
                            nullptr);
                    }
                    for (const RHIVertexStream& stream : draw.vertexStreams) {
                        const Impl::BufferResource* vertexBuffer =
                            getRenderResource(impl_->buffers, stream.buffer);
                        if (vertexBuffer == nullptr ||
                            vertexBuffer->buffer == VK_NULL_HANDLE) {
                            throw std::runtime_error(
                                "RHIDrawCommand vertex buffer is invalid");
                        }
                        const VkBuffer buffer = vertexBuffer->buffer;
                        const VkDeviceSize offset = static_cast<VkDeviceSize>(stream.offset);
                        vkCmdBindVertexBuffers(
                            commandBuffer,
                            stream.binding,
                            1,
                            &buffer,
                            &offset);
                    }
                    vkCmdDraw(
                        commandBuffer,
                        draw.vertexCount,
                        draw.instanceCount,
                        draw.firstVertex,
                        draw.firstInstance);
                }
                for (const RHIDrawIndexedCommand& draw : workload->indexedDraws) {
                    recordDraw(draw);
                }
            }

            vkCmdEndRendering(commandBuffer);
        }

        if (packet.present.has_value()) {
            const Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, packet.present->swapchain);
            if (swapchain != nullptr && packet.present->imageIndex < swapchain->images.size()) {
                transitionTexture(swapchain->images[packet.present->imageIndex], RHIResourceState::Present);
            }
        }

        if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("vkEndCommandBuffer failed");
        }

        // packet 中的 waits/signals 描述外部 GPU 工作依赖；RenderGraph 内部 pass 当前录在
        // 同一 command buffer，天然按命令顺序执行。这里把外部同步值组装到一次 queue submit。
        std::vector<VkSemaphore> waitSignals;
        std::vector<VkPipelineStageFlags> waitStages;
        std::vector<VkSemaphore> signalSemaphores;
        std::vector<u64> waitValues;
        std::vector<u64> signalValues;
        bool usesTimelineSemaphore = false;

        for (const RHIQueueSubmitDesc& submitDesc : packet.submissions) {
            for (const RHIQueueWaitDesc& wait : submitDesc.waits) {
                const Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, wait.signal);
                if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
                    throw std::runtime_error("RHIFramePacket submission contains an invalid wait semaphore");
                }
                usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == RHIGPUWaitGPUSignalType::Timeline;
                waitSignals.push_back(semaphore->semaphore);
                waitStages.push_back(toVkPipelineStages(wait.stages));
                waitValues.push_back(wait.value);
            }
            for (const RHIQueueSignalDesc& signal : submitDesc.signals) {
                const Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, signal.signal);
                if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
                    throw std::runtime_error("RHIFramePacket submission contains an invalid signal semaphore");
                }
                usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == RHIGPUWaitGPUSignalType::Timeline;
                signalSemaphores.push_back(semaphore->semaphore);
                signalValues.push_back(signal.value);
            }
        }

        // 每次图形提交都在同一个 timeline semaphore 上 signal 一个严格递增值。
        // FrameContext 只保存自己对应的值，因此一个 semaphore 就能替代 N 个逐帧 fence。
        const u64 frameCompletionValue = impl_->lastSubmissionSerial + 1;
        signalSemaphores.push_back(impl_->frameTimelineSemaphore);
        signalValues.push_back(frameCompletionValue);
        usesTimelineSemaphore = true;

        VkTimelineSemaphoreSubmitInfo timelineInfo{};
        timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
        timelineInfo.waitSemaphoreValueCount = static_cast<u32>(waitValues.size());
        timelineInfo.pWaitSemaphoreValues = waitValues.data();
        timelineInfo.signalSemaphoreValueCount = static_cast<u32>(signalValues.size());
        timelineInfo.pSignalSemaphoreValues = signalValues.data();

        VkSubmitInfo SubmitInfo{};
        SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        SubmitInfo.pNext = usesTimelineSemaphore ? &timelineInfo : nullptr;
        SubmitInfo.waitSemaphoreCount = static_cast<u32>(waitSignals.size());
        SubmitInfo.pWaitSemaphores = waitSignals.data();
        SubmitInfo.pWaitDstStageMask = waitStages.data();
        SubmitInfo.commandBufferCount = 1;
        SubmitInfo.pCommandBuffers = &commandBuffer;
        SubmitInfo.signalSemaphoreCount = static_cast<u32>(signalSemaphores.size());
        SubmitInfo.pSignalSemaphores = signalSemaphores.data();

        if (vkQueueSubmit(
                impl_->native.graphicsQueue,
                1,
                &SubmitInfo,
                VK_NULL_HANDLE) != VK_SUCCESS) {
            throw std::runtime_error("vkQueueSubmit(recorded frame) failed");
        }
        frameSubmitted = true;
        impl_->deviceKnownIdle = false;
        frame->prepared = false;
        frame->completionValue = frameCompletionValue;
        impl_->lastSubmissionSerial = frameCompletionValue;
        impl_->nextFrameContext =
            (impl_->nextFrameContext + 1) % static_cast<u32>(impl_->frameContexts.size());
        // completionValue 已写入 FrameContext，此后 Destroy 会把 native 资源挂到对应
        // serial 的延迟回收队列，直到 frame timeline 到达该值才真正释放。
        releaseTransientResources();

        if (packet.present.has_value()) {
            return Present(*packet.present, errorMessage);
        }
        return true;
    } catch (const std::exception& error) {
        releaseTransientResources();
        if (frame != nullptr && !frameSubmitted) {
            impl_->releaseStagingResources(*frame);
            frame->prepared = false;
        }
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool RHIVulkan::SubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    RHIRenderGraphCompileResult graph =
        CompileRHIRenderGraph(packet.graph, packet.workloads);
    if (!graph.Succeeded()) {
        if (errorMessage != nullptr) {
            *errorMessage = graph.ErrorMessage();
        }
        return false;
    }
    return SubmitFrame(packet, graph.plan, errorMessage);
}

bool RHIVulkan::SubmitFrame(
    const RHIFramePacket& packet,
    const RHIRenderGraphExecutionPlan& graphPlan,
    std::string* errorMessage) {
    // 自定义 submissions 可以按 passName 表达提交范围。执行前验证它们没有打乱 compiled
    // dependency 顺序；空 submissions 则让本后端把整帧合并成一次图形队列提交。
    if (!ValidateRHIRenderGraphSubmissions(
            packet.graph,
            graphPlan,
            packet.submissions,
            errorMessage)) {
        return false;
    }
    if (!graphPlan.passes.empty() || !packet.uploads.buffers.empty() ||
        !packet.uploads.textures.empty()) {
        return RecordAndSubmitFrame(packet, graphPlan, errorMessage);
    }

    for (const RHIQueueSubmitDesc& submitDesc : packet.submissions) {
        if (!Submit(submitDesc, errorMessage)) {
            return false;
        }
    }
    if (packet.present.has_value()) {
        return Present(*packet.present, errorMessage);
    }
    return true;
}

void RHIVulkan::WaitIdle() const noexcept {
    if (IsInitialized()) {
        vkDeviceWaitIdle(impl_->native.device);
        impl_->completedSubmissionSerial = impl_->lastSubmissionSerial;
        impl_->hasUntrackedSubmissions = false;
        impl_->deviceKnownIdle = true;
        for (Impl::FrameContext& frame : impl_->frameContexts) {
            impl_->releaseStagingResources(frame);
        }
        impl_->flushDeferredReleases();
    }
}

// Destroy 会先清空句柄槽，再按 submission serial 延迟释放 native 对象。

} // namespace rhi













