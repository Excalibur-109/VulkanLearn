#pragma once

#include "RHID3D11Private.inl"

namespace rhi {

static bool stageVisible(RHIShaderStage visibility, RHIShaderStage stage) {
    if (visibility == RHIShaderStage::All) {
        return true;
    }
    if (visibility == RHIShaderStage::AllGraphics) {
        return stage == RHIShaderStage::Vertex ||
               stage == RHIShaderStage::TessControl ||
               stage == RHIShaderStage::TessEvaluation ||
               stage == RHIShaderStage::Geometry ||
               stage == RHIShaderStage::Fragment;
    }
    return RHIHasAny(visibility, stage);
}

// D3D11 每个 shader stage 都有独立的 constant buffer/SRV/sampler slot。
// 统一的 RHIShaderStage visibility 会在这里展开成 VS/HS/DS/GS/PS/CS 对应的 Set* 调用。
static void setConstantBufferForStages(ID3D11DeviceContext* context, RHIShaderStage visibility, UINT slot, ID3D11Buffer* buffer) {
    if (stageVisible(visibility, RHIShaderStage::Vertex))         context->VSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, RHIShaderStage::TessControl))    context->HSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, RHIShaderStage::TessEvaluation)) context->DSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, RHIShaderStage::Geometry))       context->GSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, RHIShaderStage::Fragment))       context->PSSetConstantBuffers(slot, 1, &buffer);
    if (stageVisible(visibility, RHIShaderStage::Compute))        context->CSSetConstantBuffers(slot, 1, &buffer);
}

static void setSrvForStages(ID3D11DeviceContext* context, RHIShaderStage visibility, UINT slot, ID3D11ShaderResourceView* srv) {
    if (stageVisible(visibility, RHIShaderStage::Vertex))         context->VSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, RHIShaderStage::TessControl))    context->HSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, RHIShaderStage::TessEvaluation)) context->DSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, RHIShaderStage::Geometry))       context->GSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, RHIShaderStage::Fragment))       context->PSSetShaderResources(slot, 1, &srv);
    if (stageVisible(visibility, RHIShaderStage::Compute))        context->CSSetShaderResources(slot, 1, &srv);
}

static void setSamplerForStages(ID3D11DeviceContext* context, RHIShaderStage visibility, UINT slot, ID3D11SamplerState* sampler) {
    if (stageVisible(visibility, RHIShaderStage::Vertex))         context->VSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, RHIShaderStage::TessControl))    context->HSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, RHIShaderStage::TessEvaluation)) context->DSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, RHIShaderStage::Geometry))       context->GSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, RHIShaderStage::Fragment))       context->PSSetSamplers(slot, 1, &sampler);
    if (stageVisible(visibility, RHIShaderStage::Compute))        context->CSSetSamplers(slot, 1, &sampler);
}

// 把 BindSet 中预解析好的资源真正绑定到 context。
// 图形路径通常绑定 CBV/SRV/Sampler；compute 路径还允许把 StorageTexture 的 UAV 绑定到 CS。
template <typename BindSetResourceT>
static void applyBindSet(ID3D11DeviceContext* context, const BindSetResourceT& bindSet, bool compute) {
    for (const auto& binding : bindSet.bindings) {
        ID3D11Buffer* buffer = binding.buffer.Get();
        ID3D11ShaderResourceView* srv = binding.srv.Get();
        ID3D11SamplerState* sampler = binding.sampler.Get();
        ID3D11UnorderedAccessView* uav = binding.uav.Get();

        switch (binding.type) {
        case RHIBindingType::UniformBuffer:
            setConstantBufferForStages(context, binding.visibility, binding.slot, buffer);
            break;
        case RHIBindingType::StorageBuffer:
        case RHIBindingType::SampledTexture:
        case RHIBindingType::CombinedTextureSampler:
            if (srv != nullptr) {
                setSrvForStages(context, binding.visibility, binding.slot, srv);
            }
            if (sampler != nullptr) {
                setSamplerForStages(context, binding.visibility, binding.slot, sampler);
            }
            break;
        case RHIBindingType::StorageTexture:
            if (compute && uav != nullptr) {
                UINT initialCount = 0;
                context->CSSetUnorderedAccessViews(binding.slot, 1, &uav, &initialCount);
            } else if (srv != nullptr) {
                setSrvForStages(context, binding.visibility, binding.slot, srv);
            }
            break;
        case RHIBindingType::Sampler:
            if (sampler != nullptr) {
                setSamplerForStages(context, binding.visibility, binding.slot, sampler);
            }
            break;
        default:
            break;
        }
    }
}

// D3D11 pipeline 是一组 context 状态。每次 draw/dispatch 前调用它，确保当前 context 处于
// 这个 RHIPipeline 描述的状态；后续 draw 会继承这些状态，直到再次被覆盖。
template <typename PipelineResourceT>
static void applyPipeline(ID3D11DeviceContext* context, const PipelineResourceT& pipeline) {
    if (pipeline.compute) {
        context->CSSetShader(pipeline.computeShader.Get(), nullptr, 0);
        return;
    }

    context->IASetPrimitiveTopology(pipeline.topology);
    context->IASetInputLayout(pipeline.inputLayout.Get());
    context->VSSetShader(pipeline.vertexShader.Get(), nullptr, 0);
    context->HSSetShader(pipeline.hullShader.Get(), nullptr, 0);
    context->DSSetShader(pipeline.domainShader.Get(), nullptr, 0);
    context->GSSetShader(pipeline.geometryShader.Get(), nullptr, 0);
    context->PSSetShader(pipeline.pixelShader.Get(), nullptr, 0);
    context->RSSetState(pipeline.rasterizerState.Get());
    context->OMSetDepthStencilState(pipeline.depthStencilState.Get(), pipeline.stencilRef);
    context->OMSetBlendState(pipeline.blendState.Get(), pipeline.blendConstants.data(), pipeline.sampleMask);
}

bool RHID3D11::AcquireNextImage(
    RHISwapchain swapchain,
    RHIGPUWaitGPUSignal gpuWaitGPUSignal,
    RHICPUWaitGPUSignal cpuWaitGPUSignal,
    u32* imageIndex,
    std::string* errorMessage) {
    try {
        // D3D11 swapchain 后端当前只包装一个 back buffer，所以 imageIndex 固定为 0。
        // semaphore/fence 在这里标记为已 signal，用来保持和 Vulkan 调用流程一致。
        if (getRenderResource(impl_->swapchains, swapchain) == nullptr) {
            throw std::runtime_error("AcquireNextImage swapchain is invalid");
        }
        if (imageIndex != nullptr) {
            *imageIndex = 0;
        }
        if (Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, gpuWaitGPUSignal)) {
            semaphore->signaled = true;
            if (semaphore->desc.type == RHIGPUWaitGPUSignalType::Timeline) {
                ++semaphore->value;
            }
        }
        if (Impl::CPUWaitGPUSignalResource* fence = getRenderResource(impl_->cpuWaitGPUSignals, cpuWaitGPUSignal)) {
            fence->signaled = true;
        }
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

// D3D11 immediate context 没有 Vulkan 那样显式提交 command buffer。
// Flush 会把当前累积的状态/命令推给驱动；semaphore/fence 在本实现中作为统一接口的轻量模拟。
bool RHID3D11::Submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) {
    try {
        for (const RHIQueueWaitDesc& wait : desc.waits) {
            const Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, wait.signal);
            if (semaphore == nullptr) {
                throw std::runtime_error("RHIQueueSubmitDesc contains an invalid wait semaphore");
            }
            if (semaphore->desc.type == RHIGPUWaitGPUSignalType::Timeline && semaphore->value < wait.value) {
                throw std::runtime_error("D3D11 timeline semaphore wait value has not been reached");
            }
        }

        impl_->context->Flush();

        for (const RHIQueueSignalDesc& signal : desc.signals) {
            Impl::GPUWaitGPUSignalResource* semaphore = getRenderResource(impl_->gpuWaitGPUSignals, signal.signal);
            if (semaphore == nullptr) {
                throw std::runtime_error("RHIQueueSubmitDesc contains an invalid signal semaphore");
            }
            semaphore->signaled = true;
            semaphore->value = semaphore->desc.type == RHIGPUWaitGPUSignalType::Timeline ? signal.value : semaphore->value + 1;
        }

        if (Impl::CPUWaitGPUSignalResource* fence = getRenderResource(impl_->cpuWaitGPUSignals, desc.cpuWaitGPUSignal)) {
            D3D11_QUERY_DESC queryDesc{};
            queryDesc.Query = D3D11_QUERY_EVENT;
            throwIfFailed(impl_->device->CreateQuery(&queryDesc, &fence->eventQuery), "Create fence event query failed");
            impl_->context->End(fence->eventQuery.Get());
            fence->signaled = true;
        }
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

// Present 把当前 back buffer 交给 DXGI。Immediate 模式/allowTearing 走 syncInterval=0，
// 其它模式用 syncInterval=1 等待垂直同步。
bool RHID3D11::Present(const RHIPresentDesc& desc, std::string* errorMessage) {
    try {
        Impl::SwapchainResource* swapchain = getRenderResource(impl_->swapchains, desc.swapchain);
        if (swapchain == nullptr || !swapchain->swapchain) {
            throw std::runtime_error("RHIPresentDesc::swapchain is invalid");
        }

        const UINT syncInterval = desc.presentMode == RHIPresentMode::Immediate || desc.allowTearing ? 0u : 1u;
        throwIfFailed(swapchain->swapchain->Present(syncInterval, 0), "IDXGISwapChain::Present failed");
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

// D3D11 的 RecordAndSubmitFrame 不需要录制 command buffer；它直接在 immediate context 上执行：
// 先上传 buffer/texture，再按 RenderGraph pass 绑定 RTV/DSV、viewport/scissor，最后执行
// draw/dispatch。资源状态转换在 D3D11 里大多由驱动隐式处理，所以这里没有 Vulkan 那种
// native barrier；但 RenderGraph 的 RAW/WAR/WAW、裁剪和执行顺序仍然有价值，不能省略。
bool RHID3D11::RecordAndSubmitFrame(
    const RHIFramePacket& packet,
    const RHIRenderGraphExecutionPlan& graphPlan,
    std::string* errorMessage) {
    // transient 容器保存本帧创建的 RHI 句柄，确保正常和异常路径都能统一回收。
    // D3D11 immediate context 会持有已提交资源的内部引用，因此 CPU 侧句柄可在执行后释放。
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
        for (const RHIBufferUploadDesc& upload : packet.uploads.buffers) {
            if (upload.data.empty()) {
                continue;
            }
            Impl::BufferResource* buffer = getRenderResource(impl_->buffers, upload.destination);
            if (buffer == nullptr || !buffer->buffer) {
                throw std::runtime_error("RHIFramePacket buffer upload destination is invalid");
            }
            if (upload.destinationOffset > buffer->desc.size ||
                upload.data.size() > buffer->desc.size - upload.destinationOffset) {
                throw std::runtime_error(
                    "RHIFramePacket buffer upload range exceeds destination buffer size");
            }

            const bool constantBuffer =
                RHIHasAny(buffer->desc.usage, RHIBufferUsage::Uniform);
            if (constantBuffer) {
                // Constant buffers require a whole-resource update with pDstBox == nullptr.
                // The shadow also preserves bytes outside a partial RHI upload.
                std::memcpy(
                    buffer->uploadShadow.data() + upload.destinationOffset,
                    upload.data.data(),
                    upload.data.size());
                if (buffer->desc.memoryUsage == RHIMemoryUsage::CpuToGpu ||
                    buffer->desc.persistentlyMapped) {
                    D3D11_MAPPED_SUBRESOURCE mapped{};
                    throwIfFailed(
                        impl_->context->Map(
                            buffer->buffer.Get(),
                            0,
                            D3D11_MAP_WRITE_DISCARD,
                            0,
                            &mapped),
                        "Map constant buffer upload failed");
                    std::memcpy(
                        mapped.pData,
                        buffer->uploadShadow.data(),
                        buffer->uploadShadow.size());
                    impl_->context->Unmap(buffer->buffer.Get(), 0);
                } else {
                    impl_->context->UpdateSubresource(
                        buffer->buffer.Get(),
                        0,
                        nullptr,
                        buffer->uploadShadow.data(),
                        0,
                        0);
                }
            } else if (
                buffer->desc.memoryUsage == RHIMemoryUsage::CpuToGpu ||
                buffer->desc.persistentlyMapped) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                throwIfFailed(
                    impl_->context->Map(
                        buffer->buffer.Get(),
                        0,
                        D3D11_MAP_WRITE_DISCARD,
                        0,
                        &mapped),
                    "Map buffer upload failed");
                std::memcpy(
                    static_cast<std::byte*>(mapped.pData) +
                        upload.destinationOffset,
                    upload.data.data(),
                    upload.data.size());
                impl_->context->Unmap(buffer->buffer.Get(), 0);
            } else {
                D3D11_BOX box{};
                box.left = static_cast<UINT>(upload.destinationOffset);
                box.right = static_cast<UINT>(upload.destinationOffset + upload.data.size());
                box.top = 0;
                box.bottom = 1;
                box.front = 0;
                box.back = 1;
                impl_->context->UpdateSubresource(buffer->buffer.Get(), 0, &box, upload.data.data(), 0, 0);
            }
        }

        for (const RHITextureUploadDesc& upload : packet.uploads.textures) {
            if (upload.data.empty()) {
                continue;
            }
            Impl::TextureResource* texture = getRenderResource(impl_->textures, upload.destination);
            if (texture == nullptr || !texture->resource) {
                throw std::runtime_error("RHIFramePacket texture upload destination is invalid");
            }
            const UINT rowPitch = static_cast<UINT>(upload.bytesPerRow == 0 ? rowPitchForFormat(texture->desc.format, upload.extent.width) : upload.bytesPerRow);
            const UINT rows = static_cast<UINT>(upload.rowsPerImage == 0 ? upload.extent.height : upload.rowsPerImage);
            D3D11_BOX box{};
            box.left = static_cast<UINT>(upload.offset.x);
            box.top = static_cast<UINT>(upload.offset.y);
            box.front = static_cast<UINT>(upload.offset.z);
            box.right = box.left + upload.extent.width;
            box.bottom = box.top + upload.extent.height;
            box.back = box.front + upload.extent.depth;
            const UINT subresource = D3D11CalcSubresource(upload.mipLevel, upload.arrayLayer, texture->desc.mipLevels);
            impl_->context->UpdateSubresource(texture->resource.Get(), subresource, &box, upload.data.data(), rowPitch, rowPitch * rows);
        }

        // 第一层数组按逻辑资源下标寻址，第二层数组按编译器分配的物理槽寻址。
        // imported 资源不占物理槽；兼容且生命周期不重叠的 transient 逻辑资源共享句柄。
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
                if (view.desc.texture == texture &&
                    (aspect == RHITextureAspect::All ||
                     view.desc.aspect == RHITextureAspect::All ||
                     RHIHasAny(view.desc.aspect, aspect))) {
                    return RHITextureView(index + 1);
                }
            }
            return {};
        };

        const auto bindDrawResources = [&](const std::vector<RHIBindSet>& bindSets, bool compute) {
            // workload 只携带 RHIBindSet；真正把资源铺到 shader slot 的逻辑集中在
            // applyBindSet，draw/dispatch 代码不需要知道资源具体是 buffer、texture 还是 sampler。
            for (RHIBindSet bindSetHandle : bindSets) {
                const Impl::BindSetResource* bindSet = getRenderResource(impl_->bindSets, bindSetHandle);
                if (bindSet == nullptr) {
                    throw std::runtime_error("Draw/dispatch bind set is invalid");
                }
                applyBindSet(impl_->context.Get(), *bindSet, compute);
            }
        };

        const auto bindVertexStreams = [&](const std::vector<RHIVertexStream>& streams) {
            for (const RHIVertexStream& stream : streams) {
                const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, stream.buffer);
                if (buffer == nullptr || !buffer->buffer) {
                    throw std::runtime_error("RHIVertexStream buffer is invalid");
                }
                ID3D11Buffer* nativeBuffer = buffer->buffer.Get();
                UINT stride = static_cast<UINT>(stream.stride);
                UINT offset = static_cast<UINT>(stream.offset);
                impl_->context->IASetVertexBuffers(stream.binding, 1, &nativeBuffer, &stride, &offset);
            }
        };

        const auto recordDraw = [&](const RHIDrawCommand& draw) {
            const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, draw.pipeline);
            if (pipeline == nullptr || pipeline->compute) {
                throw std::runtime_error("RHIDrawCommand pipeline is invalid");
            }
            applyPipeline(impl_->context.Get(), *pipeline);
            bindDrawResources(draw.bindSets, false);
            bindVertexStreams(draw.vertexStreams);
            impl_->context->DrawInstanced(draw.vertexCount, draw.instanceCount, draw.firstVertex, draw.firstInstance);
        };

        const auto recordIndexedDraw = [&](const RHIDrawIndexedCommand& draw) {
            const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, draw.pipeline);
            if (pipeline == nullptr || pipeline->compute) {
                throw std::runtime_error("RHIDrawIndexedCommand pipeline is invalid");
            }
            applyPipeline(impl_->context.Get(), *pipeline);
            bindDrawResources(draw.bindSets, false);
            bindVertexStreams(draw.vertexStreams);

            const Impl::BufferResource* indexBuffer = getRenderResource(impl_->buffers, draw.indexStream.buffer);
            if (indexBuffer == nullptr || !indexBuffer->buffer) {
                throw std::runtime_error("RHIDrawIndexedCommand index buffer is invalid");
            }
            const DXGI_FORMAT indexFormat = draw.indexStream.indexType == RHIIndexType::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
            impl_->context->IASetIndexBuffer(indexBuffer->buffer.Get(), indexFormat, static_cast<UINT>(draw.indexStream.offset));
            impl_->context->DrawIndexedInstanced(
                draw.indexCount,
                draw.instanceCount,
                draw.firstIndex,
                draw.vertexOffsetElements,
                draw.firstInstance);
        };

        const auto recordDispatch = [&](const RHIDispatchCommand& dispatch) {
            const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, dispatch.pipeline);
            if (pipeline == nullptr || !pipeline->compute) {
                throw std::runtime_error("RHIDispatchCommand pipeline is invalid");
            }
            applyPipeline(impl_->context.Get(), *pipeline);
            bindDrawResources(dispatch.bindSets, true);
            impl_->context->Dispatch(dispatch.groupCountX, dispatch.groupCountY, dispatch.groupCountZ);
        };

        // D3D11 驱动管理 native resource state，所以下面的 transition 只同步 RHI 的逻辑
        // 状态追踪，不调用显式 barrier API。compiled pass 顺序仍保证生产者先于消费者。
        for (const RHICompiledRenderGraphPass& compiledPass : graphPlan.passes) {
            const RHIRenderGraphPassDesc& sourcePass =
                packet.graph.passes[compiledPass.sourcePassIndex];
            for (const RHIRenderGraphTransition& transition : compiledPass.transitions) {
                if (transition.resource.IsBuffer()) {
                    Impl::BufferResource* buffer = getRenderResource(
                        impl_->buffers,
                        graphBuffers[transition.resource.index]);
                    if (buffer == nullptr || !buffer->buffer) {
                        throw std::runtime_error(
                            "RenderGraph buffer transition has an invalid D3D11 resource");
                    }
                    buffer->currentState = transition.after;
                } else {
                    Impl::TextureResource* texture = getRenderResource(
                        impl_->textures,
                        graphTextures[transition.resource.index]);
                    if (texture == nullptr || !texture->resource) {
                        throw std::runtime_error(
                            "RenderGraph texture transition has an invalid D3D11 resource");
                    }
                    texture->currentState = transition.after;
                }
            }

            const RHIRenderPassWorkload* workload =
                compiledPass.workloadIndex == RHI_INVALID_INDEX
                    ? nullptr
                    : &packet.workloads[compiledPass.workloadIndex];
            if (workload == nullptr && compiledPass.colorAttachments.empty() &&
                !compiledPass.depthStencilAttachment.has_value()) {
                continue;
            }

            if (workload != nullptr) {
                // 所有资源依赖必须写在 graph pass 的 reads/writes 中，避免 workload barrier
                // 与编译器推导冲突。未实现命令显式报错，便于尽早发现跨后端能力缺口。
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
                        "D3D11 texture copy/blit/mipmap workloads are not implemented yet");
                }
                if (!workload->queryResets.empty() ||
                    !workload->timestampWrites.empty() ||
                    !workload->queryResolves.empty()) {
                    throw std::runtime_error(
                        "D3D11 RenderGraph query workloads are not implemented yet");
                }
                if (!workload->indirectDraws.empty() ||
                    !workload->indexedIndirectDraws.empty() ||
                    !workload->indirectDispatches.empty()) {
                    throw std::runtime_error(
                        "D3D11 RenderGraph indirect workloads are not implemented yet");
                }

                for (const RHIBufferCopyDesc& copy : workload->bufferCopies) {
                    const Impl::BufferResource* source =
                        getRenderResource(impl_->buffers, copy.source);
                    const Impl::BufferResource* destination =
                        getRenderResource(impl_->buffers, copy.destination);
                    if (source == nullptr || destination == nullptr ||
                        !source->buffer || !destination->buffer) {
                        throw std::runtime_error(
                            "D3D11 RenderGraph buffer copy resource is invalid");
                    }
                    if (copy.size == 0 ||
                        copy.sourceOffset > source->desc.size ||
                        copy.size > source->desc.size - copy.sourceOffset ||
                        copy.destinationOffset > destination->desc.size ||
                        copy.size >
                            destination->desc.size - copy.destinationOffset) {
                        throw std::runtime_error(
                            "D3D11 RenderGraph buffer copy range is invalid");
                    }
                    D3D11_BOX sourceBox{};
                    sourceBox.left = static_cast<UINT>(copy.sourceOffset);
                    sourceBox.right = static_cast<UINT>(copy.sourceOffset + copy.size);
                    sourceBox.top = 0;
                    sourceBox.bottom = 1;
                    sourceBox.front = 0;
                    sourceBox.back = 1;
                    impl_->context->CopySubresourceRegion(
                        destination->buffer.Get(),
                        0,
                        static_cast<UINT>(copy.destinationOffset),
                        0,
                        0,
                        source->buffer.Get(),
                        0,
                        &sourceBox);
                }
                if (sourcePass.type == RHIRenderGraphPassType::Copy) {
                    continue;
                }
            }

            // attachment 的资源索引来自缓存计划，clear/load/store 值来自当前 sourcePass。
            // 因此每帧动态清屏值不会破坏计划缓存。
            std::vector<ID3D11RenderTargetView*> rtvs;
            rtvs.reserve(compiledPass.colorAttachments.size());
            for (const RHICompiledRenderGraphAttachment& compiledAttachment :
                 compiledPass.colorAttachments) {
                const RHIRenderGraphAttachmentDesc& attachment =
                    sourcePass.colorAttachments[compiledAttachment.attachmentIndex];
                const RHITextureView viewHandle = findViewForTexture(
                    graphTextures[compiledAttachment.textureIndex],
                    RHITextureAspect::Color);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || !view->rtv) {
                    throw std::runtime_error("RenderGraph color attachment has no D3D11 RTV");
                }
                ID3D11RenderTargetView* rtv = view->rtv.Get();
                rtvs.push_back(rtv);
                if (attachment.loadOp == RHILoadOp::Clear) {
                    const std::array<float, 4> clear = {
                        attachment.clearValue.color.r,
                        attachment.clearValue.color.g,
                        attachment.clearValue.color.b,
                        attachment.clearValue.color.a
                    };
                    impl_->context->ClearRenderTargetView(rtv, clear.data());
                }
            }

            ID3D11DepthStencilView* dsv = nullptr;
            if (compiledPass.depthStencilAttachment.has_value()) {
                const RHICompiledRenderGraphAttachment& compiledAttachment =
                    *compiledPass.depthStencilAttachment;
                const RHIRenderGraphAttachmentDesc& attachment =
                    *sourcePass.depthStencilAttachment;
                const RHITextureView viewHandle = findViewForTexture(
                    graphTextures[compiledAttachment.textureIndex],
                    RHITextureAspect::Depth);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || !view->dsv) {
                    throw std::runtime_error("RenderGraph depth attachment has no D3D11 DSV");
                }
                dsv = view->dsv.Get();
                if (attachment.loadOp == RHILoadOp::Clear) {
                    UINT clearFlags = D3D11_CLEAR_DEPTH;
                    if (hasStencilFormat(
                            packet.graph.textures[compiledAttachment.textureIndex]
                                .desc.format)) {
                        clearFlags |= D3D11_CLEAR_STENCIL;
                    }
                    impl_->context->ClearDepthStencilView(
                        dsv,
                        clearFlags,
                        attachment.clearValue.depthStencil.depth,
                        static_cast<UINT8>(attachment.clearValue.depthStencil.stencil));
                }
            }

            if (!rtvs.empty() || dsv != nullptr) {
                impl_->context->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.empty() ? nullptr : rtvs.data(), dsv);
            }

            const RHIViewport viewport = workload == nullptr ||
                                                 workload->viewport.width == 0.0F ||
                                                 workload->viewport.height == 0.0F
                                             ? packet.settings.viewport
                                             : workload->viewport;
            D3D11_VIEWPORT d3dViewport{};
            d3dViewport.TopLeftX = viewport.x;
            d3dViewport.TopLeftY = viewport.y;
            d3dViewport.Width = viewport.width;
            d3dViewport.Height = viewport.height;
            d3dViewport.MinDepth = viewport.minDepth;
            d3dViewport.MaxDepth = viewport.maxDepth;
            impl_->context->RSSetViewports(1, &d3dViewport);

            const RHIRect2D scissor = workload == nullptr ||
                                              workload->scissor.extent.width == 0 ||
                                              workload->scissor.extent.height == 0
                                          ? packet.settings.scissor
                                          : workload->scissor;
            D3D11_RECT d3dScissor{};
            d3dScissor.left = scissor.offset.x;
            d3dScissor.top = scissor.offset.y;
            d3dScissor.right = scissor.offset.x + static_cast<LONG>(scissor.extent.width);
            d3dScissor.bottom = scissor.offset.y + static_cast<LONG>(scissor.extent.height);
            impl_->context->RSSetScissorRects(1, &d3dScissor);

            if (workload != nullptr) {
                for (const RHIDrawCommand& draw : workload->draws) {
                    recordDraw(draw);
                }
                for (const RHIDrawIndexedCommand& draw : workload->indexedDraws) {
                    recordIndexedDraw(draw);
                }
                for (const RHIDispatchCommand& dispatch : workload->dispatches) {
                    recordDispatch(dispatch);
                }
            }
        }

        for (const RHIQueueSubmitDesc& submitDesc : packet.submissions) {
            if (!Submit(submitDesc, errorMessage)) {
                releaseTransientResources();
                return false;
            }
        }

        releaseTransientResources();
        if (packet.present.has_value()) {
            return Present(*packet.present, errorMessage);
        }
        return true;
    } catch (const std::exception& error) {
        releaseTransientResources();
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool RHID3D11::SubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
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

bool RHID3D11::SubmitFrame(
    const RHIFramePacket& packet,
    const RHIRenderGraphExecutionPlan& graphPlan,
    std::string* errorMessage) {
    // 即使 D3D11 使用 immediate context，也要验证自定义 submission 中的 passName
    // 是否覆盖并遵守 compiled dependency 顺序，使三套后端共享同一帧图语义。
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

void RHID3D11::WaitIdle() const noexcept {
    if (!IsInitialized()) {
        return;
    }

    D3D11_QUERY_DESC queryDesc{};
    queryDesc.Query = D3D11_QUERY_EVENT;
    ComPtr<ID3D11Query> query;
    if (FAILED(impl_->device->CreateQuery(&queryDesc, &query))) {
        impl_->context->Flush();
        return;
    }
    impl_->context->End(query.Get());
    BOOL done = FALSE;
    while (impl_->context->GetData(query.Get(), &done, sizeof(done), 0) == S_FALSE) {
        Sleep(0);
    }
}

// Destroy 系列只清空对应槽位里的 COM 对象和描述，不压缩 vector。
// RHIHandle 是 1-based index，压缩会导致已发出的 RHIHandle 指向错误资源。
// D3D11 frame 片段负责把 RHIFramePacket 直接执行到 immediate context。
// 这里和 Vulkan 后端差别最大：Vulkan 是录制 VkCommandBuffer 后 Submit；
// D3D11 是边遍历 RHIFramePacket 边设置 context 状态并调用 Draw/Dispatch。
// 读这部分时重点关注三层映射：
// - BindSet -> VS/PS/CS 等 stage 的 CBV/SRV/Sampler/UAV slot；
// - Pipeline -> IA/InputLayout/Shader/Rasterizer/DepthStencil/Blend 状态；
// - RenderGraph pass -> OMSetRenderTargets + Clear + Draw/Dispatch + Present。

} // namespace rhi












