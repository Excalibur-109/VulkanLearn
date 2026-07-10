static D3D12_DEPTH_STENCILOP_DESC toD3D12StencilFace(const StencilFaceState& state) {
    D3D12_DEPTH_STENCILOP_DESC desc{};
    desc.StencilFailOp = toD3D12StencilOp(state.failOp);
    desc.StencilDepthFailOp = toD3D12StencilOp(state.depthFailOp);
    desc.StencilPassOp = toD3D12StencilOp(state.passOp);
    desc.StencilFunc = toD3D12Compare(state.compareOp);
    return desc;
}

static D3D12_LOGIC_OP toD3D12LogicOp(LogicOp op) {
    switch (op) {
    case LogicOp::Clear:        return D3D12_LOGIC_OP_CLEAR;
    case LogicOp::And:          return D3D12_LOGIC_OP_AND;
    case LogicOp::AndReverse:   return D3D12_LOGIC_OP_AND_REVERSE;
    case LogicOp::Copy:         return D3D12_LOGIC_OP_COPY;
    case LogicOp::AndInverted:  return D3D12_LOGIC_OP_AND_INVERTED;
    case LogicOp::NoOp:         return D3D12_LOGIC_OP_NOOP;
    case LogicOp::Xor:          return D3D12_LOGIC_OP_XOR;
    case LogicOp::Or:           return D3D12_LOGIC_OP_OR;
    case LogicOp::Nor:          return D3D12_LOGIC_OP_NOR;
    case LogicOp::Equivalent:   return D3D12_LOGIC_OP_EQUIV;
    case LogicOp::Invert:       return D3D12_LOGIC_OP_INVERT;
    case LogicOp::OrReverse:    return D3D12_LOGIC_OP_OR_REVERSE;
    case LogicOp::CopyInverted: return D3D12_LOGIC_OP_COPY_INVERTED;
    case LogicOp::OrInverted:   return D3D12_LOGIC_OP_OR_INVERTED;
    case LogicOp::Nand:         return D3D12_LOGIC_OP_NAND;
    case LogicOp::Set:          return D3D12_LOGIC_OP_SET;
    }
    return D3D12_LOGIC_OP_COPY;
}

// D3D12 graphics pipeline 是一个真正的 PSO。它会固定 shader bytecode、input layout、
// rasterizer/depth/blend、RTV/DSV 格式、MSAA 等状态。后续 command list 只需要 SetPipelineState。
PipelineHandle D3D12Renderer::createGraphicsPipeline(const GraphicsPipelineDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D12Renderer is not initialized");
    }

    const Impl::PipelineLayoutResource* layout = getRenderResource(impl_->pipelineLayouts, desc.layout);
    if (layout == nullptr || !layout->rootSignature) {
        throw std::runtime_error("GraphicsPipelineDesc::layout is invalid");
    }

    Impl::PipelineResource resource{};
    resource.compute = false;
    resource.topology = toD3D12Topology(desc.inputAssembly);
    resource.stencilRef = desc.depthStencil.front.reference;
    resource.blendConstants = desc.blend.blendConstants;
    resource.rootSignature = layout->rootSignature;

    std::vector<ShaderHandle> temporaryShaders;
    ShaderHandle vertexShaderHandle{};
    ShaderHandle hullShaderHandle{};
    ShaderHandle domainShaderHandle{};
    ShaderHandle geometryShaderHandle{};
    ShaderHandle pixelShaderHandle{};

    try {
        for (const ShaderDesc& shaderDesc : desc.shaders) {
            ShaderHandle handle = createShaderModule(shaderDesc);
            temporaryShaders.push_back(handle);
            switch (shaderDesc.stage) {
            case ShaderStage::Vertex:
                vertexShaderHandle = handle;
                break;
            case ShaderStage::TessControl:
                hullShaderHandle = handle;
                break;
            case ShaderStage::TessEvaluation:
                domainShaderHandle = handle;
                break;
            case ShaderStage::Geometry:
                geometryShaderHandle = handle;
                break;
            case ShaderStage::Fragment:
                pixelShaderHandle = handle;
                break;
            default:
                throw std::runtime_error("Graphics pipeline contains a non-graphics shader stage");
            }
        }

        const Impl::ShaderResource* vertexShader = getRenderResource(impl_->shaders, vertexShaderHandle);
        if (vertexShader == nullptr || vertexShader->bytecode.empty()) {
            throw std::runtime_error("GraphicsPipelineDesc requires a vertex shader");
        }

        size_t vertexAttributeCount = 0;
        for (const VertexBufferLayoutDesc& vertexBuffer : desc.vertexBuffers) {
            vertexAttributeCount += vertexBuffer.attributes.size();
        }

        std::vector<std::string> semanticNames;
        semanticNames.reserve(vertexAttributeCount);
        std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
        elements.reserve(vertexAttributeCount);
        for (const VertexBufferLayoutDesc& vertexBuffer : desc.vertexBuffers) {
            for (const VertexAttributeDesc& attribute : vertexBuffer.attributes) {
                semanticNames.push_back(attribute.semanticName.empty() ? "TEXCOORD" : attribute.semanticName);
                D3D12_INPUT_ELEMENT_DESC element{};
                element.SemanticName = semanticNames.back().c_str();
                element.SemanticIndex = attribute.semanticName.empty() ? attribute.location : attribute.semanticIndex;
                element.Format = toD3D12VertexFormat(attribute.format);
                element.InputSlot = attribute.binding;
                element.AlignedByteOffset = static_cast<UINT>(attribute.offset);
                element.InputSlotClass = vertexBuffer.inputRate == VertexInputRate::PerInstance
                    ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                    : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                element.InstanceDataStepRate = vertexBuffer.inputRate == VertexInputRate::PerInstance ? vertexBuffer.stepRate : 0;
                elements.push_back(element);
            }
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
        psoDesc.pRootSignature = layout->rootSignature.Get();
        psoDesc.VS = {vertexShader->bytecode.data(), vertexShader->bytecode.size()};
        if (const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, hullShaderHandle)) {
            psoDesc.HS = {shader->bytecode.data(), shader->bytecode.size()};
        }
        if (const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, domainShaderHandle)) {
            psoDesc.DS = {shader->bytecode.data(), shader->bytecode.size()};
        }
        if (const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, geometryShaderHandle)) {
            psoDesc.GS = {shader->bytecode.data(), shader->bytecode.size()};
        }
        if (const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, pixelShaderHandle)) {
            psoDesc.PS = {shader->bytecode.data(), shader->bytecode.size()};
        }

        psoDesc.BlendState.AlphaToCoverageEnable = desc.multisample.alphaToCoverageEnable;
        psoDesc.BlendState.IndependentBlendEnable = desc.blend.attachments.size() > 1;
        const size_t attachmentCount = std::max<size_t>(1, std::min<size_t>(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT, desc.blend.attachments.empty() ? desc.colorFormats.size() : desc.blend.attachments.size()));
        for (size_t i = 0; i < attachmentCount; ++i) {
            const ColorBlendAttachmentState src = desc.blend.attachments.empty() ? ColorBlendAttachmentState{} : desc.blend.attachments[i];
            D3D12_RENDER_TARGET_BLEND_DESC& target = psoDesc.BlendState.RenderTarget[i];
            target.BlendEnable = desc.blend.logicOpEnable ? FALSE : src.blendEnable;
            target.LogicOpEnable = desc.blend.logicOpEnable;
            target.SrcBlend = toD3D12Blend(src.sourceColor);
            target.DestBlend = toD3D12Blend(src.destinationColor);
            target.BlendOp = toD3D12BlendOp(src.colorOp);
            target.SrcBlendAlpha = toD3D12Blend(src.sourceAlpha);
            target.DestBlendAlpha = toD3D12Blend(src.destinationAlpha);
            target.BlendOpAlpha = toD3D12BlendOp(src.alphaOp);
            target.LogicOp = toD3D12LogicOp(desc.blend.logicOp);
            target.RenderTargetWriteMask = toD3D12ColorWriteMask(src.writeMask);
        }
        for (size_t i = attachmentCount; i < D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) {
            psoDesc.BlendState.RenderTarget[i].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        }

        psoDesc.SampleMask = static_cast<UINT>(desc.multisample.sampleMask & 0xFFFFFFFFu);
        psoDesc.RasterizerState.FillMode = desc.raster.polygonMode == PolygonMode::Line ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
        switch (desc.raster.cullMode) {
        case CullMode::None:         psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; break;
        case CullMode::Front:        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
        case CullMode::Back:
        case CullMode::FrontAndBack: psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; break;
        }
        psoDesc.RasterizerState.FrontCounterClockwise = desc.raster.frontFace == FrontFace::CounterClockwise;
        psoDesc.RasterizerState.DepthBias = static_cast<INT>(desc.raster.depthBiasConstantFactor);
        psoDesc.RasterizerState.DepthBiasClamp = desc.raster.depthBiasClamp;
        psoDesc.RasterizerState.SlopeScaledDepthBias = desc.raster.depthBiasSlopeFactor;
        psoDesc.RasterizerState.DepthClipEnable = !desc.raster.depthClampEnable;
        psoDesc.RasterizerState.MultisampleEnable = desc.multisample.samples != SampleCount::Count1;
        psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;
        psoDesc.RasterizerState.ForcedSampleCount = 0;
        psoDesc.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;

        psoDesc.DepthStencilState.DepthEnable = desc.depthStencil.depthTestEnable;
        psoDesc.DepthStencilState.DepthWriteMask = desc.depthStencil.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        psoDesc.DepthStencilState.DepthFunc = toD3D12Compare(desc.depthStencil.depthCompareOp);
        psoDesc.DepthStencilState.StencilEnable = desc.depthStencil.stencilTestEnable;
        psoDesc.DepthStencilState.StencilReadMask = static_cast<UINT8>(desc.depthStencil.front.compareMask & 0xFFu);
        psoDesc.DepthStencilState.StencilWriteMask = static_cast<UINT8>(desc.depthStencil.front.writeMask & 0xFFu);
        psoDesc.DepthStencilState.FrontFace = toD3D12StencilFace(desc.depthStencil.front);
        psoDesc.DepthStencilState.BackFace = toD3D12StencilFace(desc.depthStencil.back);

        psoDesc.InputLayout = {elements.empty() ? nullptr : elements.data(), static_cast<UINT>(elements.size())};
        psoDesc.PrimitiveTopologyType = toD3D12TopologyType(desc.inputAssembly);
        psoDesc.NumRenderTargets = static_cast<UINT>(std::min<size_t>(D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT, desc.colorFormats.size()));
        for (UINT i = 0; i < psoDesc.NumRenderTargets; ++i) {
            psoDesc.RTVFormats[i] = toDxgiFormat(desc.colorFormats[i]);
        }
        psoDesc.DSVFormat = desc.depthStencilFormat == Format::Undefined ? DXGI_FORMAT_UNKNOWN : toDsvFormat(desc.depthStencilFormat);
        psoDesc.SampleDesc.Count = static_cast<UINT>(desc.multisample.samples);
        psoDesc.SampleDesc.Quality = 0;

        if (const Impl::PipelineCacheResource* cache = getRenderResource(impl_->pipelineCaches, desc.cache)) {
            if (!cache->desc.initialData.empty()) {
                psoDesc.CachedPSO.pCachedBlob = cache->desc.initialData.data();
                psoDesc.CachedPSO.CachedBlobSizeInBytes = cache->desc.initialData.size();
            }
        }

        throwIfFailed(impl_->device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&resource.pipelineState)), "CreateGraphicsPipelineState failed");
        impl_->setDebugName(resource.pipelineState.Get(), desc.debugName);
    } catch (...) {
        for (ShaderHandle handle : temporaryShaders) {
            destroy(handle);
        }
        throw;
    }

    for (ShaderHandle handle : temporaryShaders) {
        destroy(handle);
    }
    return makeRenderHandle<PipelineHandle>(impl_->pipelines, std::move(resource));
}

PipelineHandle D3D12Renderer::createComputePipeline(const ComputePipelineDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D12Renderer is not initialized");
    }

    const Impl::PipelineLayoutResource* layout = getRenderResource(impl_->pipelineLayouts, desc.layout);
    if (layout == nullptr || !layout->rootSignature) {
        throw std::runtime_error("ComputePipelineDesc::layout is invalid");
    }

    ShaderHandle shaderHandle = createShaderModule(desc.shader);
    const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, shaderHandle);
    if (shader == nullptr || shader->bytecode.empty()) {
        destroy(shaderHandle);
        throw std::runtime_error("ComputePipelineDesc requires a compute shader");
    }

    Impl::PipelineResource resource{};
    resource.compute = true;
    resource.rootSignature = layout->rootSignature;

    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = layout->rootSignature.Get();
    psoDesc.CS = {shader->bytecode.data(), shader->bytecode.size()};
    if (const Impl::PipelineCacheResource* cache = getRenderResource(impl_->pipelineCaches, desc.cache)) {
        if (!cache->desc.initialData.empty()) {
            psoDesc.CachedPSO.pCachedBlob = cache->desc.initialData.data();
            psoDesc.CachedPSO.CachedBlobSizeInBytes = cache->desc.initialData.size();
        }
    }

    throwIfFailed(impl_->device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&resource.pipelineState)), "CreateComputePipelineState failed");
    impl_->setDebugName(resource.pipelineState.Get(), desc.debugName);
    destroy(shaderHandle);
    return makeRenderHandle<PipelineHandle>(impl_->pipelines, std::move(resource));
}

// D3D12 pipelines 片段负责把 GraphicsPipelineDesc/ComputePipelineDesc 翻译成 RootSignature + PSO。
// 和 D3D11 的“多个状态对象逐项绑定”不同，D3D12 创建 PSO 后，绘制时只需要 SetPipelineState，
// 但前提是 command list 还要正确绑定 root signature、descriptor heap 和具体 descriptor table。
