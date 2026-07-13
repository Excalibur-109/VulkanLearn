#pragma once

#include "RHID3D12Private.inl"

namespace rhi {

// D3D12 frame 录制:command allocator/list 复用(每帧 reset),RenderGraph pass
// 走 barrier + RTV/DSV bind + root param bind + draw。BindSet 的 descriptor 来自
// shader-visible heap,直接 SetGraphicsRootDescriptorTable 即可,不需 GPU 端 copy。
//
// 暂未实现:GPU-only buffer staging copy、texture upload、compute dispatch、indirect draw。
// 这些在例子当前用法下不触发,只对 GPU-only buffer upload 抛异常。
bool RHID3D12::RecordAndSubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    try {
        if (impl_->device == nullptr || impl_->commandAllocator == nullptr || impl_->commandList == nullptr) {
            throw std::runtime_error("D3D12 device/command allocator/list is not initialized");
        }

        // 1) 重置 command allocator + list。
        //
        // 【教学】D3D12 跟 Vulkan 的关键差异:
        //   - D3D12 用 command allocator(command buffer 的内存池) + command list(录制器)。
        //     两者解耦,allocator 可以装多个 list。
        //   - Reset allocator 必须等 GPU 用完——这跟 Vulkan 的 command pool reset
        //     概念相同。但 D3D12 没"per-frame fence 自动等"机制,需要在 caller
        //     侧保证;本例子里 AcquireNextImage 之前 WaitForCPUSignal(slot) 就够了。
        //   - Reset list 第二个参数是"初始 PSO",nullptr 表示不绑定(之后用
        //     SetPipelineState 单独设)。绑定初始 PSO 能省一次 state 切换,但
        //     要求第一个 draw 用那个 PSO——本实现没这限制,所以传 nullptr。
        if (FAILED(impl_->commandAllocator->Reset())) {
            throw std::runtime_error("ID3D12CommandAllocator::Reset failed");
        }
        if (FAILED(impl_->commandList->Reset(impl_->commandAllocator.Get(), nullptr))) {
            throw std::runtime_error("ID3D12GraphicsCommandList::Reset failed");
        }

        // 2) SetDescriptorHeaps:必须在第一次 SetGraphicsRootDescriptorTable 之前。
        //    CBV_SRV_UAV + SAMPLER 都已 shader-visible(见 RHID3D12Core.inl)。
        //
        // 【教学】D3D12 的 descriptor 跟 Vulkan 关键差异:
        //   - D3D12 descriptor heap 分 CPU-only 和 shader-visible 两类。
        //     shader-visible heap 的 descriptor 可以直接 SetGraphicsRootDescriptorTable
        //     绑定;CPU-only 的必须先 CopyDescriptors 到 shader-visible heap。
        //   - 本实现把 CBV_SRV_UAV 和 SAMPLER heap 设为 shader-visible,
        //     CreateTextureView/CBV/SRV 时直接拿到的 descriptor 就能 GPU 端用。
        //   - 一个 command list 同时只能"激活"一组 heap(最多 1 个 CBV_SRV_UAV + 1 个 SAMPLER),
        //     这就是为什么 SetDescriptorHeaps 只传两个。
        ID3D12DescriptorHeap* heaps[2] = {
            impl_->cbvSrvUavHeap.heap.Get(),
            impl_->samplerHeap.heap.Get()
        };
        impl_->commandList->SetDescriptorHeaps(2, heaps);

        // 3) CPU 可见 buffer upload:已映射的 buffer 直接 memcpy;否则抛"需要 staging copy"。
        for (const RHIBufferUploadDesc& upload : packet.uploads.buffers) {
            if (upload.data.empty()) {
                continue;
            }
            Impl::BufferResource* buffer = getRenderResource(impl_->buffers, upload.destination);
            if (buffer == nullptr || !buffer->resource) {
                throw std::runtime_error("RHIFramePacket buffer upload destination is invalid");
            }
            if (buffer->mappedData == nullptr) {
                throw std::runtime_error("D3D12 GPU-only buffer upload staging is not implemented yet");
            }
            if (upload.destinationOffset + upload.data.size() > buffer->desc.size) {
                throw std::runtime_error("RHIFramePacket buffer upload range exceeds destination buffer size");
            }
            std::memcpy(static_cast<std::byte*>(buffer->mappedData) + upload.destinationOffset, upload.data.data(), upload.data.size());
        }

        if (!packet.uploads.textures.empty()) {
            throw std::runtime_error("D3D12 texture upload staging is not implemented yet");
        }

        // 4) 收集 RenderGraph 中的 imported texture
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

        // 5) 状态转换 + RTV/DSV 查找工具
        const auto transitionTexture = [&](RHITexture handle, D3D12_RESOURCE_STATES target) {
            Impl::TextureResource* texture = getRenderResource(impl_->textures, handle);
            if (texture == nullptr || texture->resource == nullptr || texture->currentState == target) {
                return;
            }
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.Transition.pResource = texture->resource.Get();
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = texture->currentState;
            barrier.Transition.StateAfter = target;
            impl_->commandList->ResourceBarrier(1, &barrier);
            texture->currentState = target;
        };

        const auto findViewForTexture = [&](RHITexture texture, bool depth) -> RHITextureView {
            if (!texture) {
                return {};
            }
            for (u64 index = 0; index < impl_->textureViews.size(); ++index) {
                const Impl::TextureViewResource& view = impl_->textureViews[static_cast<size_t>(index)];
                if (view.desc.texture == texture) {
                    if (depth ? view.dsv.valid : view.rtv.valid) {
                        return RHITextureView(index + 1);
                    }
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

        // 6) 遍历 RenderGraph pass
        for (const RHIRenderGraphPassDesc& pass : packet.graph.passes) {
            for (const RHIRenderGraphResourceRef& read : pass.reads) {
                if (read.type == RHIRenderGraphResourceType::Texture || read.type == RHIRenderGraphResourceType::SwapchainImage) {
                    transitionTexture(textureForName(read.name), toD3D12ResourceStates(read.state));
                }
            }
            for (const RHIRenderGraphResourceRef& write : pass.writes) {
                if (write.type == RHIRenderGraphResourceType::Texture || write.type == RHIRenderGraphResourceType::SwapchainImage) {
                    transitionTexture(textureForName(write.name), toD3D12ResourceStates(write.state));
                }
            }

            const RHIRenderPassWorkload* workload = findWorkload(pass.name);
            if (workload == nullptr) {
                continue;
            }

            // 收集 RTV/DSV
            std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
            for (const RHIRenderGraphAttachmentDesc& attachment : pass.colorAttachments) {
                const RHITexture tex = textureForName(attachment.resourceName);
                const RHITextureView viewHandle = findViewForTexture(tex, false);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || !view->rtv.valid) {
                    throw std::runtime_error("RenderGraph color attachment has no D3D12 RTV");
                }
                if (attachment.loadOp == RHILoadOp::Clear) {
                    const FLOAT clear[] = {
                        attachment.clearValue.color.r,
                        attachment.clearValue.color.g,
                        attachment.clearValue.color.b,
                        attachment.clearValue.color.a
                    };
                    impl_->commandList->ClearRenderTargetView(view->rtv.handle, clear, 0, nullptr);
                }
                rtvs.push_back(view->rtv.handle);
            }

            D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
            bool hasDsv = false;
            if (pass.depthStencilAttachment.has_value()) {
                const RHIRenderGraphAttachmentDesc& attachment = *pass.depthStencilAttachment;
                const RHITexture tex = textureForName(attachment.resourceName);
                const RHITextureView viewHandle = findViewForTexture(tex, true);
                const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, viewHandle);
                if (view == nullptr || !view->dsv.valid) {
                    throw std::runtime_error("RenderGraph depth attachment has no D3D12 DSV");
                }
                if (attachment.loadOp == RHILoadOp::Clear) {
                    const D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL;
                    impl_->commandList->ClearDepthStencilView(
                        view->dsv.handle,
                        clearFlags,
                        attachment.clearValue.depthStencil.depth,
                        static_cast<UINT8>(attachment.clearValue.depthStencil.stencil),
                        0, nullptr);
                }
                dsv = view->dsv.handle;
                hasDsv = true;
            }

            if (!rtvs.empty() || hasDsv) {
                impl_->commandList->OMSetRenderTargets(
                    static_cast<UINT>(rtvs.size()),
                    rtvs.empty() ? nullptr : rtvs.data(),
                    FALSE,
                    hasDsv ? &dsv : nullptr);
            }

            // Viewport / scissor
            const RHIViewport viewport = workload->viewport.width == 0.0F || workload->viewport.height == 0.0F
                ? packet.settings.viewport
                : workload->viewport;
            D3D12_VIEWPORT d3dViewport{};
            d3dViewport.TopLeftX = viewport.x;
            d3dViewport.TopLeftY = viewport.y;
            d3dViewport.Width = viewport.width;
            d3dViewport.Height = viewport.height;
            d3dViewport.MinDepth = viewport.minDepth;
            d3dViewport.MaxDepth = viewport.maxDepth;
            impl_->commandList->RSSetViewports(1, &d3dViewport);

            const RHIRect2D scissor = workload->scissor.extent.width == 0 || workload->scissor.extent.height == 0
                ? packet.settings.scissor
                : workload->scissor;
            D3D12_RECT d3dScissor{};
            d3dScissor.left = scissor.offset.x;
            d3dScissor.top = scissor.offset.y;
            d3dScissor.right = scissor.offset.x + static_cast<LONG>(scissor.extent.width);
            d3dScissor.bottom = scissor.offset.y + static_cast<LONG>(scissor.extent.height);
            impl_->commandList->RSSetScissorRects(1, &d3dScissor);

            // 录制 draws
            //
            // 【教学】D3D12 draw 的最少必要动作:
            //   1. SetPipelineState        — 切换 PSO(state 全烘进 PSO)
            //   2. SetGraphicsRootSignature — 切换 root signature(PSO 创建时绑定,
            //                                   但 explicit 调能 hot-swap 不同 PSO 共享 signature)
            //   3. SetGraphicsRootDescriptorTable ×N — 绑定 descriptor table 到 root param slot
            //   4. IASetVertexBuffers / IASetIndexBuffer — 几何输入
            //   5. DrawIndexedInstanced    — draw
            //
            // 【教学】关于 root param index 的简化:
            //   createRootSignatureForLayout 给 layout 每个 entry 加一个 root param,
            //   所以 root param i = layout entry i。但 CombinedTextureSampler 实际上
            //   会加 2 个 root param(SRV + SAMPLER),这里按 1 entry 1 root param 处理
            //   是简化,真实工程应该把 root param 数量和顺序在 PipelineLayoutResource
            //   里存下来,record 时直接索引。当前简化对 PBR 例子的非组合 binding 是对的。
            for (const RHIDrawIndexedCommand& draw : workload->indexedDraws) {
                const Impl::PipelineResource* pipeline = getRenderResource(impl_->pipelines, draw.pipeline);
                if (pipeline == nullptr || pipeline->pipelineState == nullptr) {
                    throw std::runtime_error("RHIDrawIndexedCommand pipeline is invalid or not yet created");
                }
                impl_->commandList->SetPipelineState(pipeline->pipelineState.Get());
                if (pipeline->rootSignature) {
                    impl_->commandList->SetGraphicsRootSignature(pipeline->rootSignature.Get());
                }

                // Bind bind sets,按 root param 顺序逐 binding 设置 descriptor table
                u32 rootParamIndex = 0;
                for (RHIBindSet bindSetHandle : draw.bindSets) {
                    const Impl::BindSetResource* bindSet = getRenderResource(impl_->bindSets, bindSetHandle);
                    if (bindSet == nullptr) {
                        throw std::runtime_error("RHIDrawIndexedCommand bind set is invalid");
                    }
                    for (const Impl::ResolvedBinding& resolved : bindSet->bindings) {
                        if (!resolved.resourceDescriptor.valid) {
                            ++rootParamIndex;
                            continue;
                        }
                        impl_->commandList->SetGraphicsRootDescriptorTable(rootParamIndex, resolved.resourceDescriptor.gpuHandle);
                        ++rootParamIndex;
                    }
                }

                // Vertex buffer views
                std::vector<D3D12_VERTEX_BUFFER_VIEW> vbViews;
                vbViews.reserve(draw.vertexStreams.size());
                for (const RHIVertexStream& stream : draw.vertexStreams) {
                    const Impl::BufferResource* vb = getRenderResource(impl_->buffers, stream.buffer);
                    if (vb == nullptr || !vb->resource) {
                        throw std::runtime_error("RHIDrawIndexedCommand vertex buffer is invalid");
                    }
                    D3D12_VERTEX_BUFFER_VIEW view{};
                    view.BufferLocation = vb->resource->GetGPUVirtualAddress() + stream.offset;
                    view.SizeInBytes = static_cast<UINT>(stream.stride == 0 ? vb->desc.size : stream.stride);
                    view.StrideInBytes = static_cast<UINT>(stream.stride);
                    vbViews.push_back(view);
                }
                if (!vbViews.empty()) {
                    impl_->commandList->IASetVertexBuffers(0, static_cast<UINT>(vbViews.size()), vbViews.data());
                }

                const Impl::BufferResource* ib = getRenderResource(impl_->buffers, draw.indexStream.buffer);
                if (ib == nullptr || !ib->resource) {
                    throw std::runtime_error("RHIDrawIndexedCommand index buffer is invalid");
                }
                D3D12_INDEX_BUFFER_VIEW ibView{};
                ibView.BufferLocation = ib->resource->GetGPUVirtualAddress() + draw.indexStream.offset;
                ibView.SizeInBytes = static_cast<UINT>(ib->desc.size - draw.indexStream.offset);
                ibView.Format = draw.indexStream.indexType == RHIIndexType::UInt16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
                impl_->commandList->IASetIndexBuffer(&ibView);

                impl_->commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                impl_->commandList->DrawIndexedInstanced(
                    draw.indexCount,
                    draw.instanceCount,
                    draw.firstIndex,
                    draw.vertexOffsetElements,
                    draw.firstInstance);
            }
        }

        if (FAILED(impl_->commandList->Close())) {
            throw std::runtime_error("ID3D12GraphicsCommandList::Close failed");
        }

        // 7) 提交 + Present
        for (const RHIQueueSubmitDesc& submitDesc : packet.submissions) {
            if (!Submit(submitDesc, errorMessage)) {
                return false;
            }
        }

        if (packet.present.has_value()) {
            return Present(*packet.present, errorMessage);
        }
        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        return false;
    }
}

bool RHID3D12::SubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    const bool hasUploads = !packet.uploads.buffers.empty() || !packet.uploads.textures.empty();
    if (hasUploads || !packet.workloads.empty()) {
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

void RHID3D12::WaitForCPUSignal(RHICPUWaitGPUSignal handle) const noexcept {
    if (!IsInitialized()) {
        return;
    }
    const Impl::CPUWaitGPUSignalResource* fence = getRenderResource(impl_->cpuWaitGPUSignals, handle);
    if (fence == nullptr || fence->fence == nullptr || fence->eventHandle == nullptr) {
        return;
    }
    if (fence->fence->GetCompletedValue() >= fence->value) {
        return;
    }
    if (SUCCEEDED(fence->fence->SetEventOnCompletion(fence->value, fence->eventHandle))) {
        WaitForSingleObject(fence->eventHandle, INFINITE);
    }
}

} // namespace rhi
