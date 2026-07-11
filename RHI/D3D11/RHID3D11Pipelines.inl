#pragma once

#include "RHID3D11Private.inl"

namespace rhi {

static D3D11_DEPTH_STENCILOP_DESC toD3DStencilFace(const RHIStencilFaceState& state) {
    D3D11_DEPTH_STENCILOP_DESC desc{};
    desc.StencilFailOp = toD3DStencilOp(state.failOp);
    desc.StencilDepthFailOp = toD3DStencilOp(state.depthFailOp);
    desc.StencilPassOp = toD3DStencilOp(state.passOp);
    desc.StencilFunc = toD3DCompare(state.compareOp);
    return desc;
}

// 图形 pipeline 在 D3D11 里不是一个原生大对象，而是一组状态对象和 shader 组合：
// input layout、shader stages、rasterizer state、depth-stencil state、blend state。
// applyPipeline 会在绘制时把这些对象依次设置到 immediate context。
RHIPipeline RHID3D11::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D11 is not initialized");
    }
    if (getRenderResource(impl_->pipelineLayouts, desc.layout) == nullptr) {
        throw std::runtime_error("RHIGraphicsPipelineDesc::layout is invalid");
    }

    Impl::PipelineResource resource{};
    resource.compute = false;
    resource.topology = toD3DTopology(desc.inputAssembly);
    resource.stencilRef = desc.depthStencil.front.reference;
    resource.blendConstants = desc.blend.blendConstants;
    resource.sampleMask = static_cast<UINT>(desc.multisample.sampleMask & 0xFFFFFFFFu);

    std::vector<RHIShader> temporaryShaders;
    RHIShader vertexShaderHandle{};

    try {
        for (const RHIShaderDesc& shaderDesc : desc.shaders) {
            RHIShader handle = createShaderModule(shaderDesc);
            temporaryShaders.push_back(handle);
            const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, handle);
            switch (shaderDesc.stage) {
            case RHIShaderStage::Vertex:
                resource.vertexShader = shader->vertexShader;
                vertexShaderHandle = handle;
                break;
            case RHIShaderStage::TessControl:
                resource.hullShader = shader->hullShader;
                break;
            case RHIShaderStage::TessEvaluation:
                resource.domainShader = shader->domainShader;
                break;
            case RHIShaderStage::Geometry:
                resource.geometryShader = shader->geometryShader;
                break;
            case RHIShaderStage::Fragment:
                resource.pixelShader = shader->pixelShader;
                break;
            default:
                throw std::runtime_error("Graphics pipeline contains a non-graphics shader stage");
            }
        }

        const Impl::ShaderResource* vertexShader = getRenderResource(impl_->shaders, vertexShaderHandle);
        if (vertexShader == nullptr || !resource.vertexShader) {
            throw std::runtime_error("RHIGraphicsPipelineDesc requires a vertex shader");
        }

        size_t vertexAttributeCount = 0;
        for (const RHIVertexBufferLayoutDesc& vertexBuffer : desc.vertexBuffers) {
            vertexAttributeCount += vertexBuffer.attributes.size();
        }

        std::vector<std::string> semanticNames;
        semanticNames.reserve(vertexAttributeCount);
        std::vector<D3D11_INPUT_ELEMENT_DESC> elements;
        elements.reserve(vertexAttributeCount);
        for (const RHIVertexBufferLayoutDesc& vertexBuffer : desc.vertexBuffers) {
            for (const RHIVertexAttributeDesc& attribute : vertexBuffer.attributes) {
                semanticNames.push_back(attribute.semanticName.empty() ? "TEXCOORD" : attribute.semanticName);
                D3D11_INPUT_ELEMENT_DESC element{};
                element.SemanticName = semanticNames.back().c_str();
                element.SemanticIndex = attribute.semanticName.empty() ? attribute.location : attribute.semanticIndex;
                element.Format = toD3DVertexFormat(attribute.format);
                element.InputSlot = attribute.binding;
                element.AlignedByteOffset = static_cast<UINT>(attribute.offset);
                element.InputSlotClass = vertexBuffer.inputRate == RHIVertexInputRate::PerInstance
                    ? D3D11_INPUT_PER_INSTANCE_DATA
                    : D3D11_INPUT_PER_VERTEX_DATA;
                element.InstanceDataStepRate = vertexBuffer.inputRate == RHIVertexInputRate::PerInstance ? vertexBuffer.stepRate : 0;
                elements.push_back(element);
            }
        }

        if (!elements.empty()) {
            throwIfFailed(
                impl_->device->CreateInputLayout(
                    elements.data(),
                    static_cast<UINT>(elements.size()),
                    vertexShader->bytecode.data(),
                    vertexShader->bytecode.size(),
                    &resource.inputLayout),
                "CreateInputLayout failed");
        }

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = desc.raster.polygonMode == RHIPolygonMode::Line ? D3D11_FILL_WIREFRAME : D3D11_FILL_SOLID;
        switch (desc.raster.cullMode) {
        case RHICullMode::None: rasterDesc.CullMode = D3D11_CULL_NONE; break;
        case RHICullMode::Front: rasterDesc.CullMode = D3D11_CULL_FRONT; break;
        case RHICullMode::Back:
        case RHICullMode::FrontAndBack: rasterDesc.CullMode = D3D11_CULL_BACK; break;
        }
        rasterDesc.FrontCounterClockwise = desc.raster.frontFace == RHIFrontFace::CounterClockwise;
        rasterDesc.DepthBias = static_cast<INT>(desc.raster.depthBiasConstantFactor);
        rasterDesc.DepthBiasClamp = desc.raster.depthBiasClamp;
        rasterDesc.SlopeScaledDepthBias = desc.raster.depthBiasSlopeFactor;
        rasterDesc.DepthClipEnable = !desc.raster.depthClampEnable;
        rasterDesc.ScissorEnable = true;
        rasterDesc.MultisampleEnable = desc.multisample.samples != RHISampleCount::Count1;
        rasterDesc.AntialiasedLineEnable = false;
        throwIfFailed(impl_->device->CreateRasterizerState(&rasterDesc, &resource.rasterizerState), "CreateRasterizerState failed");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = desc.depthStencil.depthTestEnable;
        depthDesc.DepthWriteMask = desc.depthStencil.depthWriteEnable ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc = toD3DCompare(desc.depthStencil.depthCompareOp);
        depthDesc.StencilEnable = desc.depthStencil.stencilTestEnable;
        depthDesc.StencilReadMask = static_cast<UINT8>(desc.depthStencil.front.compareMask & 0xFFu);
        depthDesc.StencilWriteMask = static_cast<UINT8>(desc.depthStencil.front.writeMask & 0xFFu);
        depthDesc.FrontFace = toD3DStencilFace(desc.depthStencil.front);
        depthDesc.BackFace = toD3DStencilFace(desc.depthStencil.back);
        throwIfFailed(impl_->device->CreateDepthStencilState(&depthDesc, &resource.depthStencilState), "CreateDepthStencilState failed");

        D3D11_BLEND_DESC blendDesc{};
        blendDesc.AlphaToCoverageEnable = desc.multisample.alphaToCoverageEnable;
        blendDesc.IndependentBlendEnable = desc.blend.attachments.size() > 1;
        const size_t attachmentCount = std::max<size_t>(1, std::min<size_t>(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, desc.blend.attachments.empty() ? desc.colorFormats.size() : desc.blend.attachments.size()));
        for (size_t i = 0; i < attachmentCount; ++i) {
            const RHIColorBlendAttachmentState src = desc.blend.attachments.empty() ? RHIColorBlendAttachmentState{} : desc.blend.attachments[i];
            D3D11_RENDER_TARGET_BLEND_DESC& target = blendDesc.RenderTarget[i];
            target.BlendEnable = src.blendEnable;
            target.SrcBlend = toD3DBlend(src.sourceColor);
            target.DestBlend = toD3DBlend(src.destinationColor);
            target.BlendOp = toD3DBlendOp(src.colorOp);
            target.SrcBlendAlpha = toD3DBlend(src.sourceAlpha);
            target.DestBlendAlpha = toD3DBlend(src.destinationAlpha);
            target.BlendOpAlpha = toD3DBlendOp(src.alphaOp);
            target.RenderTargetWriteMask = toD3DColorWriteMask(src.writeMask);
        }
        for (size_t i = attachmentCount; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            blendDesc.RenderTarget[i].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        }
        throwIfFailed(impl_->device->CreateBlendState(&blendDesc, &resource.blendState), "CreateBlendState failed");

        impl_->setDebugName(resource.inputLayout.Get(), desc.debugName + ".InputLayout");
        impl_->setDebugName(resource.rasterizerState.Get(), desc.debugName + ".Rasterizer");
        impl_->setDebugName(resource.depthStencilState.Get(), desc.debugName + ".DepthStencil");
        impl_->setDebugName(resource.blendState.Get(), desc.debugName + ".Blend");
    } catch (...) {
        for (RHIShader handle : temporaryShaders) {
            destroy(handle);
        }
        throw;
    }

    for (RHIShader handle : temporaryShaders) {
        destroy(handle);
    }
    return makeRenderHandle<RHIPipeline>(impl_->pipelines, std::move(resource));
}

// Compute pipeline 只需要 compute shader。为了和统一 RHIPipeline 对齐，仍然存到
// PipelineResource，并用 compute=true 区分 applyPipeline 的绑定路径。
RHIPipeline RHID3D11::createComputePipeline(const RHIComputePipelineDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D11 is not initialized");
    }
    if (getRenderResource(impl_->pipelineLayouts, desc.layout) == nullptr) {
        throw std::runtime_error("RHIComputePipelineDesc::layout is invalid");
    }

    RHIShader shaderHandle = createShaderModule(desc.shader);
    const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, shaderHandle);
    if (shader == nullptr || !shader->computeShader) {
        destroy(shaderHandle);
        throw std::runtime_error("RHIComputePipelineDesc requires a compute shader");
    }

    Impl::PipelineResource resource{};
    resource.compute = true;
    resource.computeShader = shader->computeShader;
    destroy(shaderHandle);
    return makeRenderHandle<RHIPipeline>(impl_->pipelines, std::move(resource));
}

// D3D11 query 是单个 ID3D11Query 对象；为了匹配统一 QueryPool 抽象，这里预创建一组 query。
// D3D11 pipelines 片段负责把 RHIGraphicsPipelineDesc/RHIComputePipelineDesc 翻译成 D3D11 状态组合。
// 和 Vulkan 的 VkPipeline 不同，D3D11 没有一个完整的 graphics pipeline 对象；
// 本实现把 input layout、shader stages、rasterizer、depth-stencil、blend、topology 等保存在 PipelineResource，
// 真正绘制时由 RHID3D11Frame.inl 的 applyPipeline 逐项绑定到 immediate context。

} // namespace rhi

