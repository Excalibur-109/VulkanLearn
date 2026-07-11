#pragma once

#include "RHIVulkanPrivate.inl"

namespace rhi {

RHIBuffer RHIVulkan::CreateBuffer(const RHIBufferDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHIVulkan is not initialized");
    }
    if (desc.size == 0) {
        throw std::runtime_error("RHIBufferDesc::size must be greater than zero");
    }

    Impl::BufferResource resource{};
    resource.desc = desc;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = static_cast<VkDeviceSize>(desc.size);
    bufferInfo.usage = toVkBufferUsage(desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (RHIHasAny(desc.flags, RHIBufferCreateFlags::SparseBinding)) {
        bufferInfo.flags |= VK_BUFFER_CREATE_SPARSE_BINDING_BIT;
    }

    if (vkCreateBuffer(impl_->native.device, &bufferInfo, nullptr, &resource.buffer) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateBuffer failed");
    }

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(impl_->native.device, resource.buffer, &requirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = impl_->findMemoryType(requirements.memoryTypeBits, toVkMemoryProperties(desc.memoryUsage));

    if (vkAllocateMemory(impl_->native.device, &allocateInfo, nullptr, &resource.memory) != VK_SUCCESS) {
        vkDestroyBuffer(impl_->native.device, resource.buffer, nullptr);
        throw std::runtime_error("vkAllocateMemory(buffer) failed");
    }
    // 创建对象和分配内存只是两步准备；绑定成功后，buffer 才真正拥有可访问的存储。
    if (vkBindBufferMemory(impl_->native.device, resource.buffer, resource.memory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(impl_->native.device, resource.buffer, nullptr);
        vkFreeMemory(impl_->native.device, resource.memory, nullptr);
        throw std::runtime_error("vkBindBufferMemory failed");
    }

    if (desc.persistentlyMapped) {
        if (vkMapMemory(impl_->native.device, resource.memory, 0, VK_WHOLE_SIZE, 0, &resource.mapped) != VK_SUCCESS) {
            vkDestroyBuffer(impl_->native.device, resource.buffer, nullptr);
            vkFreeMemory(impl_->native.device, resource.memory, nullptr);
            throw std::runtime_error("vkMapMemory(buffer) failed");
        }
    }

    const RHIBuffer handle = makeRenderHandle<RHIBuffer>(impl_->buffers, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<u64>(impl_->buffers.back().buffer), desc.debugName);
    return handle;
}

// Texture 对应 VkImage。注意 VkImage 只是“存储和布局状态”，真正给 shader 或 render pass
// 使用时还需要 VkImageView；因此 CreateTexture 只负责 image + memory，CreateTextureView
// 才负责 format/aspect/mip/layer 这些访问窗口。
RHITexture RHIVulkan::CreateTexture(const RHITextureDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHIVulkan is not initialized");
    }

    Impl::TextureResource resource{};
    resource.desc = desc;
    resource.currentState = desc.initialState;
    resource.ownsImage = true;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.flags = toVkImageCreateFlags(desc.flags);
    imageInfo.imageType = toVkImageType(desc.dimension);
    imageInfo.format = toVkFormat(desc.format);
    imageInfo.extent = {desc.extent.width, desc.extent.height, desc.extent.depth};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.samples = toVkSampleCount(desc.samples);
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVkImageUsage(desc.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(impl_->native.device, &imageInfo, nullptr, &resource.image) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImage failed");
    }

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(impl_->native.device, resource.image, &requirements);

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex = impl_->findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(impl_->native.device, &allocateInfo, nullptr, &resource.memory) != VK_SUCCESS) {
        vkDestroyImage(impl_->native.device, resource.image, nullptr);
        throw std::runtime_error("vkAllocateMemory(image) failed");
    }
    if (vkBindImageMemory(impl_->native.device, resource.image, resource.memory, 0) != VK_SUCCESS) {
        vkDestroyImage(impl_->native.device, resource.image, nullptr);
        vkFreeMemory(impl_->native.device, resource.memory, nullptr);
        throw std::runtime_error("vkBindImageMemory failed");
    }

    const RHITexture handle = makeRenderHandle<RHITexture>(impl_->textures, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<u64>(impl_->textures.back().image), desc.debugName);
    return handle;
}

// ImageView 是 Vulkan 访问图片的入口：同一个 VkImage 可以有多个 view，分别选择不同 mip、
// array layer、format reinterpretation 或 depth/stencil aspect。RenderGraph 找附件时也是
// 通过 RHITextureView 找到可绑定的 VkImageView。
RHITextureView RHIVulkan::CreateTextureView(const RHITextureViewDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHIVulkan is not initialized");
    }

    const Impl::TextureResource* texture = getRenderResource(impl_->textures, desc.texture);
    if (texture == nullptr || texture->image == VK_NULL_HANDLE) {
        throw std::runtime_error("RHITextureViewDesc::texture is invalid");
    }

    const RHIFormat viewFormat = desc.format == RHIFormat::Undefined ? texture->desc.format : desc.format;

    Impl::TextureViewResource resource{};
    resource.desc = desc;

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture->image;
    viewInfo.viewType = toVkImageViewType(desc.dimension);
    viewInfo.format = toVkFormat(viewFormat);
    viewInfo.subresourceRange.aspectMask = toVkImageAspect(desc.aspect, viewFormat);
    viewInfo.subresourceRange.baseMipLevel = desc.baseMipLevel;
    viewInfo.subresourceRange.levelCount = desc.mipLevelCount;
    viewInfo.subresourceRange.baseArrayLayer = desc.baseArrayLayer;
    viewInfo.subresourceRange.layerCount = desc.arrayLayerCount;

    if (vkCreateImageView(impl_->native.device, &viewInfo, nullptr, &resource.view) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateImageView failed");
    }

    const RHITextureView handle = makeRenderHandle<RHITextureView>(impl_->textureViews, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<u64>(impl_->textureViews.back().view), desc.debugName);
    return handle;
}

// Sampler 只描述采样规则，不拥有纹理数据。Vulkan 把 sampled image 和 sampler 拆开是常见
// 做法：同一张 texture 可以配多个 sampler，同一个 sampler 也能复用到多张 texture。
RHISampler RHIVulkan::CreateSampler(const RHISamplerDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHIVulkan is not initialized");
    }

    Impl::SamplerResource resource{};
    resource.desc = desc;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = toVkFilter(desc.magFilter);
    samplerInfo.minFilter = toVkFilter(desc.minFilter);
    samplerInfo.mipmapMode = toVkMipmapMode(desc.mipmapMode);
    samplerInfo.addressModeU = toVkAddressMode(desc.addressU);
    samplerInfo.addressModeV = toVkAddressMode(desc.addressV);
    samplerInfo.addressModeW = toVkAddressMode(desc.addressW);
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.anisotropyEnable = desc.enableAnisotropy ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = std::min(desc.maxAnisotropy, impl_->caps.maxSamplerAnisotropy);
    samplerInfo.compareEnable = desc.enableCompare ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = toVkCompareOp(desc.compareOp);
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;
    samplerInfo.borderColor = toVkBorderColor(desc.borderColor);

    if (vkCreateSampler(impl_->native.device, &samplerInfo, nullptr, &resource.sampler) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateSampler failed");
    }

    const RHISampler handle = makeRenderHandle<RHISampler>(impl_->samplers, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<u64>(impl_->samplers.back().sampler), desc.debugName);
    return handle;
}

// Vulkan shader module 只接收 SPIR-V bytecode。这里不编译 GLSL/HLSL，而是假定上层已经
// 提供 bytecode 或文件路径；entry point 和 stage 会在创建 pipeline 时写进 shader stage。
RHIShader RHIVulkan::CreateShaderModule(const RHIShaderDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHIVulkan is not initialized");
    }

    std::vector<std::byte> bytecode = desc.bytecode;
    if (bytecode.empty() && !desc.filePath.empty()) {
        bytecode = readBinaryFile(desc.filePath);
    }
    if (bytecode.empty() || (bytecode.size() % sizeof(u32)) != 0) {
        throw std::runtime_error("Vulkan shader modules require non-empty, 4-byte-aligned SPIR-V bytecode");
    }

    Impl::ShaderResource resource{};
    resource.desc = desc;

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = bytecode.size();
    moduleInfo.pCode = reinterpret_cast<const u32*>(bytecode.data());

    if (vkCreateShaderModule(impl_->native.device, &moduleInfo, nullptr, &resource.module) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateShaderModule failed");
    }

    const RHIShader handle = makeRenderHandle<RHIShader>(impl_->shaders, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_SHADER_MODULE, reinterpret_cast<u64>(impl_->shaders.back().module), desc.debugName);
    return handle;
}

// BindSetLayout 对应 Vulkan descriptor set layout：它声明某个 set 里有哪些 binding、
// 每个 binding 是 buffer/image/sampler，以及哪些 shader stage 可见。Push constant 不在
// descriptor set 中分配，所以这里跳过，稍后交给 PipelineLayout。
RHIBindSetLayout RHIVulkan::CreateBindSetLayout(const RHIBindSetLayoutDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHIVulkan is not initialized");
    }

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(desc.entries.size());
    for (const RHIBindSetLayoutEntry& entry : desc.entries) {
        if (entry.type == RHIBindingType::PushConstant) {
            // Vulkan push constant 属于 pipeline layout，不占用 descriptor set binding。
            continue;
        }
        if (entry.type == RHIBindingType::AccelerationStructure) {
            throw std::runtime_error("AccelerationStructure bindings require a ray-tracing resource model, which is not implemented by RHIVulkan");
        }
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = entry.binding;
        binding.descriptorType = toVkDescriptorType(entry.type);
        binding.descriptorCount = std::max(1u, entry.arrayCount);
        binding.stageFlags = toVkShaderStages(entry.visibility);
        bindings.push_back(binding);
    }

    Impl::BindSetLayoutResource resource{};
    resource.desc = desc;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<u32>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(impl_->native.device, &layoutInfo, nullptr, &resource.layout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreateDescriptorSetLayout failed");
    }

    const RHIBindSetLayout handle = makeRenderHandle<RHIBindSetLayout>(impl_->bindSetLayouts, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<u64>(impl_->bindSetLayouts.back().layout), desc.debugName);
    return handle;
}

// BindSet 对应实际 descriptor set。layout 只声明“槽位形状”，这里把具体 buffer/view/sampler
// 写入 VkDescriptorSet。绘制时只需要 vkCmdBindDescriptorSets，不再逐个资源绑定。
RHIBindSet RHIVulkan::CreateBindSet(const RHIBindSetDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHIVulkan is not initialized");
    }

    const Impl::BindSetLayoutResource* layout = getRenderResource(impl_->bindSetLayouts, desc.layout);
    if (layout == nullptr || layout->layout == VK_NULL_HANDLE) {
        throw std::runtime_error("RHIBindSetDesc::layout is invalid");
    }

    Impl::BindSetResource resource{};
    resource.desc = desc;

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = impl_->descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &layout->layout;

    if (vkAllocateDescriptorSets(impl_->native.device, &allocateInfo, &resource.set) != VK_SUCCESS) {
        throw std::runtime_error("vkAllocateDescriptorSets failed");
    }

    std::vector<VkDescriptorBufferInfo> bufferInfos;
    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkWriteDescriptorSet> writes;
    bufferInfos.reserve(desc.bindings.size());
    imageInfos.reserve(desc.bindings.size());
    writes.reserve(desc.bindings.size());

    for (const RHIResourceBinding& binding : desc.bindings) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = resource.set;
        write.dstBinding = binding.binding;
        write.dstArrayElement = binding.arrayElement;
        write.descriptorCount = 1;
        write.descriptorType = toVkDescriptorType(binding.type);

        if (binding.type == RHIBindingType::UniformBuffer || binding.type == RHIBindingType::StorageBuffer) {
            const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, binding.buffer.buffer);
            if (buffer == nullptr || buffer->buffer == VK_NULL_HANDLE) {
                throw std::runtime_error("RHIResourceBinding buffer is invalid");
            }
            VkDescriptorBufferInfo info{};
            info.buffer = buffer->buffer;
            info.offset = binding.buffer.offset;
            info.range = toVkWholeSize(binding.buffer.size);
            bufferInfos.push_back(info);
            write.pBufferInfo = &bufferInfos.back();
        } else if (binding.type == RHIBindingType::Sampler) {
            const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
            if (sampler == nullptr || sampler->sampler == VK_NULL_HANDLE) {
                throw std::runtime_error("RHIResourceBinding sampler is invalid");
            }
            VkDescriptorImageInfo info{};
            info.sampler = sampler->sampler;
            imageInfos.push_back(info);
            write.pImageInfo = &imageInfos.back();
        } else if (binding.type == RHIBindingType::SampledTexture ||
                   binding.type == RHIBindingType::StorageTexture ||
                   binding.type == RHIBindingType::CombinedTextureSampler) {
            const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, binding.texture.view);
            if (view == nullptr || view->view == VK_NULL_HANDLE) {
                throw std::runtime_error("RHIResourceBinding texture view is invalid");
            }
            VkDescriptorImageInfo info{};
            info.imageView = view->view;
            info.imageLayout = binding.type == RHIBindingType::StorageTexture ? VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            if (binding.type == RHIBindingType::CombinedTextureSampler) {
                const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
                if (sampler == nullptr || sampler->sampler == VK_NULL_HANDLE) {
                    throw std::runtime_error("CombinedTextureSampler requires a valid sampler");
                }
                info.sampler = sampler->sampler;
            }
            imageInfos.push_back(info);
            write.pImageInfo = &imageInfos.back();
        } else if (binding.type == RHIBindingType::AccelerationStructure) {
            throw std::runtime_error("AccelerationStructure descriptor updates are not implemented");
        } else {
            continue;
        }

        writes.push_back(write);
    }

    if (!writes.empty()) {
        vkUpdateDescriptorSets(impl_->native.device, static_cast<u32>(writes.size()), writes.data(), 0, nullptr);
    }

    return makeRenderHandle<RHIBindSet>(impl_->bindSets, std::move(resource));
}

// PipelineLayout 是 shader 资源接口的总表：它组合多个 descriptor set layout，并声明 push
// constant 范围。图形/计算 pipeline 创建和命令绑定 descriptor set 时都必须使用同一个 layout。
RHIPipelineLayout RHIVulkan::CreatePipelineLayout(const RHIPipelineLayoutDesc& desc) {
    if (!IsInitialized()) {
        throw std::runtime_error("RHIVulkan is not initialized");
    }

    std::vector<VkDescriptorSetLayout> setLayouts;
    setLayouts.reserve(desc.bindSetLayouts.size());
    for (RHIBindSetLayout handle : desc.bindSetLayouts) {
        const Impl::BindSetLayoutResource* layout = getRenderResource(impl_->bindSetLayouts, handle);
        if (layout == nullptr || layout->layout == VK_NULL_HANDLE) {
            throw std::runtime_error("RHIPipelineLayoutDesc contains an invalid bind set layout");
        }
        setLayouts.push_back(layout->layout);
    }

    std::vector<VkPushConstantRange> pushRanges;
    pushRanges.reserve(desc.pushConstants.size());
    for (const RHIPushConstantRange& range : desc.pushConstants) {
        VkPushConstantRange vkRange{};
        vkRange.stageFlags = toVkShaderStages(range.stages);
        vkRange.offset = range.offset;
        vkRange.size = range.size;
        pushRanges.push_back(vkRange);
    }

    Impl::PipelineLayoutResource resource{};
    resource.desc = desc;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<u32>(setLayouts.size());
    layoutInfo.pSetLayouts = setLayouts.data();
    layoutInfo.pushConstantRangeCount = static_cast<u32>(pushRanges.size());
    layoutInfo.pPushConstantRanges = pushRanges.data();

    if (vkCreatePipelineLayout(impl_->native.device, &layoutInfo, nullptr, &resource.layout) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineLayout failed");
    }

    const RHIPipelineLayout handle = makeRenderHandle<RHIPipelineLayout>(impl_->pipelineLayouts, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_PIPELINE_LAYOUT, reinterpret_cast<u64>(impl_->pipelineLayouts.back().layout), desc.debugName);
    return handle;
}

RHIPipelineCache RHIVulkan::CreatePipelineCache(const RHIPipelineCacheDesc& desc) {
    Impl::PipelineCacheResource resource{};
    resource.desc = desc;

    VkPipelineCacheCreateInfo cacheInfo{};
    cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    cacheInfo.initialDataSize = desc.initialData.size();
    cacheInfo.pInitialData = desc.initialData.empty() ? nullptr : desc.initialData.data();

    if (vkCreatePipelineCache(impl_->native.device, &cacheInfo, nullptr, &resource.cache) != VK_SUCCESS) {
        throw std::runtime_error("vkCreatePipelineCache failed");
    }

    const RHIPipelineCache handle = makeRenderHandle<RHIPipelineCache>(impl_->pipelineCaches, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_PIPELINE_CACHE, reinterpret_cast<u64>(impl_->pipelineCaches.back().cache), desc.debugName);
    return handle;
}

} // namespace rhi










