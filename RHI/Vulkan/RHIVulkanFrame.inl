#pragma once

#include "RHIVulkanPrivate.inl"

namespace rhi {

bool RHIVulkan::recordAndSubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    struct StagingResource {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
    };

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence submitFence = VK_NULL_HANDLE;
    std::vector<StagingResource> stagingResources;

    const auto cleanup = [&]() noexcept {
        if (submitFence != VK_NULL_HANDLE) {
            vkDestroyFence(impl_->native.device, submitFence, nullptr);
        }
        if (commandBuffer != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(impl_->native.device, impl_->graphicsCommandPool, 1, &commandBuffer);
        }
        for (const StagingResource& staging : stagingResources) {
            if (staging.buffer != VK_NULL_HANDLE) {
                vkDestroyBuffer(impl_->native.device, staging.buffer, nullptr);
            }
            if (staging.memory != VK_NULL_HANDLE) {
                vkFreeMemory(impl_->native.device, staging.memory, nullptr);
            }
        }
    };

    try {
        if (!isInitialized() || impl_->graphicsCommandPool == VK_NULL_HANDLE) {
            throw std::runtime_error("RHIVulkan is not initialized or has no graphics command pool");
        }

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = impl_->graphicsCommandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(impl_->native.device, &allocateInfo, &commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateCommandBuffers failed");
        }

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
            return access == 0 ? VK_ACCESS_MEMORY_READ_BIT : access;
        };

        std::vector<VkBufferMemoryBarrier> uploadBarriers;
        for (const RHIBufferUploadDesc& upload : packet.uploads.buffers) {
            if (upload.data.empty()) {
                continue;
            }

            Impl::BufferResource* destination = getRenderResource(impl_->buffers, upload.destination);
            if (destination == nullptr || destination->buffer == VK_NULL_HANDLE) {
                throw std::runtime_error("RHIFramePacket uploads contain an invalid destination buffer");
            }

            StagingResource staging{};
            VkBufferCreateInfo stagingInfo{};
            stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            stagingInfo.size = static_cast<VkDeviceSize>(upload.data.size());
            stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(impl_->native.device, &stagingInfo, nullptr, &staging.buffer) != VK_SUCCESS) {
                throw std::runtime_error("vkCreateBuffer(staging) failed");
            }

            // buffer 创建成功后立即登记所有权，后续 memory allocation 失败时也能由 cleanup 回收。
            stagingResources.push_back(staging);
            StagingResource& trackedStaging = stagingResources.back();

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
        }

        if (!uploadBarriers.empty()) {
            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0,
                0,
                nullptr,
                static_cast<u32>(uploadBarriers.size()),
                uploadBarriers.data(),
                0,
                nullptr);
        }

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

        const auto findViewForTexture = [&](RHITexture texture, RHITextureAspect aspect) -> RHITextureView {
            for (u64 index = 0; index < impl_->textureViews.size(); ++index) {
                const Impl::TextureViewResource& view = impl_->textureViews[static_cast<size_t>(index)];
                if (view.view != VK_NULL_HANDLE && view.desc.texture == texture &&
                    (aspect == RHITextureAspect::All || view.desc.aspect == aspect || view.desc.aspect == RHITextureAspect::All)) {
                    return RHITextureView(index + 1);
                }
            }
            return {};
        };

        const auto transitionTexture = [&](RHITexture handle, RHIResourceState after) {
            // RenderGraph 只描述 pass 读写需要的 RHIResourceState；Vulkan 需要实际 image layout
            // 和 access mask，所以这里根据当前状态生成 VkImageMemoryBarrier。
            Impl::TextureResource* texture = getRenderResource(impl_->textures, handle);
            if (texture == nullptr || texture->image == VK_NULL_HANDLE || texture->currentState == after) {
                return;
            }

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = accessFromResourceState(texture->currentState);
            barrier.dstAccessMask = accessFromResourceState(after);
            barrier.oldLayout = toVkImageLayout(texture->currentState);
            barrier.newLayout = toVkImageLayout(after);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = texture->image;
            barrier.subresourceRange.aspectMask = toVkImageAspect(RHITextureAspect::All, texture->desc.format);
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = texture->desc.mipLevels;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = texture->desc.arrayLayers;

            const VkPipelineStageFlags sourceStage =
                texture->currentState == RHIResourceState::Undefined ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
            vkCmdPipelineBarrier(
                commandBuffer,
                sourceStage,
                VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                0,
                0,
                nullptr,
                0,
                nullptr,
                1,
                &barrier);
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
            // pass 的 reads/writes 先变成 layout transition，再根据 workload 录制具体 draw。
            // 这样“资源生命周期/状态”和“画什么”保持分离，便于之后扩展自动依赖分析。
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
            if (pass.depthStencilAttachment.has_value()) {
                const RHIRenderGraphAttachmentDesc& attachment = *pass.depthStencilAttachment;
                const RHITexture texture = textureForName(attachment.resourceName);
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
            renderingInfo.pDepthAttachment = pass.depthStencilAttachment.has_value() ? &depthAttachment : nullptr;

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

            const auto recordDraw = [&](const RHIDrawIndexedCommand& draw) {
                // 一个 draw 的最小 Vulkan 绑定集合：pipeline、descriptor sets、vertex buffers、
                // index buffer，然后发出 vkCmdDrawIndexed。
                const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, draw.pipeline);
                if (pipeline == nullptr || pipeline->pipeline == VK_NULL_HANDLE) {
                    throw std::runtime_error("RHIDrawIndexedCommand pipeline is invalid");
                }
                vkCmdBindPipeline(commandBuffer, pipeline->bindPoint, pipeline->pipeline);

                std::vector<VkDescriptorSet> descriptorSets;
                descriptorSets.reserve(draw.bindGroups.size());
                for (RHIBindGroup bindGroupHandle : draw.bindGroups) {
                    const Impl::BindGroupResource* bindGroup = getRenderResource(impl_->bindGroups, bindGroupHandle);
                    if (bindGroup == nullptr || bindGroup->set == VK_NULL_HANDLE) {
                        throw std::runtime_error("RHIDrawIndexedCommand bind group is invalid");
                    }
                    descriptorSets.push_back(bindGroup->set);
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
            throw std::runtime_error("vkEndCommandBuffer failed");
        }

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

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(impl_->native.device, &fenceInfo, nullptr, &submitFence) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateFence(submit) failed");
        }

        if (vkQueueSubmit(impl_->native.graphicsQueue, 1, &submitInfo, submitFence) != VK_SUCCESS) {
            throw std::runtime_error("vkQueueSubmit(recorded frame) failed");
        }
        const VkResult waitResult = vkWaitForFences(
            impl_->native.device, 1, &submitFence, VK_TRUE, std::numeric_limits<u64>::max());
        if (waitResult != VK_SUCCESS) {
            throw std::runtime_error("vkWaitForFences(recorded frame) failed");
        }
        cleanup();
        commandBuffer = VK_NULL_HANDLE;
        submitFence = VK_NULL_HANDLE;
        stagingResources.clear();

        if (packet.present.has_value()) {
            return present(*packet.present, errorMessage);
        }
        return true;
    } catch (const std::exception& error) {
        cleanup();
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool RHIVulkan::submitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    // RHIFramePacket 有 workload 时走“录制并提交”的路径；没有 workload 时只执行用户提供的
    // RHIQueueSubmitDesc/RHIPresentDesc，方便外部系统自己管理 command buffer。
    if (!packet.workloads.empty()) {
        return recordAndSubmitFrame(packet, errorMessage);
    }

    for (const RHIQueueSubmitDesc& submitDesc : packet.submissions) {
        if (!submit(submitDesc, errorMessage)) {
            return false;
        }
    }
    if (packet.present.has_value()) {
        return present(*packet.present, errorMessage);
    }
    return true;
}

void RHIVulkan::waitIdle() const noexcept {
    if (isInitialized()) {
        vkDeviceWaitIdle(impl_->native.device);
    }
}

// destroy 系列只释放 native 对象并清空句柄槽里的内容，不压缩 vector。

} // namespace rhi





