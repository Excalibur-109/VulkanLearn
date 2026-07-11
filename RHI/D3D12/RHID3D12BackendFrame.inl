bool RHID3D12Backend::recordAndSubmitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    try {
        // 先支持最基础的 CPU 可见 buffer 上传。GpuOnly buffer/texture 上传需要 staging resource +
        // CopyBufferRegion/CopyTextureRegion + resource barrier，这属于完整 command recording 的一部分。
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

        if (!packet.workloads.empty()) {
            throw std::runtime_error(
                "D3D12 RHIFramePacket workload recording is not implemented yet: "
                "next step is resetting command allocator/list, inserting D3D12_RESOURCE_BARRIER transitions, "
                "copying CPU descriptors into shader-visible heaps, binding root signature/descriptor tables, "
                "and recording draw/dispatch commands.");
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

bool RHID3D12Backend::submitFrame(const RHIFramePacket& packet, std::string* errorMessage) {
    const bool hasUploads = !packet.uploads.buffers.empty() || !packet.uploads.textures.empty();
    if (hasUploads || !packet.workloads.empty()) {
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

void RHID3D12Backend::waitIdle() const noexcept {
    if (!isInitialized() || impl_->fence == nullptr || impl_->fenceEvent == nullptr) {
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
// - 已经可以处理 CPU 可见 buffer 上传、submit、present 和 waitIdle；
// - 还没有把 RenderGraph pass 录制成 command list；
// - 下一步要补的是 resource barrier、RTV/DSV 绑定、descriptor heap 拷贝、root table 设置、Draw/Dispatch。
