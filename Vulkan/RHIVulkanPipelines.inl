#pragma once

#include "RHIVulkanPrivate.inl"

namespace rhi {

static VkStencilOpState toVkStencilState(const RHIStencilFaceState& state) {
    VkStencilOpState vkState{};
    vkState.failOp = toVkStencilOp(state.failOp);
    vkState.passOp = toVkStencilOp(state.passOp);
    vkState.depthFailOp = toVkStencilOp(state.depthFailOp);
    vkState.compareOp = toVkCompareOp(state.compareOp);
    vkState.compareMask = state.compareMask;
    vkState.writeMask = state.writeMask;
    vkState.reference = state.reference;
    return vkState;
}

// GraphicsPipeline 在 Vulkan 中是“大状态对象”：shader stages、vertex layout、primitive、
// raster/depth/blend/multisample 等固定状态会一起烘进 VkPipeline。本实现使用 dynamic
// rendering，所以 pipeline 只记录附件 format，不需要提前创建 VkRenderPass。
RHIPipeline RHIVulkan::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    if (!impl_->caps.supportsDynamicRendering) {
        throw std::runtime_error("The Vulkan graphics pipeline implementation requires dynamic rendering");
    }

    const Impl::PipelineLayoutResource* layout = getRenderResource(impl_->pipelineLayouts, desc.layout);
    if (layout == nullptr || layout->layout == VK_NULL_HANDLE) {
        throw std::runtime_error("RHIGraphicsPipelineDesc::layout is invalid");
    }

    std::vector<VkShaderModule> temporaryModules;
    std::vector<RHIShader> temporaryShaderHandles;
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    temporaryModules.reserve(desc.shaders.size());
    temporaryShaderHandles.reserve(desc.shaders.size());
    shaderStages.reserve(desc.shaders.size());

    const auto DestroyTemporaryShaderModules = [&]() noexcept {
        for (RHIShader handle : temporaryShaderHandles) {
            Destroy(handle);
        }
        temporaryShaderHandles.clear();
        temporaryModules.clear();
    };

    try {
        for (const RHIShaderDesc& shader : desc.shaders) {
            RHIShader shaderHandle = CreateShaderModule(shader);
            temporaryShaderHandles.push_back(shaderHandle);
            Impl::ShaderResource* shaderResource = getRenderResource(impl_->shaders, shaderHandle);
            temporaryModules.push_back(shaderResource->module);

            VkPipelineShaderStageCreateInfo stageInfo{};
            stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stageInfo.stage = toVkSingleShaderStage(shader.stage);
            stageInfo.module = shaderResource->module;
            stageInfo.pName = shader.entryPoint.empty() ? "main" : shader.entryPoint.c_str();
            shaderStages.push_back(stageInfo);
        }
    } catch (...) {
        DestroyTemporaryShaderModules();
        throw;
    }

    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    for (const RHIVertexBufferLayoutDesc& binding : desc.vertexBuffers) {
        VkVertexInputBindingDescription vkBinding{};
        vkBinding.binding = binding.binding;
        vkBinding.stride = static_cast<u32>(binding.stride);
        vkBinding.inputRate = binding.inputRate == RHIVertexInputRate::PerInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
        bindingDescriptions.push_back(vkBinding);

        for (const RHIVertexAttributeDesc& attribute : binding.attributes) {
            VkVertexInputAttributeDescription vkAttribute{};
            vkAttribute.location = attribute.location;
            vkAttribute.binding = attribute.binding;
            vkAttribute.format = toVkVertexFormat(attribute.format);
            vkAttribute.offset = static_cast<u32>(attribute.offset);
            attributeDescriptions.push_back(vkAttribute);
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = static_cast<u32>(bindingDescriptions.size());
    vertexInput.pVertexBindingDescriptions = bindingDescriptions.data();
    vertexInput.vertexAttributeDescriptionCount = static_cast<u32>(attributeDescriptions.size());
    vertexInput.pVertexAttributeDescriptions = attributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVkPrimitiveTopology(desc.inputAssembly.topology);
    inputAssembly.primitiveRestartEnable = desc.inputAssembly.primitiveRestart ? VK_TRUE : VK_FALSE;

    VkPipelineTessellationStateCreateInfo tessellation{};
    tessellation.sType = VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO;
    tessellation.patchControlPoints = desc.inputAssembly.patchControlPoints;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.depthClampEnable = desc.raster.depthClampEnable ? VK_TRUE : VK_FALSE;
    raster.rasterizerDiscardEnable = VK_FALSE;
    raster.polygonMode = toVkPolygonMode(desc.raster.polygonMode);
    raster.cullMode = toVkCullMode(desc.raster.cullMode);
    raster.frontFace = toVkFrontFace(desc.raster.frontFace);
    raster.depthBiasEnable = desc.raster.depthBiasEnable ? VK_TRUE : VK_FALSE;
    raster.depthBiasConstantFactor = desc.raster.depthBiasConstantFactor;
    raster.depthBiasClamp = desc.raster.depthBiasClamp;
    raster.depthBiasSlopeFactor = desc.raster.depthBiasSlopeFactor;
    raster.lineWidth = desc.raster.lineWidth;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = toVkSampleCount(desc.multisample.samples);
    multisample.sampleShadingEnable = desc.multisample.sampleShadingEnable ? VK_TRUE : VK_FALSE;
    multisample.minSampleShading = desc.multisample.minSampleShading;
    VkSampleMask sampleMask = static_cast<VkSampleMask>(desc.multisample.sampleMask);
    multisample.pSampleMask = &sampleMask;
    multisample.alphaToCoverageEnable = desc.multisample.alphaToCoverageEnable ? VK_TRUE : VK_FALSE;
    multisample.alphaToOneEnable = desc.multisample.alphaToOneEnable ? VK_TRUE : VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = desc.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = desc.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = toVkCompareOp(desc.depthStencil.depthCompareOp);
    depthStencil.depthBoundsTestEnable = desc.depthStencil.depthBoundsTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.stencilTestEnable = desc.depthStencil.stencilTestEnable ? VK_TRUE : VK_FALSE;
    depthStencil.front = toVkStencilState(desc.depthStencil.front);
    depthStencil.back = toVkStencilState(desc.depthStencil.back);
    depthStencil.minDepthBounds = desc.depthStencil.minDepthBounds;
    depthStencil.maxDepthBounds = desc.depthStencil.maxDepthBounds;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments;
    const size_t attachmentCount = desc.blend.attachments.empty() ? desc.colorFormats.size() : desc.blend.attachments.size();
    blendAttachments.reserve(attachmentCount);
    for (size_t i = 0; i < attachmentCount; ++i) {
        const RHIColorBlendAttachmentState src = desc.blend.attachments.empty() ? RHIColorBlendAttachmentState{} : desc.blend.attachments[i];
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.blendEnable = src.blendEnable ? VK_TRUE : VK_FALSE;
        attachment.srcColorBlendFactor = toVkBlendFactor(src.sourceColor);
        attachment.dstColorBlendFactor = toVkBlendFactor(src.destinationColor);
        attachment.colorBlendOp = toVkBlendOp(src.colorOp);
        attachment.srcAlphaBlendFactor = toVkBlendFactor(src.sourceAlpha);
        attachment.dstAlphaBlendFactor = toVkBlendFactor(src.destinationAlpha);
        attachment.alphaBlendOp = toVkBlendOp(src.alphaOp);
        attachment.colorWriteMask = toVkColorWriteMask(src.writeMask);
        blendAttachments.push_back(attachment);
    }

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.logicOpEnable = desc.blend.logicOpEnable ? VK_TRUE : VK_FALSE;
    blend.attachmentCount = static_cast<u32>(blendAttachments.size());
    blend.pAttachments = blendAttachments.data();
    std::copy(desc.blend.blendConstants.begin(), desc.blend.blendConstants.end(), blend.blendConstants);

    std::vector<VkDynamicState> dynamicStates;
    dynamicStates.reserve(desc.dynamicStates.size());
    for (RHIDynamicState state : desc.dynamicStates) {
        dynamicStates.push_back(toVkDynamicState(state));
    }
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<u32>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    std::vector<VkFormat> colorFormats;
    colorFormats.reserve(desc.colorFormats.size());
    for (RHIFormat format : desc.colorFormats) {
        colorFormats.push_back(toVkFormat(format));
    }
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = static_cast<u32>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();
    rendering.depthAttachmentFormat = isDepthFormat(desc.depthStencilFormat) ? toVkFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;
    rendering.stencilAttachmentFormat = hasStencilFormat(desc.depthStencilFormat) ? toVkFormat(desc.depthStencilFormat) : VK_FORMAT_UNDEFINED;

    VkPipelineCache cache = VK_NULL_HANDLE;
    if (const Impl::PipelineCacheResource* cacheResource = getRenderResource(impl_->pipelineCaches, desc.cache)) {
        cache = cacheResource->cache;
    }

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = static_cast<u32>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = desc.inputAssembly.topology == RHIPrimitiveTopology::PatchList ? &tessellation : nullptr;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = layout->layout;
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = desc.subpass;

    Impl::PipelineResource resource{};
    resource.bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    resource.layout = layout->layout;
    if (vkCreateGraphicsPipelines(impl_->native.device, cache, 1, &pipelineInfo, nullptr, &resource.pipeline) != VK_SUCCESS) {
        DestroyTemporaryShaderModules();
        throw std::runtime_error("vkCreateGraphicsPipelines failed");
    }

    DestroyTemporaryShaderModules();

    const RHIPipeline handle = makeRenderHandle<RHIPipeline>(impl_->pipelines, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(impl_->pipelines.back().pipeline), desc.debugName);
    return handle;
}

// Compute pipeline 比图形管线简单：只有一个 compute shader stage 和一个 pipeline layout。
// 创建完成后临时 shader module 可以销毁，因为 VkPipeline 已经内部引用/编译了需要的信息。
RHIPipeline RHIVulkan::CreateComputePipeline(const RHIComputePipelineDesc& desc) {
    const Impl::PipelineLayoutResource* layout = getRenderResource(impl_->pipelineLayouts, desc.layout);
    if (layout == nullptr || layout->layout == VK_NULL_HANDLE) {
        throw std::runtime_error("RHIComputePipelineDesc::layout is invalid");
    }

    RHIShader shaderHandle = CreateShaderModule(desc.shader);
    Impl::ShaderResource* shader = getRenderResource(impl_->shaders, shaderHandle);

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = shader->module;
    stage.pName = desc.shader.entryPoint.empty() ? "main" : desc.shader.entryPoint.c_str();

    VkPipelineCache cache = VK_NULL_HANDLE;
    if (const Impl::PipelineCacheResource* cacheResource = getRenderResource(impl_->pipelineCaches, desc.cache)) {
        cache = cacheResource->cache;
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stage;
    pipelineInfo.layout = layout->layout;

    Impl::PipelineResource resource{};
    resource.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    resource.layout = layout->layout;
    if (vkCreateComputePipelines(impl_->native.device, cache, 1, &pipelineInfo, nullptr, &resource.pipeline) != VK_SUCCESS) {
        vkDestroyShaderModule(impl_->native.device, shader->module, nullptr);
        shader->module = VK_NULL_HANDLE;
        throw std::runtime_error("vkCreateComputePipelines failed");
    }

    vkDestroyShaderModule(impl_->native.device, shader->module, nullptr);
    shader->module = VK_NULL_HANDLE;

    const RHIPipeline handle = makeRenderHandle<RHIPipeline>(impl_->pipelines, std::move(resource));
    impl_->setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<u64>(impl_->pipelines.back().pipeline), desc.debugName);
    return handle;
}

// QueryPool 用于 GPU 侧统计：timestamp 量时间，occlusion 量通过深度/模板测试的样本，
// pipeline statistics 量各阶段调用次数。不是所有统计项都默认可用，所以初始化时会检查 feature。

} // namespace rhi







