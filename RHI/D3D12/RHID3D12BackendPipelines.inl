static D3D12_DEPTH_STENCILOP_DESC toD3D12StencilFace(const RHIStencilFaceState& state) {
    D3D12_DEPTH_STENCILOP_DESC desc{};
    desc.StencilFailOp = toD3D12StencilOp(state.failOp);
    desc.StencilDepthFailOp = toD3D12StencilOp(state.depthFailOp);
    desc.StencilPassOp = toD3D12StencilOp(state.passOp);
    desc.StencilFunc = toD3D12Compare(state.compareOp);
    return desc;
}

static D3D12_LOGIC_OP toD3D12LogicOp(RHILogicOp op) {
    switch (op) {
    case RHILogicOp::Clear:        return D3D12_LOGIC_OP_CLEAR;
    case RHILogicOp::And:          return D3D12_LOGIC_OP_AND;
    case RHILogicOp::AndReverse:   return D3D12_LOGIC_OP_AND_REVERSE;
    case RHILogicOp::Copy:         return D3D12_LOGIC_OP_COPY;
    case RHILogicOp::AndInverted:  return D3D12_LOGIC_OP_AND_INVERTED;
    case RHILogicOp::NoOp:         return D3D12_LOGIC_OP_NOOP;
    case RHILogicOp::Xor:          return D3D12_LOGIC_OP_XOR;
    case RHILogicOp::Or:           return D3D12_LOGIC_OP_OR;
    case RHILogicOp::Nor:          return D3D12_LOGIC_OP_NOR;
    case RHILogicOp::Equivalent:   return D3D12_LOGIC_OP_EQUIV;
    case RHILogicOp::Invert:       return D3D12_LOGIC_OP_INVERT;
    case RHILogicOp::OrReverse:    return D3D12_LOGIC_OP_OR_REVERSE;
    case RHILogicOp::CopyInverted: return D3D12_LOGIC_OP_COPY_INVERTED;
    case RHILogicOp::OrInverted:   return D3D12_LOGIC_OP_OR_INVERTED;
    case RHILogicOp::Nand:         return D3D12_LOGIC_OP_NAND;
    case RHILogicOp::Set:          return D3D12_LOGIC_OP_SET;
    }
    return D3D12_LOGIC_OP_COPY;
}

// D3D12 graphics pipeline 是一个真正的 PSO。它会固定 shader bytecode、input layout、
// rasterizer/depth/blend、RTV/DSV 格式、MSAA 等状态。后续 command list 只需要 SetPipelineState。
RHIPipeline RHID3D12Backend::createGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12Backend is not initialized");
    }

    const Impl::PipelineLayoutResource* layout = getRenderResource(impl_->pipelineLayouts, desc.layout);
    if (layout == nullptr || !layout->rootSignature) {
        throw std::runtime_error("RHIGraphicsPipelineDesc::layout is invalid");
    }

    Impl::PipelineResource resource{};
    resource.compute = false;
    resource.topology = toD3D12Topology(desc.inputAssembly);
    resource.stencilRef = desc.depthStencil.front.reference;
    resource.blendConstants = desc.blend.blendConstants;
    resource.rootSignature = layout->rootSignature;

    std::vector<RHIShader> temporaryShaders;
    RHIShader vertexShaderHandle{};
    RHIShader hullShaderHandle{};
    RHIShader domainShaderHandle{};
    RHIShader geometryShaderHandle{};
    RHIShader pixelShaderHandle{};

    try {
        for (const RHIShaderDesc& shaderDesc : desc.shaders) {
            RHIShader handle = createShaderModule(shaderDesc);
            temporaryShaders.push_back(handle);
            switch (shaderDesc.stage) {
            case RHIShaderStage::Vertex:
                vertexShaderHandle = handle;
                break;
            case RHIShaderStage::TessControl:
                hullShaderHandle = handle;
                break;
            case RHIShaderStage::TessEvaluation:
                domainShaderHandle = handle;
                break;
            case RHIShaderStage::Geometry:
                geometryShaderHandle = handle;
                break;
            case RHIShaderStage::Fragment:
                pixelShaderHandle = handle;
                break;
            default:
                throw std::runtime_error("Graphics pipeline contains a non-graphics shader stage");
            }
        }

        const Impl::ShaderResource* vertexShader = getRenderResource(impl_->shaders, vertexShaderHandle);
        if (vertexShader == nullptr || vertexShader->bytecode.empty()) {
            throw std::runtime_error("RHIGraphicsPipelineDesc requires a vertex shader");
        }

        size_t vertexAttributeCount = 0;
        for (const RHIVertexBufferLayoutDesc& vertexBuffer : desc.vertexBuffers) {
            vertexAttributeCount += vertexBuffer.attributes.size();
        }

        std::vector<std::string> semanticNames;
        semanticNames.reserve(vertexAttributeCount);
        std::vector<D3D12_INPUT_ELEMENT_DESC> elements;
        elements.reserve(vertexAttributeCount);
        for (const RHIVertexBufferLayoutDesc& vertexBuffer : desc.vertexBuffers) {
            for (const RHIVertexAttributeDesc& attribute : vertexBuffer.attributes) {
                semanticNames.push_back(attribute.semanticName.empty() ? "TEXCOORD" : attribute.semanticName);
                D3D12_INPUT_ELEMENT_DESC element{};
                element.SemanticName = semanticNames.back().c_str();
                element.SemanticIndex = attribute.semanticName.empty() ? attribute.location : attribute.semanticIndex;
                element.Format = toD3D12VertexFormat(attribute.format);
                element.InputSlot = attribute.binding;
                element.AlignedByteOffset = static_cast<UINT>(attribute.offset);
                element.InputSlotClass = vertexBuffer.inputRate == RHIVertexInputRate::PerInstance
                    ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                    : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
                element.InstanceDataStepRate = vertexBuffer.inputRate == RHIVertexInputRate::PerInstance ? vertexBuffer.stepRate : 0;
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
            const RHIColorBlendAttachmentState src = desc.blend.attachments.empty() ? RHIColorBlendAttachmentState{} : desc.blend.attachments[i];
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
        psoDesc.RasterizerState.FillMode = desc.raster.polygonMode == RHIPolygonMode::Line ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
        switch (desc.raster.cullMode) {
        case RHICullMode::None:         psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; break;
        case RHICullMode::Front:        psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT; break;
        case RHICullMode::Back:
        case RHICullMode::FrontAndBack: psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK; break;
        }
        psoDesc.RasterizerState.FrontCounterClockwise = desc.raster.frontFace == RHIFrontFace::CounterClockwise;
        psoDesc.RasterizerState.DepthBias = static_cast<INT>(desc.raster.depthBiasConstantFactor);
        psoDesc.RasterizerState.DepthBiasClamp = desc.raster.depthBiasClamp;
        psoDesc.RasterizerState.SlopeScaledDepthBias = desc.raster.depthBiasSlopeFactor;
        psoDesc.RasterizerState.DepthClipEnable = !desc.raster.depthClampEnable;
        psoDesc.RasterizerState.MultisampleEnable = desc.multisample.samples != RHISampleCount::Count1;
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
        psoDesc.DSVFormat = desc.depthStencilFormat == RHIFormat::Undefined ? DXGI_FORMAT_UNKNOWN : toDsvFormat(desc.depthStencilFormat);
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

RHIPipeline RHID3D12Backend::createComputePipeline(const RHIComputePipelineDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12Backend is not initialized");
    }

    const Impl::PipelineLayoutResource* layout = getRenderResource(impl_->pipelineLayouts, desc.layout);
    if (layout == nullptr || !layout->rootSignature) {
        throw std::runtime_error("RHIComputePipelineDesc::layout is invalid");
    }

    RHIShader shaderHandle = createShaderModule(desc.shader);
    const Impl::ShaderResource* shader = getRenderResource(impl_->shaders, shaderHandle);
    if (shader == nullptr || shader->bytecode.empty()) {
        destroy(shaderHandle);
        throw std::runtime_error("RHIComputePipelineDesc requires a compute shader");
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
    return makeRenderHandle<RHIPipeline>(impl_->pipelines, std::move(resource));
}

// D3D12 pipelines 片段负责把 RHIGraphicsPipelineDesc/RHIComputePipelineDesc 翻译成 RootSignature + PSO。
// 和 D3D11 的“多个状态对象逐项绑定”不同，D3D12 创建 PSO 后，绘制时只需要 SetPipelineState，
// 但前提是 command list 还要正确绑定 root signature、descriptor heap 和具体 descriptor table。
