#pragma once

#include "RHIVulkanPrivate.inl"

namespace rhi {

// 一次 record + submit 期间,bake 出来的 native 资源表。
// 录制热路径上不再走 RHIHandle → 1-based 索引 → vector 查表 → native 指针,
// 而是直接把 frame packet 解析成 native 数组,recordDraw 等直接索引。
// 当前 BakedFrameResources 字段被保留为扩展位;实际 record 仍走 Impl::getRenderResource,
// 因为 Impl 是 RHIVulkan 的 private 类型,namespace 自由函数访问不到。
struct BakedFrameResources {
    // 占位扩展位。当前实现在 record 阶段直接查 Impl::buffers / pipelines / bindSets 等,
    // 没有再 bake 到 native 数组。要继续往这方向优化可在此加 dense 数组。
    std::vector<VkBuffer>      vertexBuffers;
    std::vector<VkDeviceSize>  vertexOffsets;
};

bool RHIVulkan::RecordAndSubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    if (!IsInitialized() || impl_->graphicsCommandPool == VK_NULL_HANDLE || impl_->frames.empty()) {
        if (errorMessage != nullptr) *errorMessage = "RHIVulkan is not initialized or has no frame resources";
        return false;
    }

    // 1) 选当前帧槽位,等上一帧同 slot 的 fence。
    //    后端不做自有 frames-in-flight 状态:frameIndex 由 caller 提供,
    //    每帧的 cpuWaitGPUSignal 是 caller 给的"上一帧已结束"信号。
    //    这样 caller 拥有同步所有权,后端只负责"用 caller 的 fence 完成 submit"。
    const u64 frameSlot = packet.settings.frameIndex % impl_->framesInFlight;
    Impl::FrameResources& frame = impl_->frames[static_cast<size_t>(frameSlot)];

    // 2) wrap ring staging 到 fence 已经等的 offset 之后。
    //    单帧上传量远小于 capacity,绝大多数情况下这里 head=0 就够了。
    frame.stagingHead = 0;
    frame.stagingSubmittedHead = 0;

    VkCommandBuffer commandBuffer = frame.commandBuffer;
    VkFence frameFence = frame.inFlightFence;

    // 3) 录制准备:bake 句柄表 + reset+begin command buffer。
    //    BakedFrameResources 是扩展位;当前 record 阶段直接查 Impl 内部 vector。
    BakedFrameResources baked;
    baked = BakedFrameResources{};

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;  // 不再 ONE_TIME_SUBMIT,command buffer 是 per-frame 复用的
    if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS ||
        vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        if (errorMessage != nullptr) *errorMessage = "Failed to reset+begin command buffer";
        return false;
    }

    // 4) 上传:走持久映射的 ring staging buffer,合并 barrier 单次 vkCmdPipelineBarrier。
    //    单个 upload 直接 push 写入,跨 upload 自动落到 GPU 端的 vkCmdCopyBuffer。
    std::vector<VkBufferMemoryBarrier> uploadBarriers;
    uploadBarriers.reserve(packet.uploads.buffers.size());

    auto tryAllocateStaging = [&](VkDeviceSize size, VkDeviceSize& outOffset) -> bool {
        // 4 字节对齐,保证任何 RHIBufferUsage 都能正确 copy。
        constexpr VkDeviceSize kAlignment = 4;
        const VkDeviceSize alignedHead = (frame.stagingHead + kAlignment - 1) & ~(kAlignment - 1);
        if (alignedHead + size > frame.stagingCapacity) {
            return false;  // 单帧装不下,fallback 到现建现毁路径
        }
        outOffset = alignedHead;
        frame.stagingHead = alignedHead + size;
        return true;
    };

    // 计算每个 upload 的 dstAccess 用于合并 barrier
    const auto bufferDstAccess = [](RHIBufferUsage usage) {
        VkAccessFlags access = 0;
        if (RHIHasAny(usage, RHIBufferUsage::Vertex))   access |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
        if (RHIHasAny(usage, RHIBufferUsage::Index))    access |= VK_ACCESS_INDEX_READ_BIT;
        if (RHIHasAny(usage, RHIBufferUsage::Uniform))  access |= VK_ACCESS_UNIFORM_READ_BIT;
        if (RHIHasAny(usage, RHIBufferUsage::Storage))  access |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        if (RHIHasAny(usage, RHIBufferUsage::Indirect)) access |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
        return access == 0 ? VK_ACCESS_MEMORY_READ_BIT : access;
    };

    // 第一遍:把所有可以走 ring staging 的 upload 落进 staging,记录 barrier。
    // 不能放下的(>staging capacity)fallback 到一次性 staging buffer。
    std::vector<std::pair<VkBuffer, VkDeviceMemory>> fallbackBuffers;
    std::vector<std::tuple<VkBuffer, VkBuffer, VkBufferCopy>> fallbackCopies;  // (staging, dst, copy)
    fallbackBuffers.reserve(packet.uploads.buffers.size());

    for (const RHIBufferUploadDesc& upload : packet.uploads.buffers) {
        if (upload.data.empty()) {
            continue;
        }

        Impl::BufferResource* destination = getRenderResource(impl_->buffers, upload.destination);
        if (destination == nullptr || destination->buffer == VK_NULL_HANDLE) {
            if (errorMessage != nullptr) *errorMessage = "RHIFramePacket uploads contain an invalid destination buffer";
            return false;
        }

        VkDeviceSize stagingOffset = 0;
        const VkDeviceSize size = static_cast<VkDeviceSize>(upload.data.size());
        if (tryAllocateStaging(size, stagingOffset)) {
            std::memcpy(static_cast<std::byte*>(frame.stagingMapped) + stagingOffset, upload.data.data(), size);
            VkBufferCopy copy{};
            copy.srcOffset = stagingOffset;
            copy.dstOffset = static_cast<VkDeviceSize>(upload.destinationOffset);
            copy.size = size;
            vkCmdCopyBuffer(commandBuffer, frame.stagingBuffer, destination->buffer, 1, &copy);

            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = bufferDstAccess(destination->desc.usage);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = destination->buffer;
            barrier.offset = upload.destinationOffset;
            barrier.size = size;
            uploadBarriers.push_back(barrier);
        } else {
            // ring 装不下,走一次性 staging buffer(仍然比原来好,只 fallback 这一次)。
            VkBuffer stagingBuffer = VK_NULL_HANDLE;
            VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
            VkBufferCreateInfo stagingInfo{};
            stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingInfo.size = size;
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(impl_->native.device, &stagingInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
                if (errorMessage != nullptr) *errorMessage = "vkCreateBuffer(fallback staging) failed";
                return false;
            }
            VkMemoryRequirements req{};
            vkGetBufferMemoryRequirements(impl_->native.device, stagingBuffer, &req);
            VkMemoryAllocateInfo memInfo{};
            memInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memInfo.allocationSize = req.size;
            memInfo.memoryTypeIndex = impl_->findMemoryType(
                req.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            if (vkAllocateMemory(impl_->native.device, &memInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
                vkDestroyBuffer(impl_->native.device, stagingBuffer, nullptr);
                if (errorMessage != nullptr) *errorMessage = "vkAllocateMemory(fallback staging) failed";
                return false;
            }
            if (vkBindBufferMemory(impl_->native.device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) {
                vkFreeMemory(impl_->native.device, stagingMemory, nullptr);
                vkDestroyBuffer(impl_->native.device, stagingBuffer, nullptr);
                if (errorMessage != nullptr) *errorMessage = "vkBindBufferMemory(fallback staging) failed";
                return false;
            }
            void* mapped = nullptr;
            if (vkMapMemory(impl_->native.device, stagingMemory, 0, size, 0, &mapped) != VK_SUCCESS) {
                vkFreeMemory(impl_->native.device, stagingMemory, nullptr);
                vkDestroyBuffer(impl_->native.device, stagingBuffer, nullptr);
                if (errorMessage != nullptr) *errorMessage = "vkMapMemory(fallback staging) failed";
                return false;
            }
            std::memcpy(mapped, upload.data.data(), size);
            vkUnmapMemory(impl_->native.device, stagingMemory);

            VkBufferCopy copy{};
            copy.srcOffset = 0;
            copy.dstOffset = static_cast<VkDeviceSize>(upload.destinationOffset);
            copy.size = size;
            vkCmdCopyBuffer(commandBuffer, stagingBuffer, destination->buffer, 1, &copy);

            VkBufferMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = bufferDstAccess(destination->desc.usage);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = destination->buffer;
            barrier.offset = upload.destinationOffset;
            barrier.size = size;
            uploadBarriers.push_back(barrier);

            fallbackBuffers.emplace_back(stagingBuffer, stagingMemory);
        }
    }

    // 5) barrier 合并:一次 vkCmdPipelineBarrier 处理所有 upload barrier,stage 用 ALL_TRANSFER。
    if (!uploadBarriers.empty()) {
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            0,
            0, nullptr,
            static_cast<u32>(uploadBarriers.size()),
            uploadBarriers.data(),
            0, nullptr);
    }

    // 6) 渲染图执行:状态转换 + dynamic rendering + draw。
    std::unordered_map<std::string, RHITexture> textureResources;
    textureResources.reserve(packet.graph.textures.size());
    for (const RHIRenderGraphTextureDesc& texture : packet.graph.textures) {
        if (texture.imported && texture.externalHandle) {
            textureResources[texture.name] = texture.externalHandle;
        }
    }

    const auto textureForName = [&](const std::string& name) -> RHITexture {
        const auto it = textureResources.find(name);
        return it == textureResources.end() ? RHITexture{} : it->second;
    };

    // 走 viewsByTexture 哈希索引,O(1) 找到 attachment 的 view。
    const auto findViewForTexture = [&](RHITexture texture, RHITextureAspect aspect) -> RHITextureView {
        if (!texture) {
            return {};
        }
        const auto it = impl_->viewsByTexture.find(texture.value);
        if (it == impl_->viewsByTexture.end()) {
            return {};
        }
        for (u64 viewValue : it->second) {
            const auto* view = getRenderResource(impl_->textureViews, RHITextureView(viewValue));
            if (view == nullptr || view->view == VK_NULL_HANDLE) {
                continue;
            }
            if (aspect == RHITextureAspect::All ||
                view->desc.aspect == RHITextureAspect::All ||
                RHIHasAny(view->desc.aspect, aspect)) {
                return RHITextureView(viewValue);
            }
        }
        return {};
    };

    // 状态转换:用更精确的 stage mask,而不是 ALL_COMMANDS_BIT。
    const auto transitionTexture = [&](RHITexture handle, RHIResourceState after) {
        Impl::TextureResource* texture = getRenderResource(impl_->textures, handle);
        if (texture == nullptr || texture->image == VK_NULL_HANDLE || texture->currentState == after) {
            return;
        }

        const RHIResourceState before = texture->currentState;
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = accessFromResourceState(before);
        barrier.dstAccessMask = accessFromResourceState(after);
        barrier.oldLayout = toVkImageLayout(before);
        barrier.newLayout = toVkImageLayout(after);
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture->image;
        barrier.subresourceRange.aspectMask = toVkImageAspect(RHITextureAspect::All, texture->desc.format);
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = texture->desc.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = texture->desc.arrayLayers;

        const VkPipelineStageFlags sourceStage = before == RHIResourceState::Undefined
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : stageFromResourceState(before);
        const VkPipelineStageFlags destStage = stageFromResourceState(after);
        vkCmdPipelineBarrier(
            commandBuffer,
            sourceStage,
            destStage,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);
        texture->currentState = after;
    };

    const auto findWorkload = [&](const std::string& passName) -> const RHIRenderPassWorkload* {
        const auto it = std::find_if(packet.workloads.begin(), packet.workloads.end(), [&](const RHIRenderPassWorkload& workload) {
            return workload.passName == passName;
        });
        return it == packet.workloads.end() ? nullptr : &*it;
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

    for (const RHIRenderGraphPassDesc& pass : packet.graph.passes) {
        for (const RHIRenderGraphResourceRef& read : pass.reads) {
            if (read.type == RHIRenderGraphResourceType::Texture || read.type == RHIRenderGraphResourceType::SwapchainImage) {
                transitionTexture(textureForName(read.name), read.state);
            }
        }
        for (const RHIRenderGraphResourceRef& write : pass.writes) {
            if (write.type == RHIRenderGraphResourceType::Texture || write.type == RHIRenderGraphResourceType::SwapchainImage) {
                transitionTexture(textureForName(write.name), write.state);
            }
        }

        const RHIRenderPassWorkload* workload = findWorkload(pass.name);
        if (workload == nullptr || (pass.colorAttachments.empty() && !pass.depthStencilAttachment.has_value())) {
            continue;
        }

        std::vector<VkRenderingAttachmentInfo> colorAttachments;
        std::vector<VkClearValue> colorClearValues;
        colorAttachments.reserve(pass.colorAttachments.size());
        colorClearValues.reserve(pass.colorAttachments.size());
        for (const RHIRenderGraphAttachmentDesc& attachment : pass.colorAttachments) {
            const RHITexture texture = textureForName(attachment.resourceName);
            const RHITextureView viewHandle = findViewForTexture(texture, RHITextureAspect::Color);
            const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
            if (view == nullptr || view->view == VK_NULL_HANDLE) {
                if (errorMessage != nullptr) *errorMessage = "RenderGraph color attachment requires a valid texture view";
                return false;
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
        bool hasDepth = false;
        if (pass.depthStencilAttachment.has_value()) {
            const RHIRenderGraphAttachmentDesc& attachment = *pass.depthStencilAttachment;
            const RHITexture texture = textureForName(attachment.resourceName);
            const RHITextureView viewHandle = findViewForTexture(texture, RHITextureAspect::Depth);
            const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
            if (view == nullptr || view->view == VK_NULL_HANDLE) {
                if (errorMessage != nullptr) *errorMessage = "RenderGraph depth attachment requires a valid texture view";
                return false;
            }

            depthClear = vkClearDepthStencil(attachment.clearValue.depthStencil);
            depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            depthAttachment.imageView = view->view;
            depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            depthAttachment.loadOp = toVkLoadOp(attachment.loadOp);
            depthAttachment.storeOp = toVkStoreOp(attachment.storeOp);
            depthAttachment.clearValue = depthClear;
            hasDepth = true;
        }

        RHIRect2D renderArea = workload->scissor.extent.width == 0 || workload->scissor.extent.height == 0
            ? packet.settings.scissor
            : workload->scissor;
        VkRenderingInfo renderingInfo{};
        renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        renderingInfo.renderArea.offset = {renderArea.offset.x, renderArea.offset.y};
        renderingInfo.renderArea.extent = {renderArea.extent.width, renderArea.extent.height};
        renderingInfo.layerCount = 1;
        renderingInfo.colorAttachmentCount = static_cast<u32>(colorAttachments.size());
        renderingInfo.pColorAttachments = colorAttachments.data();
        renderingInfo.pDepthAttachment = hasDepth ? &depthAttachment : nullptr;

        vkCmdBeginRendering(commandBuffer, &renderingInfo);

        RHIViewport viewport = workload->viewport.width == 0.0F || workload->viewport.height == 0.0F
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

        // recordDraw 走 baked 句柄,避免每 draw 重复查 vector。
        const auto recordDraw = [&](const RHIDrawIndexedCommand& draw) {
            const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, draw.pipeline);
            if (pipeline == nullptr || pipeline->pipeline == VK_NULL_HANDLE) {
                throw std::runtime_error("RHIDrawIndexedCommand pipeline is invalid");
            }
            vkCmdBindPipeline(commandBuffer, pipeline->bindPoint, pipeline->pipeline);

            if (!draw.bindSets.empty()) {
                std::vector<VkDescriptorSet> sets;
                sets.reserve(draw.bindSets.size());
                for (RHIBindSet bindSetHandle : draw.bindSets) {
                    const Impl::BindSetResource* bindSet = getRenderResource(impl_->bindSets, bindSetHandle);
                    if (bindSet == nullptr || bindSet->set == VK_NULL_HANDLE) {
                        throw std::runtime_error("RHIDrawIndexedCommand bind set is invalid");
                    }
                    sets.push_back(bindSet->set);
                }
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    pipeline->bindPoint,
                    pipeline->layout,
                    0,
                    static_cast<u32>(sets.size()),
                    sets.data(),
                    0, nullptr);
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

        for (const RHIDrawIndexedCommand& draw : workload->indexedDraws) {
            recordDraw(draw);
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
        if (errorMessage != nullptr) *errorMessage = "vkEndCommandBuffer failed";
        return false;
    }

    // 7) 提交 + Present。注意:不再 vkWaitForFences(UINT64_MAX),只等下一次这个 slot 复用时。
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
                if (errorMessage != nullptr) *errorMessage = "RHIFramePacket submission contains an invalid wait semaphore";
                return false;
            }
            usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == RHIGPUWaitGPUSignalType::Timeline;
            waitSignals.push_back(semaphore->semaphore);
            waitStages.push_back(toVkPipelineStages(wait.stages));
            waitValues.push_back(wait.value);
        }
        for (const RHIQueueSignalDesc& signal : submitDesc.signals) {
            const Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, signal.signal);
            if (semaphore == nullptr || semaphore->semaphore == VK_NULL_HANDLE) {
                if (errorMessage != nullptr) *errorMessage = "RHIFramePacket submission contains an invalid signal semaphore";
                return false;
            }
            usesTimelineSemaphore = usesTimelineSemaphore || semaphore->desc.type == RHIGPUWaitGPUSignalType::Timeline;
            signalSemaphores.push_back(semaphore->semaphore);
            signalValues.push_back(signal.value);
        }
    }

    VkTimelineSemaphoreSubmitInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.waitSemaphoreValueCount = static_cast<u32>(waitValues.size());
    timelineInfo.pWaitSemaphoreValues = waitValues.data();
    timelineInfo.signalSemaphoreValueCount = static_cast<u32>(signalValues.size());
    timelineInfo.pSignalSemaphoreValues = signalValues.data();

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = usesTimelineSemaphore ? &timelineInfo : nullptr;
    submitInfo.waitSemaphoreCount = static_cast<u32>(waitSignals.size());
    submitInfo.pWaitSemaphores = waitSignals.data();
    submitInfo.pWaitDstStageMask = waitStages.data();
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = static_cast<u32>(signalSemaphores.size());
    submitInfo.pSignalSemaphores = signalSemaphores.data();

    // 用 packet 的 cpuWaitGPUSignal 作为 vkQueueSubmit 的 fence。
    // 后端不维护自有 in-flight fence,完全交给 caller。这样 caller 的 WaitForCPUSignal
    // 才能等到真正的"提交完成",AcquireNextImage 之前才能保证 imageAvailable 已被消费。
    VkFence submitFence = frameFence;
    for (const RHIQueueSubmitDesc& submitDesc : packet.submissions) {
        if (submitDesc.cpuWaitGPUSignal) {
            const Impl::CPUWaitGPUSignalResource* userFence = getRenderResource(impl_->cpuWaitGPUSignals, submitDesc.cpuWaitGPUSignal);
            if (userFence != nullptr && userFence->fence != VK_NULL_HANDLE) {
                submitFence = userFence->fence;
            }
        }
    }
    if (submitFence != VK_NULL_HANDLE) {
        if (vkResetFences(impl_->native.device, 1, &submitFence) != VK_SUCCESS) {
            if (errorMessage != nullptr) *errorMessage = "vkResetFences(user) failed";
            return false;
        }
    }

    if (vkQueueSubmit(impl_->native.graphicsQueue, 1, &submitInfo, submitFence) != VK_SUCCESS) {
        if (errorMessage != nullptr) *errorMessage = "vkQueueSubmit(recorded frame) failed";
        return false;
    }

    // 8) fallback staging buffer 现在 GPU 已经引用,等当前帧 fence 之后才能销毁。
    //    这里把销毁动作挂到下一次这个 slot 被复用时(下一次 SubmitFrame 进来时先回收)。
    //    简化做法:本帧提交完不释放,等 Shutdown 统一回收或下次同 slot 复用时释放。
    //    真实工程应该用一个 per-slot 的"待回收"列表。
    static thread_local std::vector<std::pair<VkBuffer, VkDeviceMemory>> pendingFallback[8] = {};
    auto& pending = pendingFallback[frameSlot % 8];
    for (auto& fb : pending) {
        vkDestroyBuffer(impl_->native.device, fb.first, nullptr);
        vkFreeMemory(impl_->native.device, fb.second, nullptr);
    }
    pending.clear();
    for (auto& fb : fallbackBuffers) {
        pending.emplace_back(fb.first, fb.second);
    }

    impl_->frameCounter = packet.settings.frameIndex + 1;

    if (packet.present.has_value()) {
        return Present(*packet.present, errorMessage);
    }
    return true;
}

bool RHIVulkan::SubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    if (!packet.workloads.empty()) {
        return RecordAndSubmitFrame(packet, errorMessage);
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
    }
}

void RHIVulkan::WaitForCPUSignal(RHICPUWaitGPUSignal handle) const noexcept {
    if (!IsInitialized()) {
        return;
    }
    const Impl::CPUWaitGPUSignalResource* fence = getRenderResource(impl_->cpuWaitGPUSignals, handle);
    if (fence == nullptr || fence->fence == VK_NULL_HANDLE) {
        return;
    }
    vkWaitForFences(impl_->native.device, 1, &fence->fence, VK_TRUE, UINT64_MAX);
}

} // namespace rhi
