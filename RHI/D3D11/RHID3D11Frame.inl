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

bool RHID3D11::acquireNextImage(
    RHISwapchain swapchain,
    RHIGPUWaitGPUSignal gpuWaitGPUSignal,
    RHICPUWaitGPUSignal cpuWaitGPUSignal,
    u32* imageIndex,
    std::string* errorMessage) {
    try {
        // D3D11 swapchain 后端当前只包装一个 back buffer，所以 imageIndex 固定为 0。
        // semaphore/fence 在这里标记为已 signal，用来保持和 Vulkan 调用流程一致。
        if (getRenderResource(impl_->swapchains, swapchain) == nullptr) {
            throw std::runtime_error("acquireNextImage swapchain is invalid");
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
bool RHID3D11::submit(const RHIQueueSubmitDesc& desc, std::string* errorMessage) {
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
bool RHID3D11::present(const RHIPresentDesc& desc, std::string* errorMessage) {
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

// D3D11 的 recordAndSubmitFrame 不需要录制 command buffer；它直接在 immediate context 上执行：
// 先上传 buffer/texture，再按 RenderGraph pass 绑定 RTV/DSV、viewport/scissor，最后执行
// draw/dispatch。资源状态转换在 D3D11 里大多由驱动隐式处理，所以这里没有 Vulkan 那种 barrier。
bool RHID3D11::recordAndSubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    try {
        for (const RHIBufferUploadDesc& upload : packet.uploads.buffers) {
            if (upload.data.empty()) {
                continue;
            }
            Impl::BufferResource* buffer = getRenderResource(impl_->buffers, upload.destination);
            if (buffer == nullptr || !buffer->buffer) {
                throw std::runtime_error("RHIFramePacket buffer upload destination is invalid");
            }

            if (buffer->desc.memoryUsage == RHIMemoryUsage::CpuToGpu || buffer->desc.persistentlyMapped) {
                D3D11_MAPPED_SUBRESOURCE mapped{};
                throwIfFailed(impl_->context->Map(buffer->buffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map buffer upload failed");
                std::memcpy(static_cast<std::byte*>(mapped.pData) + upload.destinationOffset, upload.data.data(), upload.data.size());
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

        std::unordered_map<std::string, RHITexture> textureResources;
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
                if (view.desc.texture == texture &&
                    (aspect == RHITextureAspect::All ||
                     view.desc.aspect == RHITextureAspect::All ||
                     RHIHasAny(view.desc.aspect, aspect))) {
                    return RHITextureView(index + 1);
                }
            }
            return {};
        };

        const auto findWorkload = [&](const std::string& passName) -> const RHIRenderPassWorkload* {
            const auto it = std::find_if(packet.workloads.begin(), packet.workloads.end(), [&](const RHIRenderPassWorkload& workload) {
                return workload.passName == passName;
            });
            return it == packet.workloads.end() ? nullptr : &*it;
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

        for (const RHIRenderGraphPassDesc& pass : packet.graph.passes) {
            // RenderGraph pass 负责声明附件；workload 负责声明 draw/dispatch。这里把二者合并成
            // D3D11 output-merger 绑定和实际绘制命令。
            const RHIRenderPassWorkload* workload = findWorkload(pass.name);
            if (workload == nullptr) {
                continue;
            }

            std::vector<ID3D11RenderTargetView*> rtvs;
            rtvs.reserve(pass.colorAttachments.size());
            for (const RHIRenderGraphAttachmentDesc& attachment : pass.colorAttachments) {
                const RHITextureView viewHandle = findViewForTexture(textureForName(attachment.resourceName), RHITextureAspect::Color);
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
            if (pass.depthStencilAttachment.has_value()) {
                const RHIRenderGraphAttachmentDesc& attachment = *pass.depthStencilAttachment;
                const RHITextureView viewHandle = findViewForTexture(textureForName(attachment.resourceName), RHITextureAspect::Depth);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || !view->dsv) {
                    throw std::runtime_error("RenderGraph depth attachment has no D3D11 DSV");
                }
                dsv = view->dsv.Get();
                if (attachment.loadOp == RHILoadOp::Clear) {
                    impl_->context->ClearDepthStencilView(
                        dsv,
                        D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                        attachment.clearValue.depthStencil.depth,
                        static_cast<UINT8>(attachment.clearValue.depthStencil.stencil));
                }
            }

            if (!rtvs.empty() || dsv != nullptr) {
                impl_->context->OMSetRenderTargets(static_cast<UINT>(rtvs.size()), rtvs.empty() ? nullptr : rtvs.data(), dsv);
            }

            const RHIViewport viewport = workload->viewport.width == 0.0F || workload->viewport.height == 0.0F
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

            const RHIRect2D scissor = workload->scissor.extent.width == 0 || workload->scissor.extent.height == 0
                ? packet.settings.scissor
                : workload->scissor;
            D3D11_RECT d3dScissor{};
            d3dScissor.left = scissor.offset.x;
            d3dScissor.top = scissor.offset.y;
            d3dScissor.right = scissor.offset.x + static_cast<LONG>(scissor.extent.width);
            d3dScissor.bottom = scissor.offset.y + static_cast<LONG>(scissor.extent.height);
            impl_->context->RSSetScissorRects(1, &d3dScissor);

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

        for (const RHIQueueSubmitDesc& submitDesc : packet.submissions) {
            if (!submit(submitDesc, errorMessage)) {
                return false;
            }
        }

        if (packet.present.has_value()) {
            return present(*packet.present, errorMessage);
        }
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool RHID3D11::submitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    // 和 Vulkan 后端保持同一入口：有 workload 就由 renderer 执行帧包；没有 workload 时只处理
    // 外部传入的 submit/present 描述。
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

void RHID3D11::waitIdle() const noexcept {
    if (!isInitialized()) {
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

// destroy 系列只清空对应槽位里的 COM 对象和描述，不压缩 vector。
// RHIHandle 是 1-based index，压缩会导致已发出的 RHIHandle 指向错误资源。
// D3D11 frame 片段负责把 RHIFramePacket 直接执行到 immediate context。
// 这里和 Vulkan 后端差别最大：Vulkan 是录制 VkCommandBuffer 后 submit；
// D3D11 是边遍历 RHIFramePacket 边设置 context 状态并调用 Draw/Dispatch。
// 读这部分时重点关注三层映射：
// - BindSet -> VS/PS/CS 等 stage 的 CBV/SRV/Sampler/UAV slot；
// - Pipeline -> IA/InputLayout/Shader/Rasterizer/DepthStencil/Blend 状态；
// - RenderGraph pass -> OMSetRenderTargets + Clear + Draw/Dispatch + Present。

} // namespace rhi







