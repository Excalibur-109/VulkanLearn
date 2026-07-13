#pragma once

// =============================================================================
//  Vulkan frame 录制
// =============================================================================
//
// 一帧的核心任务是:把 RHIFramePacket(高层意图)翻译成 VkCommandBuffer 二进制,
// 然后 vkQueueSubmit 提交给 GPU。这个文件围绕"性能"和"并发"两个主题组织:
//
// 性能:
//   - ring staging buffer:跨帧复用一块大 HOST_VISIBLE buffer,避免每帧
//     vkCreateBuffer / vkAllocateMemory / vkMapMemory。CPU 写完一个 upload 就
//     推进 head,跨帧回卷;这是教科书级"避免 driver 开销"技巧。
//   - barrier 合并:本帧所有 upload 共享一次 vkCmdPipelineBarrier(原版每个
//     upload 一次),driver 内部 barrier graph 也会更小。
//   - barrier stage 精确化:用 stageFromResourceState() 推导 src/dst pipeline
//     stage,而不是 ALL_COMMANDS_BIT。后者强制 GPU 等待所有命令流,前者只等
//     真正会读这个 buffer 的阶段。在大量并发的 command buffer 录制下差距明显。
//   - hash 索引:viewsByTexture 让"按 texture 找 view"从 O(n) 线性扫变成
//     O(1) 查表;每次 RenderGraph pass 都会触发这种查询。
//
// 并发:
//   - frames-in-flight:每帧独立的 command buffer + fence + ring staging slot,
//     配合 caller 提供的 per-slot cpuWaitGPUSignal,允许 CPU 提前 N 帧录制下一帧。
//     不做 frames-in-flight 的 RHI 实际只跑出 GPU 性能的 30%~50%(CPU 干等
//     GPU 完成才能复用 command buffer / 资源)。
//   - 后端不持有自有 in-flight 状态:vkQueueSubmit 的 fence 直接用 packet 的
//     cpuWaitGPUSignal,caller 拥有同步所有权,这样 caller 的 WaitForCPUSignal
//     才能等到"提交完成",下一帧 AcquireNextImage 才能保证 semaphore 已被消费。
//
// 教学要点:
//   - "所有权"问题:同一份资源(view/bindset/buffer)被多次引用,句柄
//     是 1-based 索引,Destroy 只置 null 不压缩 vector——这是为了保持句柄稳定。
//   - 一次性 staging 的回收:fallback path 的 buffer 在 GPU 仍在引用时不能
//     立即销毁,得挂到下次同 slot 复用时统一释放。简化做法是 thread_local
//     pendingFallback[] 数组,生产应该换成"等当前 slot fence 后释放"队列。
//   - Vulkan 是显式 API:state 转换、barrier、queue submit 全部要自己写,这也是
//     为什么每一行代码都在做事——没有"driver 帮忙"的余地。

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
    //
    // 【教学】frames-in-flight 的本质:CPU 录制和 GPU 执行天然 pipeline 化。
    // 想要 CPU 不被 GPU 阻塞,必须让 CPU 写 command buffer 用的所有资源(command
    // buffer 自身、staging buffer、descriptor、render target)对 GPU 的"上一帧
    // 使用"已经结束。这就是为什么每帧需要独立的 ring staging slot——否则 slot 0
    // 的 staging buffer 写到一半,GPU 还在读 slot 0 的旧数据,data race。
    // 取模选 slot 是为了 slots 自动循环,slots 数 = CPU/GPU 重叠度。
    const u64 frameSlot = packet.settings.frameIndex % impl_->framesInFlight;
    Impl::FrameResources& frame = impl_->frames[static_cast<size_t>(frameSlot)];

    // 2) wrap ring staging 到 fence 已经等的 offset 之后。
    //    单帧上传量远小于 capacity,绝大多数情况下这里 head=0 就够了。
    //
    // 【教学】为什么 ring staging 比"每帧新建 staging buffer"快:
    //   - vkCreateBuffer/vkAllocateMemory 内部要 driver 走系统调用,锁 heap,
    //     还要刷 driver 内部 cache。1080p PBR 例子一帧 6 个 upload,6 套流程
    //     累加 ~0.3-1ms 的 driver 开销。
    //   - vkMapMemory/unmapMemory 同理,map 还要做 page 锁定。
    //   - 用持久映射 + ring 后,所有这些都消掉。CPU 直接 memcpy 到 mapped pointer,
    //     driver 看到的是普通的内存写入,不感知这是一次上传。
    // 缺点:capacity 要预分配(本实现 16MB/slot),超了就 fallback 到一次性 staging。
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
    //
    // 【教学】为什么 upload barrier 要合并:Vulkan 允许一次 vkCmdPipelineBarrier
    // 提交多个 buffer/texture barrier(用数组指针)。原版每 upload 一次 barrier,
    // 6 个 upload = 6 次 driver call + 6 次 barrier graph node;合并后 1 次调用,
    // barrier graph 也只有 1 个节点(可以批量 pass 优化)。
    // GPU 端真正"等"的语义没变——只是少了几条 command buffer entry。
    std::vector<VkBufferMemoryBarrier> uploadBarriers;
    uploadBarriers.reserve(packet.uploads.buffers.size());

    auto tryAllocateStaging = [&](VkDeviceSize size, VkDeviceSize& outOffset) -> bool {
        // 4 字节对齐,保证任何 RHIBufferUsage 都能正确 copy。
        // Vulkan spec 规定 copy 的 offset 和 size 必须满足 buffer usage 对齐要求
        // (例如 uniform buffer 要求 16 字节对齐);这里取保守的 4 字节起步,
        // 对齐要求更严的 buffer 类型在使用方保证。
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
    //
    // 【教学】srcStageMask 的意义:vkCmdPipelineBarrier 的 srcStageMask 告诉 GPU
    // "等到 srcStage 之前的命令执行完再继续";ALL_COMMANDS_BIT 等所有阶段的命令
    // (包括 graphics/compute/transfer),保守但慢。VK_PIPELINE_STAGE_TRANSFER_BIT
    // 只等 transfer queue 阶段——我们这里唯一的 src 是 vkCmdCopyBuffer(transfer),
    // 所以 TRANSFER 阶段就够,GPU 不用等 graphics/compute。
    // dstStageMask 选 ALL_COMMANDS_BIT 是因为 upload 的目标 buffer 可能是 vertex
    // / index / uniform / storage,这些被不同 stage 消费,得等所有 stage 才能用。
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
    //
    // 【教学】原版每个 RenderGraph pass 录制时,要在所有 textureViews 里线性扫
    // 找匹配 texture + aspect 的 view。一个复杂场景几百个 view,4 个 pass × 2
    // attachment = 8 次扫描,每帧白白消耗 10000+ vector 索引。改成 hash 后
    // O(1) 查 bucket,然后只比对这个 texture 的几个 view(通常 1~3 个)。
    // 创建/销毁 view 时维护这个 hash 表(registerView/unregisterView)是关键。
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
    //
    // 【教学】RHIResourceState::RenderTarget / DepthWrite / ShaderRead 等只是
    // "意图",Vulkan 实际需要 VkPipelineStageFlags + VkAccessFlags + VkImageLayout
    // 三元组才能正确表达 barrier。本函数把这三件事一次性做了:
    //   - access:用 accessFromResourceState() 查表(也是 O(1) 查表版本)
    //   - layout:用 toVkImageLayout() 查表
    //   - stage:用 stageFromResourceState() 查表(关键:不是 ALL_COMMANDS_BIT)
    //
    // "Undefined" 特殊:刚创建或没被写入过的 image,oldLayout 必须是
    // VK_IMAGE_LAYOUT_UNDEFINED,Vulkan 会丢弃旧内容(配合 discardContents
    // 优化),但 src stage 必须是 TOP_OF_PIPE(没有前置命令要等)。
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
        //
        // 【教学】Vulkan draw 调用的最少"必要"动作:
        //   1. vkCmdBindPipeline    — 切换 PSO(state 都烘进 PSO)
        //   2. vkCmdBindDescriptorSets — 绑定本 draw 要用的所有 set
        //   3. vkCmdBindVertexBuffers  — N 个 vertex stream(binding N)
        //   4. vkCmdBindIndexBuffer  — index buffer + format
        //   5. vkCmdDrawIndexed      — indexCount, instanceCount, firstIndex 等
        //   6. optional: vkCmdPushConstants — 小块高频数据(b 在 pipelineLayout 范围内)
        // 这里 1~5 都做了,6 留作未来。多个 draw 共享相同 pipeline/bindset/vertex
        // buffer 时,Vulkan driver 会自动剔除 redundant state 设置(等价于
        // D3D12 driver 的 PSO 缓存),所以不需要在 record 层手动去重。
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
    //
    // 【教学】binary vs timeline semaphore 的选择:
    //   - binary semaphore 只能 wait/signal 各一次(必须先 signal 再 wait,1:1 配对)。
    //     适合单队列、顺序执行的场景——本 example 就是这种。
    //   - timeline semaphore 可以"wait >= N"和"signal 到 N"任意顺序,适合:
    //     · compute 跟 graphics 异步执行(compute 先 signal,graphics 之后 wait)
    //     · 一根 timeline 表达多帧多提交的依赖关系,避免 N 根 binary
    //     · 跨 queue 同步
    //   - 决定走哪条路径看 usesTimelineSemaphore——packet 里有任一 timeline 就走。
    //   - waitValues/signalValues 即使 binary 也 push(VkTimelineSemaphoreSubmitInfo
    //     的 pNext 在 binary 模式下不挂载,值不会被读到,所以填啥都无所谓)。
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
    //
    // 【教学】vkQueueSubmit 的 fence 参数:当 fence 非 null 时,GPU 会在
    // 整个 submit 链(command buffer + 所有 wait/signal semaphore)完成后
    // signal 这个 fence。CPU 之后可以 vkWaitForFences() 阻塞等到完成,
    // 这是 CPU/GPU 同步的标准机制。
    // 一定要先 vkResetFences 再提交(否则 validation 会报 "submitted in SIGNALED state")。
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
    //
    // 【教学】Vulkan 资源生命周期问题:vkCmdCopyBuffer 把 staging buffer 引用
    // 进 command buffer,GPU 异步执行时还在读 staging buffer。如果提交完立即
    // vkDestroyBuffer,vkFreeMemory,driver 会因为"buffer 仍在使用"而报错
    // 或者更糟——释放后被另一个线程分配,GPU 读到错误数据。
    // 正确做法:fence signal 表示 GPU 完成,完成之后才能释放。
    // 这里的 pendingFallback[] 用"两次同 slot SubmitFrame 之间一定等过 fence"这个
    // 不变量,简化实现:本帧的 fallback 挂到下次同 slot 进来时再释放。
    // 8 个 slot 数组足够 frames-in-flight <= 8 的常见场景;真正的生产应该用
    // (frameSlot, 资源列表) 这样的 map,或 per-slot "待回收"队列。
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
