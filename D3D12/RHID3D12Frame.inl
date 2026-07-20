#pragma once

#include "RHID3D12Private.inl"

namespace rhi {

bool RHID3D12::RecordAndSubmitFrame(
    const RHIFramePacket& packet,
    const RHIRenderGraphExecutionPlan& graphPlan,
    std::string* errorMessage) {
    std::vector<ComPtr<ID3D12Resource>> stagingResources;
    std::vector<RHIBuffer> transientBuffers;
    std::vector<RHITexture> transientTextures;
    std::vector<RHITextureView> transientTextureViews;

    const auto releaseLocalTransients = [&]() noexcept {
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
    };

    try {
        if (!IsInitialized()) {
            throw std::runtime_error("RHID3D12 is not initialized");
        }

        // 单 allocator 版本必须先等上一份 command list 完成。这样 Reset、动态 descriptor
        // heap 复用和上一帧 transient/staging 回收都有明确的 fence 生命周期。
        if (impl_->fenceValue != 0 &&
            impl_->fence->GetCompletedValue() < impl_->fenceValue) {
            throwIfFailed(
                impl_->fence->SetEventOnCompletion(impl_->fenceValue, impl_->fenceEvent),
                "SetEventOnCompletion for D3D12 frame reuse failed");
            WaitForSingleObject(impl_->fenceEvent, INFINITE);
        }
        for (auto view = impl_->pendingTransientTextureViews.rbegin();
             view != impl_->pendingTransientTextureViews.rend();
             ++view) {
            Destroy(*view);
        }
        for (auto texture = impl_->pendingTransientTextures.rbegin();
             texture != impl_->pendingTransientTextures.rend();
             ++texture) {
            Destroy(*texture);
        }
        for (auto buffer = impl_->pendingTransientBuffers.rbegin();
             buffer != impl_->pendingTransientBuffers.rend();
             ++buffer) {
            Destroy(*buffer);
        }
        impl_->pendingTransientTextureViews.clear();
        impl_->pendingTransientTextures.clear();
        impl_->pendingTransientBuffers.clear();
        impl_->pendingStagingResources.clear();
        impl_->resetShaderVisibleDescriptors();

        throwIfFailed(
            impl_->commandAllocator->Reset(),
            "ID3D12CommandAllocator::Reset failed");
        throwIfFailed(
            impl_->commandList->Reset(impl_->commandAllocator.Get(), nullptr),
            "ID3D12GraphicsCommandList::Reset failed");
        impl_->commandListOpen = true;

        ID3D12DescriptorHeap* shaderVisibleHeaps[] = {
            impl_->shaderVisibleCbvSrvUavHeap.heap.Get(),
            impl_->shaderVisibleSamplerHeap.heap.Get()};
        impl_->commandList->SetDescriptorHeaps(
            static_cast<UINT>(std::size(shaderVisibleHeaps)),
            shaderVisibleHeaps);

        const auto transitionResource = [&] (
            ID3D12Resource* resource,
            D3D12_RESOURCE_STATES& currentState,
            RHIResourceState requestedState) {
            if (resource == nullptr) {
                throw std::runtime_error("RenderGraph D3D12 transition has a null resource");
            }
            const D3D12_RESOURCE_STATES destination =
                toD3D12ResourceStates(requestedState);
            if (currentState == destination) {
                if (destination == D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
                    D3D12_RESOURCE_BARRIER barrier{};
                    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
                    barrier.UAV.pResource = resource;
                    impl_->commandList->ResourceBarrier(1, &barrier);
                }
                return;
            }

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource;
            barrier.Transition.StateBefore = currentState;
            barrier.Transition.StateAfter = destination;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            impl_->commandList->ResourceBarrier(1, &barrier);
            currentState = destination;
        };

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

        for (const RHIBufferUploadDesc& upload : packet.uploads.buffers) {
            if (upload.data.empty()) {
                continue;
            }
            Impl::BufferResource* buffer = getRenderResource(impl_->buffers, upload.destination);
            if (buffer == nullptr || !buffer->resource) {
                throw std::runtime_error("RHIFramePacket buffer upload destination is invalid");
            }
            if (upload.destinationOffset > buffer->desc.size ||
                upload.data.size() > buffer->desc.size - upload.destinationOffset) {
                throw std::runtime_error("RHIFramePacket buffer upload range exceeds destination buffer size");
            }
            if (buffer->mappedData != nullptr) {
                std::memcpy(
                    static_cast<std::byte*>(buffer->mappedData) + upload.destinationOffset,
                    upload.data.data(),
                    upload.data.size());
                continue;
            }

            D3D12_HEAP_PROPERTIES uploadHeap{};
            uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
            uploadHeap.CreationNodeMask = 1;
            uploadHeap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC uploadDesc{};
            uploadDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            uploadDesc.Width = upload.data.size();
            uploadDesc.Height = 1;
            uploadDesc.DepthOrArraySize = 1;
            uploadDesc.MipLevels = 1;
            uploadDesc.SampleDesc.Count = 1;
            uploadDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            ComPtr<ID3D12Resource> staging;
            throwIfFailed(
                impl_->device->CreateCommittedResource(
                    &uploadHeap,
                    D3D12_HEAP_FLAG_NONE,
                    &uploadDesc,
                    D3D12_RESOURCE_STATE_GENERIC_READ,
                    nullptr,
                    IID_PPV_ARGS(&staging)),
                "CreateCommittedResource for D3D12 upload staging failed");
            void* mapped = nullptr;
            throwIfFailed(staging->Map(0, nullptr, &mapped), "Map D3D12 upload staging failed");
            std::memcpy(mapped, upload.data.data(), upload.data.size());
            staging->Unmap(0, nullptr);

            transitionResource(
                buffer->resource.Get(),
                buffer->currentState,
                RHIResourceState::CopyDestination);
            impl_->commandList->CopyBufferRegion(
                buffer->resource.Get(),
                upload.destinationOffset,
                staging.Get(),
                0,
                upload.data.size());
            stagingResources.push_back(std::move(staging));
        }

        if (!packet.uploads.textures.empty()) {
            throw std::runtime_error("D3D12 texture upload staging is not implemented yet");
        }

        const auto findViewForTexture = [&](RHITexture texture, RHITextureAspect aspect)
            -> Impl::TextureViewResource* {
            for (Impl::TextureViewResource& view : impl_->textureViews) {
                if (view.desc.texture == texture &&
                    (aspect == RHITextureAspect::All ||
                     view.desc.aspect == RHITextureAspect::All ||
                     RHIHasAny(view.desc.aspect, aspect))) {
                    return &view;
                }
            }
            return nullptr;
        };

        const auto bindSets = [&] (
            const std::vector<RHIBindSet>& bindSetHandles,
            bool compute) {
            UINT rootParameter = 0;
            for (RHIBindSet bindSetHandle : bindSetHandles) {
                const Impl::BindSetResource* bindSet =
                    getRenderResource(impl_->bindSets, bindSetHandle);
                if (bindSet == nullptr) {
                    throw std::runtime_error("D3D12 draw/dispatch bind set is invalid");
                }
                const Impl::BindSetLayoutResource* layout =
                    getRenderResource(impl_->bindSetLayouts, bindSet->desc.layout);
                if (layout == nullptr) {
                    throw std::runtime_error("D3D12 bind set layout is invalid");
                }
                for (const RHIBindSetLayoutEntry& entry : layout->desc.entries) {
                    if (entry.type == RHIBindingType::PushConstant) {
                        continue;
                    }
                    const auto resolved = std::find_if(
                        bindSet->bindings.begin(),
                        bindSet->bindings.end(),
                        [&](const Impl::ResolvedBinding& binding) {
                            return binding.slot == entry.binding;
                        });
                    if (resolved == bindSet->bindings.end()) {
                        throw std::runtime_error(
                            "D3D12 bind set is missing a declared layout binding");
                    }
                    if (resolved->resourceDescriptor.valid) {
                        const D3D12_GPU_DESCRIPTOR_HANDLE descriptor =
                            impl_->copyToShaderVisible(resolved->resourceDescriptor);
                        if (compute) {
                            impl_->commandList->SetComputeRootDescriptorTable(
                                rootParameter++, descriptor);
                        } else {
                            impl_->commandList->SetGraphicsRootDescriptorTable(
                                rootParameter++, descriptor);
                        }
                    }
                    if (resolved->samplerDescriptor.valid) {
                        const D3D12_GPU_DESCRIPTOR_HANDLE descriptor =
                            impl_->copyToShaderVisible(resolved->samplerDescriptor);
                        if (compute) {
                            impl_->commandList->SetComputeRootDescriptorTable(
                                rootParameter++, descriptor);
                        } else {
                            impl_->commandList->SetGraphicsRootDescriptorTable(
                                rootParameter++, descriptor);
                        }
                    }
                }
            }
        };

        const auto bindVertexStreams = [&](const std::vector<RHIVertexStream>& streams) {
            for (const RHIVertexStream& stream : streams) {
                const Impl::BufferResource* buffer =
                    getRenderResource(impl_->buffers, stream.buffer);
                if (buffer == nullptr || !buffer->resource) {
                    throw std::runtime_error("D3D12 vertex stream buffer is invalid");
                }
                D3D12_VERTEX_BUFFER_VIEW view{};
                view.BufferLocation = buffer->resource->GetGPUVirtualAddress() + stream.offset;
                view.SizeInBytes = static_cast<UINT>(buffer->desc.size - stream.offset);
                view.StrideInBytes = static_cast<UINT>(stream.stride);
                impl_->commandList->IASetVertexBuffers(stream.binding, 1, &view);
            }
        };

        for (const RHICompiledRenderGraphPass& compiledPass : graphPlan.passes) {
            const RHIRenderGraphPassDesc& sourcePass =
                packet.graph.passes[compiledPass.sourcePassIndex];
            for (const RHIRenderGraphTransition& transition : compiledPass.transitions) {
                if (transition.resource.IsBuffer()) {
                    Impl::BufferResource* buffer = getRenderResource(
                        impl_->buffers,
                        graphBuffers[transition.resource.index]);
                    if (buffer == nullptr || !buffer->resource) {
                        throw std::runtime_error("RenderGraph D3D12 buffer is invalid");
                    }
                    // UPLOAD/READBACK heap 的 native state 受 D3D12 固定约束，不能像 DEFAULT
                    // heap 一样切换；它们的逻辑用途仍由 RenderGraph dependency 约束。
                    if (buffer->desc.memoryUsage == RHIMemoryUsage::GpuOnly) {
                        transitionResource(
                            buffer->resource.Get(),
                            buffer->currentState,
                            transition.after);
                    }
                } else {
                    Impl::TextureResource* texture = getRenderResource(
                        impl_->textures,
                        graphTextures[transition.resource.index]);
                    if (texture == nullptr || !texture->resource) {
                        throw std::runtime_error("RenderGraph D3D12 texture is invalid");
                    }
                    transitionResource(
                        texture->resource.Get(),
                        texture->currentState,
                        transition.after);
                }
            }

            const RHIRenderPassWorkload* workload =
                compiledPass.workloadIndex == RHI_INVALID_INDEX
                    ? nullptr
                    : &packet.workloads[compiledPass.workloadIndex];

            if (workload != nullptr) {
                if (!workload->barriers.globals.empty() ||
                    !workload->barriers.textures.empty() ||
                    !workload->barriers.buffers.empty()) {
                    throw std::runtime_error(
                        "Explicit workload barriers are not supported; declare RenderGraph reads/writes instead");
                }
                if (!workload->queryResets.empty() ||
                    !workload->timestampWrites.empty() ||
                    !workload->queryResolves.empty()) {
                    throw std::runtime_error(
                        "D3D12 RenderGraph query workloads are not implemented yet");
                }
                if (!workload->indirectDraws.empty() ||
                    !workload->indexedIndirectDraws.empty() ||
                    !workload->indirectDispatches.empty()) {
                    throw std::runtime_error(
                        "D3D12 RenderGraph indirect workloads are not implemented yet");
                }
                for (const RHIBufferCopyDesc& copy : workload->bufferCopies) {
                    const Impl::BufferResource* source =
                        getRenderResource(impl_->buffers, copy.source);
                    const Impl::BufferResource* destination =
                        getRenderResource(impl_->buffers, copy.destination);
                    if (source == nullptr || destination == nullptr ||
                        !source->resource || !destination->resource) {
                        throw std::runtime_error("D3D12 buffer copy resource is invalid");
                    }
                    if (copy.size == 0 ||
                        copy.sourceOffset > source->desc.size ||
                        copy.size > source->desc.size - copy.sourceOffset ||
                        copy.destinationOffset > destination->desc.size ||
                        copy.size >
                            destination->desc.size - copy.destinationOffset) {
                        throw std::runtime_error(
                            "D3D12 RenderGraph buffer copy range is invalid");
                    }
                    impl_->commandList->CopyBufferRegion(
                        destination->resource.Get(),
                        copy.destinationOffset,
                        source->resource.Get(),
                        copy.sourceOffset,
                        copy.size);
                }
                if (!workload->textureCopies.empty() ||
                    !workload->bufferToTextureCopies.empty() ||
                    !workload->textureToBufferCopies.empty() ||
                    !workload->textureBlits.empty() ||
                    !workload->mipmapGenerations.empty()) {
                    throw std::runtime_error(
                        "D3D12 advanced texture copy/blit workloads are not implemented yet");
                }
                if (sourcePass.type == RHIRenderGraphPassType::Copy) {
                    continue;
                }
            }

            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> renderTargets;
            renderTargets.reserve(compiledPass.colorAttachments.size());
            for (const RHICompiledRenderGraphAttachment& attachment :
                 compiledPass.colorAttachments) {
                Impl::TextureViewResource* view = findViewForTexture(
                    graphTextures[attachment.textureIndex],
                    RHITextureAspect::Color);
                if (view == nullptr || !view->rtv.valid) {
                    throw std::runtime_error("RenderGraph color attachment has no D3D12 RTV");
                }
                renderTargets.push_back(view->rtv.handle);
                const RHIRenderGraphAttachmentDesc& attachmentDesc =
                    sourcePass.colorAttachments[attachment.attachmentIndex];
                if (attachmentDesc.loadOp == RHILoadOp::Clear) {
                    const float clear[] = {
                        attachmentDesc.clearValue.color.r,
                        attachmentDesc.clearValue.color.g,
                        attachmentDesc.clearValue.color.b,
                        attachmentDesc.clearValue.color.a};
                    impl_->commandList->ClearRenderTargetView(
                        view->rtv.handle, clear, 0, nullptr);
                }
            }

            D3D12_CPU_DESCRIPTOR_HANDLE depthHandle{};
            const D3D12_CPU_DESCRIPTOR_HANDLE* depthHandlePointer = nullptr;
            if (compiledPass.depthStencilAttachment.has_value()) {
                const RHICompiledRenderGraphAttachment& attachment =
                    *compiledPass.depthStencilAttachment;
                Impl::TextureViewResource* view = findViewForTexture(
                    graphTextures[attachment.textureIndex],
                    RHITextureAspect::Depth);
                if (view == nullptr || !view->dsv.valid) {
                    throw std::runtime_error("RenderGraph depth attachment has no D3D12 DSV");
                }
                depthHandle = view->dsv.handle;
                depthHandlePointer = &depthHandle;
                const RHIRenderGraphAttachmentDesc& attachmentDesc =
                    *sourcePass.depthStencilAttachment;
                if (attachmentDesc.loadOp == RHILoadOp::Clear) {
                    impl_->commandList->ClearDepthStencilView(
                        depthHandle,
                        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                        attachmentDesc.clearValue.depthStencil.depth,
                        static_cast<UINT8>(attachmentDesc.clearValue.depthStencil.stencil),
                        0,
                        nullptr);
                }
            }
            if (!renderTargets.empty() || depthHandlePointer != nullptr) {
                impl_->commandList->OMSetRenderTargets(
                    static_cast<UINT>(renderTargets.size()),
                    renderTargets.empty() ? nullptr : renderTargets.data(),
                    FALSE,
                    depthHandlePointer);
            }

            if (workload == nullptr) {
                continue;
            }
            const RHIViewport viewport = workload->viewport.width == 0.0F ||
                                                 workload->viewport.height == 0.0F
                                             ? packet.settings.viewport
                                             : workload->viewport;
            D3D12_VIEWPORT nativeViewport{
                viewport.x,
                viewport.y,
                viewport.width,
                viewport.height,
                viewport.minDepth,
                viewport.maxDepth};
            impl_->commandList->RSSetViewports(1, &nativeViewport);
            const RHIRect2D scissor = workload->scissor.extent.width == 0 ||
                                              workload->scissor.extent.height == 0
                                          ? packet.settings.scissor
                                          : workload->scissor;
            D3D12_RECT nativeScissor{
                scissor.offset.x,
                scissor.offset.y,
                scissor.offset.x + static_cast<LONG>(scissor.extent.width),
                scissor.offset.y + static_cast<LONG>(scissor.extent.height)};
            impl_->commandList->RSSetScissorRects(1, &nativeScissor);

            for (const RHIDrawCommand& draw : workload->draws) {
                const Impl::PipelineResource* pipeline =
                    getRenderResource(impl_->pipelines, draw.pipeline);
                if (pipeline == nullptr || pipeline->compute || !pipeline->pipelineState) {
                    throw std::runtime_error("RHIDrawCommand D3D12 pipeline is invalid");
                }
                impl_->commandList->SetPipelineState(pipeline->pipelineState.Get());
                impl_->commandList->SetGraphicsRootSignature(pipeline->rootSignature.Get());
                impl_->commandList->IASetPrimitiveTopology(pipeline->topology);
                bindSets(draw.bindSets, false);
                bindVertexStreams(draw.vertexStreams);
                impl_->commandList->DrawInstanced(
                    draw.vertexCount,
                    draw.instanceCount,
                    draw.firstVertex,
                    draw.firstInstance);
            }
            for (const RHIDrawIndexedCommand& draw : workload->indexedDraws) {
                const Impl::PipelineResource* pipeline =
                    getRenderResource(impl_->pipelines, draw.pipeline);
                if (pipeline == nullptr || pipeline->compute || !pipeline->pipelineState) {
                    throw std::runtime_error(
                        "RHIDrawIndexedCommand D3D12 pipeline is invalid");
                }
                const Impl::BufferResource* indexBuffer =
                    getRenderResource(impl_->buffers, draw.indexStream.buffer);
                if (indexBuffer == nullptr || !indexBuffer->resource) {
                    throw std::runtime_error("D3D12 index buffer is invalid");
                }
                impl_->commandList->SetPipelineState(pipeline->pipelineState.Get());
                impl_->commandList->SetGraphicsRootSignature(pipeline->rootSignature.Get());
                impl_->commandList->IASetPrimitiveTopology(pipeline->topology);
                bindSets(draw.bindSets, false);
                bindVertexStreams(draw.vertexStreams);
                D3D12_INDEX_BUFFER_VIEW indexView{};
                indexView.BufferLocation =
                    indexBuffer->resource->GetGPUVirtualAddress() + draw.indexStream.offset;
                indexView.SizeInBytes =
                    static_cast<UINT>(indexBuffer->desc.size - draw.indexStream.offset);
                indexView.Format = draw.indexStream.indexType == RHIIndexType::UInt16
                                       ? DXGI_FORMAT_R16_UINT
                                       : DXGI_FORMAT_R32_UINT;
                impl_->commandList->IASetIndexBuffer(&indexView);
                impl_->commandList->DrawIndexedInstanced(
                    draw.indexCount,
                    draw.instanceCount,
                    draw.firstIndex,
                    draw.vertexOffsetElements,
                    draw.firstInstance);
            }
            for (const RHIDispatchCommand& dispatch : workload->dispatches) {
                const Impl::PipelineResource* pipeline =
                    getRenderResource(impl_->pipelines, dispatch.pipeline);
                if (pipeline == nullptr || !pipeline->compute || !pipeline->pipelineState) {
                    throw std::runtime_error("RHIDispatchCommand D3D12 pipeline is invalid");
                }
                impl_->commandList->SetPipelineState(pipeline->pipelineState.Get());
                impl_->commandList->SetComputeRootSignature(pipeline->rootSignature.Get());
                bindSets(dispatch.bindSets, true);
                impl_->commandList->Dispatch(
                    dispatch.groupCountX,
                    dispatch.groupCountY,
                    dispatch.groupCountZ);
            }
        }

        if (packet.present.has_value()) {
            Impl::SwapchainResource* swapchain =
                getRenderResource(impl_->swapchains, packet.present->swapchain);
            if (swapchain != nullptr && packet.present->imageIndex < swapchain->images.size()) {
                Impl::TextureResource* image = getRenderResource(
                    impl_->textures,
                    swapchain->images[packet.present->imageIndex]);
                if (image != nullptr && image->resource) {
                    transitionResource(
                        image->resource.Get(),
                        image->currentState,
                        RHIResourceState::Present);
                }
            }
        }

        if (packet.submissions.empty()) {
            if (!Submit(RHIQueueSubmitDesc{}, errorMessage)) {
                throw std::runtime_error(
                    errorMessage != nullptr && !errorMessage->empty()
                        ? *errorMessage
                        : "D3D12 RenderGraph submission failed");
            }
        } else {
            for (const RHIQueueSubmitDesc& submitDesc : packet.submissions) {
                if (!Submit(submitDesc, errorMessage)) {
                    throw std::runtime_error(
                        errorMessage != nullptr && !errorMessage->empty()
                            ? *errorMessage
                            : "D3D12 RenderGraph submission failed");
                }
            }
        }

        impl_->pendingStagingResources = std::move(stagingResources);
        impl_->pendingTransientBuffers = std::move(transientBuffers);
        impl_->pendingTransientTextures = std::move(transientTextures);
        impl_->pendingTransientTextureViews = std::move(transientTextureViews);

        if (packet.present.has_value()) {
            return Present(*packet.present, errorMessage);
        }
        return true;
    } catch (const std::exception& error) {
        if (impl_->commandListOpen) {
            (void)impl_->commandList->Close();
            impl_->commandListOpen = false;
        }
        releaseLocalTransients();
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool RHID3D12::SubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
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

bool RHID3D12::SubmitFrame(
    const RHIFramePacket& packet,
    const RHIRenderGraphExecutionPlan& graphPlan,
    std::string* errorMessage) {
    if (!ValidateRHIRenderGraphSubmissions(
            packet.graph,
            graphPlan,
            packet.submissions,
            errorMessage)) {
        return false;
    }
    const bool hasUploads = !packet.uploads.buffers.empty() || !packet.uploads.textures.empty();
    if (hasUploads || !graphPlan.passes.empty()) {
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

void RHID3D12::WaitIdle() const noexcept {
    if (!IsInitialized() || impl_->fence == nullptr || impl_->fenceEvent == nullptr) {
        return;
    }

    const UINT64 waitValue = impl_->fenceValue + 1;
    if (FAILED(impl_->graphicsQueue->Signal(impl_->fence.Get(), waitValue))) {
        return;
    }
    impl_->fenceValue = waitValue;
    impl_->native.fenceValue = impl_->fenceValue;

    if (impl_->fence->GetCompletedValue() < waitValue) {
        if (SUCCEEDED(impl_->fence->SetEventOnCompletion(waitValue, impl_->fenceEvent))) {
            WaitForSingleObject(impl_->fenceEvent, INFINITE);
        }
    }
}

// D3D12 frame 片段目前是“可扩展入口”而不是完整渲染器：
// - 已经可以处理 CPU 可见 buffer 上传、Submit、Present 和 WaitIdle；
// - 还没有把 RenderGraph pass 录制成 command list；
// - 下一步要补的是 resource barrier、RTV/DSV 绑定、descriptor heap 拷贝、root table 设置、Draw/Dispatch。

} // namespace rhi






