BufferHandle D3D11Renderer::createBuffer(const BufferDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }
    if (desc.size == 0) {
        throw std::runtime_error("BufferDesc::size must be greater than zero");
    }

    D3D11_BUFFER_DESC bufferDesc{};
    bufferDesc.ByteWidth = static_cast<UINT>(desc.size);
    if (hasAny(desc.usage, BufferUsage::Uniform)) {
        bufferDesc.ByteWidth = (bufferDesc.ByteWidth + 15u) & ~15u;
    }
    if (hasAny(desc.usage, BufferUsage::Storage)) {
        bufferDesc.ByteWidth = (bufferDesc.ByteWidth + 3u) & ~3u;
        bufferDesc.MiscFlags |= D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    }
    bufferDesc.Usage = toD3DUsage(desc.memoryUsage, desc.persistentlyMapped);
    bufferDesc.BindFlags = toBufferBindFlags(desc.usage, desc.memoryUsage);
    bufferDesc.CPUAccessFlags = toCpuAccessFlags(desc.memoryUsage, desc.persistentlyMapped);

    Impl::BufferResource resource{};
    resource.desc = desc;
    throwIfFailed(impl_->device->CreateBuffer(&bufferDesc, nullptr, &resource.buffer), "ID3D11Device::CreateBuffer failed");
    impl_->setDebugName(resource.buffer.Get(), desc.debugName);
    return makeRenderHandle<BufferHandle>(impl_->buffers, std::move(resource));
}

// D3D11 texture 分成 Texture1D/2D/3D 三套接口；这里用 TextureDesc::dimension 选择创建函数。
// 深度格式会先创建成 typeless，之后再由 SRV/DSV 决定“采样视图”还是“深度视图”。
TextureHandle D3D11Renderer::createTexture(const TextureDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    const DXGI_FORMAT format = isDepthFormat(desc.format) || hasStencilFormat(desc.format)
        ? toTypelessDepthFormat(desc.format)
        : toDxgiFormat(desc.format);
    if (format == DXGI_FORMAT_UNKNOWN) {
        throw std::runtime_error("TextureDesc::format is not supported by D3D11");
    }

    Impl::TextureResource resource{};
    resource.desc = desc;
    resource.currentState = desc.initialState;

    if (desc.dimension == TextureDimension::Texture1D) {
        D3D11_TEXTURE1D_DESC textureDesc{};
        textureDesc.Width = desc.extent.width;
        textureDesc.MipLevels = desc.mipLevels;
        textureDesc.ArraySize = desc.arrayLayers;
        textureDesc.Format = format;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = toTextureBindFlags(desc.usage, MemoryUsage::GpuOnly);
        textureDesc.MiscFlags = hasAny(desc.flags, TextureCreateFlags::GenerateMips) ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        ComPtr<ID3D11Texture1D> texture;
        throwIfFailed(impl_->device->CreateTexture1D(&textureDesc, nullptr, &texture), "ID3D11Device::CreateTexture1D failed");
        impl_->setDebugName(texture.Get(), desc.debugName);
        resource.resource = texture;
    } else if (desc.dimension == TextureDimension::Texture3D) {
        D3D11_TEXTURE3D_DESC textureDesc{};
        textureDesc.Width = desc.extent.width;
        textureDesc.Height = desc.extent.height;
        textureDesc.Depth = desc.extent.depth;
        textureDesc.MipLevels = desc.mipLevels;
        textureDesc.Format = format;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = toTextureBindFlags(desc.usage, MemoryUsage::GpuOnly);
        textureDesc.MiscFlags = hasAny(desc.flags, TextureCreateFlags::GenerateMips) ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
        ComPtr<ID3D11Texture3D> texture;
        throwIfFailed(impl_->device->CreateTexture3D(&textureDesc, nullptr, &texture), "ID3D11Device::CreateTexture3D failed");
        impl_->setDebugName(texture.Get(), desc.debugName);
        resource.resource = texture;
    } else {
        D3D11_TEXTURE2D_DESC textureDesc{};
        textureDesc.Width = desc.extent.width;
        textureDesc.Height = desc.extent.height;
        textureDesc.MipLevels = desc.mipLevels;
        textureDesc.ArraySize = desc.arrayLayers;
        textureDesc.Format = format;
        textureDesc.SampleDesc.Count = toSampleCount(desc.samples);
        textureDesc.SampleDesc.Quality = 0;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;
        textureDesc.BindFlags = toTextureBindFlags(desc.usage, MemoryUsage::GpuOnly);
        if (hasAny(desc.flags, TextureCreateFlags::CubeCompatible)) textureDesc.MiscFlags |= D3D11_RESOURCE_MISC_TEXTURECUBE;
        if (hasAny(desc.flags, TextureCreateFlags::GenerateMips)) textureDesc.MiscFlags |= D3D11_RESOURCE_MISC_GENERATE_MIPS;
        ComPtr<ID3D11Texture2D> texture;
        throwIfFailed(impl_->device->CreateTexture2D(&textureDesc, nullptr, &texture), "ID3D11Device::CreateTexture2D failed");
        impl_->setDebugName(texture.Get(), desc.debugName);
        resource.resource = texture;
    }

    return makeRenderHandle<TextureHandle>(impl_->textures, std::move(resource));
}

// D3D11 的 view 是资源用途的入口：SRV 给 shader 读，RTV 给 color attachment 写，DSV 给深度
// 测试，UAV 给 compute/像素 shader 随机读写。一个 TextureViewDesc 可能按 usage 创建多个 view。
TextureViewHandle D3D11Renderer::createTextureView(const TextureViewDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    const Impl::TextureResource* texture = getRenderResource(impl_->textures, desc.texture);
    if (texture == nullptr || texture->resource == nullptr) {
        throw std::runtime_error("TextureViewDesc::texture is invalid");
    }

    const Format viewFormat = desc.format == Format::Undefined ? texture->desc.format : desc.format;
    Impl::TextureViewResource resource{};
    resource.desc = desc;

    if (hasAny(texture->desc.usage, TextureUsage::Sampled)) {
        const D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = makeTextureSrvDesc(texture->desc, desc, viewFormat);
        throwIfFailed(impl_->device->CreateShaderResourceView(texture->resource.Get(), &srvDesc, &resource.srv), "CreateShaderResourceView failed");
    }

    const bool wantsDepth = desc.aspect == TextureAspect::Depth ||
                            desc.aspect == TextureAspect::Stencil ||
                            desc.aspect == TextureAspect::All ||
                            isDepthFormat(viewFormat) ||
                            hasStencilFormat(viewFormat);
    if (hasAny(texture->desc.usage, TextureUsage::DepthStencilAttachment) && wantsDepth) {
        const D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = makeDsvDesc(texture->desc, desc, viewFormat);
        throwIfFailed(impl_->device->CreateDepthStencilView(texture->resource.Get(), &dsvDesc, &resource.dsv), "CreateDepthStencilView failed");
    }

    if (hasAny(texture->desc.usage, TextureUsage::ColorAttachment) || hasAny(texture->desc.usage, TextureUsage::Present)) {
        const D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = makeRtvDesc(texture->desc, desc, viewFormat);
        throwIfFailed(impl_->device->CreateRenderTargetView(texture->resource.Get(), &rtvDesc, &resource.rtv), "CreateRenderTargetView failed");
    }

    if (hasAny(texture->desc.usage, TextureUsage::Storage)) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.Format = toDxgiFormat(viewFormat);
        if (texture->desc.dimension == TextureDimension::Texture3D) {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
            uavDesc.Texture3D.MipSlice = desc.baseMipLevel;
            uavDesc.Texture3D.FirstWSlice = desc.baseArrayLayer;
            uavDesc.Texture3D.WSize = desc.arrayLayerCount;
        } else if (texture->desc.arrayLayers > 1) {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.MipSlice = desc.baseMipLevel;
            uavDesc.Texture2DArray.FirstArraySlice = desc.baseArrayLayer;
            uavDesc.Texture2DArray.ArraySize = desc.arrayLayerCount;
        } else {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = desc.baseMipLevel;
        }
        throwIfFailed(impl_->device->CreateUnorderedAccessView(texture->resource.Get(), &uavDesc, &resource.uav), "CreateUnorderedAccessView failed");
    }

    const TextureViewHandle handle = makeRenderHandle<TextureViewHandle>(impl_->textureViews, std::move(resource));
    Impl::TextureViewResource& stored = impl_->textureViews.back();
    impl_->setDebugName(stored.srv.Get(), desc.debugName + ".SRV");
    impl_->setDebugName(stored.rtv.Get(), desc.debugName + ".RTV");
    impl_->setDebugName(stored.dsv.Get(), desc.debugName + ".DSV");
    impl_->setDebugName(stored.uav.Get(), desc.debugName + ".UAV");
    return handle;
}

// SamplerState 描述采样过滤、寻址、比较采样等规则。和 Vulkan 一样，sampler 不拥有纹理；
// 具体纹理通过 SRV 绑定，采样规则通过 sampler slot 绑定。
SamplerHandle D3D11Renderer::createSampler(const SamplerDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    D3D11_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = toD3DFilter(desc);
    samplerDesc.AddressU = toD3DAddressMode(desc.addressU);
    samplerDesc.AddressV = toD3DAddressMode(desc.addressV);
    samplerDesc.AddressW = toD3DAddressMode(desc.addressW);
    samplerDesc.MipLODBias = desc.mipLodBias;
    samplerDesc.MaxAnisotropy = static_cast<UINT>(std::clamp(desc.maxAnisotropy, 1.0F, 16.0F));
    samplerDesc.ComparisonFunc = toD3DCompare(desc.compareOp);
    samplerDesc.MinLOD = desc.minLod;
    samplerDesc.MaxLOD = desc.maxLod;
    switch (desc.borderColor) {
    case BorderColor::TransparentBlack:
        samplerDesc.BorderColor[0] = 0.0F;
        samplerDesc.BorderColor[1] = 0.0F;
        samplerDesc.BorderColor[2] = 0.0F;
        samplerDesc.BorderColor[3] = 0.0F;
        break;
    case BorderColor::OpaqueBlack:
        samplerDesc.BorderColor[3] = 1.0F;
        break;
    case BorderColor::OpaqueWhite:
        samplerDesc.BorderColor[0] = 1.0F;
        samplerDesc.BorderColor[1] = 1.0F;
        samplerDesc.BorderColor[2] = 1.0F;
        samplerDesc.BorderColor[3] = 1.0F;
        break;
    }

    Impl::SamplerResource resource{};
    resource.desc = desc;
    throwIfFailed(impl_->device->CreateSamplerState(&samplerDesc, &resource.sampler), "CreateSamplerState failed");
    impl_->setDebugName(resource.sampler.Get(), desc.debugName);
    return makeRenderHandle<SamplerHandle>(impl_->samplers, std::move(resource));
}

// D3D11 可以直接从 HLSL source/file 编译，也可以接收已编译 DXBC bytecode。
// 这里集中处理 entry point、target profile、宏和编译选项，返回 create shader object 所需字节码。
static std::vector<std::byte> compileHlsl(const ShaderDesc& desc) {
    if (!desc.bytecode.empty()) {
        return desc.bytecode;
    }
    if (desc.language != ShaderLanguage::Unknown && desc.language != ShaderLanguage::HLSL) {
        throw std::runtime_error("D3D11 shader modules require HLSL source or compiled DXBC bytecode");
    }

    const std::string profile = desc.compileOptions.targetProfile.empty()
        ? defaultProfileForStage(desc.stage)
        : desc.compileOptions.targetProfile;
    if (profile.empty()) {
        throw std::runtime_error("ShaderDesc::stage is not supported by D3D11");
    }

    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(desc.compileOptions.defines.size() + 1);
    for (const ShaderDefine& define : desc.compileOptions.defines) {
        macros.push_back({define.name.c_str(), define.value.c_str()});
    }
    macros.push_back({nullptr, nullptr});

    UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
    if (desc.compileOptions.enableDebugInfo) flags |= D3DCOMPILE_DEBUG;
    if (desc.compileOptions.optimize)        flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
    else flags |= D3DCOMPILE_SKIP_OPTIMIZATION;
    if (desc.compileOptions.treatWarningsAsErrors) flags |= D3DCOMPILE_WARNINGS_ARE_ERRORS;

    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    HRESULT hr = E_FAIL;
    const char* entryPoint = desc.entryPoint.empty() ? "main" : desc.entryPoint.c_str();
    if (!desc.source.empty()) {
        hr = D3DCompile(
            desc.source.data(),
            desc.source.size(),
            desc.debugName.empty() ? nullptr : desc.debugName.c_str(),
            macros.data(),
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint,
            profile.c_str(),
            flags,
            0,
            &bytecode,
            &errors);
    } else if (!desc.filePath.empty()) {
        const std::wstring path = toWideString(desc.filePath);
        hr = D3DCompileFromFile(
            path.c_str(),
            macros.data(),
            D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entryPoint,
            profile.c_str(),
            flags,
            0,
            &bytecode,
            &errors);
    } else {
        throw std::runtime_error("ShaderDesc requires bytecode, source, or filePath");
    }

    if (FAILED(hr)) {
        std::string message = "D3DCompile failed";
        if (errors) {
            message += ": ";
            message.append(static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize());
        }
        throw std::runtime_error(message);
    }

    std::vector<std::byte> result(bytecode->GetBufferSize());
    std::memcpy(result.data(), bytecode->GetBufferPointer(), result.size());
    return result;
}

// ShaderResource 保存具体 stage 的 D3D11 shader 对象，同时保留 bytecode。
// 顶点 shader 的 bytecode 后续还要用于 CreateInputLayout，因为 D3D11 需要用它校验顶点输入签名。
ShaderHandle D3D11Renderer::createShaderModule(const ShaderDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    Impl::ShaderResource resource{};
    resource.desc = desc;
    resource.bytecode = compileHlsl(desc);
    if (resource.bytecode.empty()) {
        throw std::runtime_error("ShaderDesc has no bytecode");
    }

    const void* data = resource.bytecode.data();
    const SIZE_T size = resource.bytecode.size();
    switch (desc.stage) {
    case ShaderStage::Vertex:
        throwIfFailed(impl_->device->CreateVertexShader(data, size, nullptr, &resource.vertexShader), "CreateVertexShader failed");
        impl_->setDebugName(resource.vertexShader.Get(), desc.debugName);
        break;
    case ShaderStage::TessControl:
        throwIfFailed(impl_->device->CreateHullShader(data, size, nullptr, &resource.hullShader), "CreateHullShader failed");
        impl_->setDebugName(resource.hullShader.Get(), desc.debugName);
        break;
    case ShaderStage::TessEvaluation:
        throwIfFailed(impl_->device->CreateDomainShader(data, size, nullptr, &resource.domainShader), "CreateDomainShader failed");
        impl_->setDebugName(resource.domainShader.Get(), desc.debugName);
        break;
    case ShaderStage::Geometry:
        throwIfFailed(impl_->device->CreateGeometryShader(data, size, nullptr, &resource.geometryShader), "CreateGeometryShader failed");
        impl_->setDebugName(resource.geometryShader.Get(), desc.debugName);
        break;
    case ShaderStage::Fragment:
        throwIfFailed(impl_->device->CreatePixelShader(data, size, nullptr, &resource.pixelShader), "CreatePixelShader failed");
        impl_->setDebugName(resource.pixelShader.Get(), desc.debugName);
        break;
    case ShaderStage::Compute:
        throwIfFailed(impl_->device->CreateComputeShader(data, size, nullptr, &resource.computeShader), "CreateComputeShader failed");
        impl_->setDebugName(resource.computeShader.Get(), desc.debugName);
        break;
    default:
        throw std::runtime_error("ShaderDesc::stage is not supported by D3D11");
    }

    return makeRenderHandle<ShaderHandle>(impl_->shaders, std::move(resource));
}

// D3D11 没有原生 DescriptorSetLayout；这里保存 layout 描述，用来在 createBindGroup 时校验
// binding 是否存在、可见 shader stage 是哪些、storage 是否可写。
BindGroupLayoutHandle D3D11Renderer::createBindGroupLayout(const BindGroupLayoutDesc& desc) {
    Impl::BindGroupLayoutResource resource{};
    resource.desc = desc;
    return makeRenderHandle<BindGroupLayoutHandle>(impl_->bindGroupLayouts, std::move(resource));
}

// D3D11 的 BindGroup 是“延迟绑定记录”：创建时把统一 ResourceBinding 解析成 COM view/state，
// 绘制/dispatch 时 applyBindGroup 再把这些对象设置到 VS/PS/CS 等具体 shader stage。
BindGroupHandle D3D11Renderer::createBindGroup(const BindGroupDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("D3D11Renderer is not initialized");
    }

    const Impl::BindGroupLayoutResource* layout = getRenderResource(impl_->bindGroupLayouts, desc.layout);
    if (layout == nullptr) {
        throw std::runtime_error("BindGroupDesc::layout is invalid");
    }

    Impl::BindGroupResource resource{};
    resource.desc = desc;
    resource.bindings.reserve(desc.bindings.size());

    for (const ResourceBinding& binding : desc.bindings) {
        const BindGroupLayoutEntry* layoutEntry = impl_->findLayoutEntry(*layout, binding.binding);
        if (layoutEntry == nullptr) {
            throw std::runtime_error("ResourceBinding has no matching BindGroupLayoutEntry");
        }

        Impl::ResolvedBinding resolved{};
        resolved.slot = binding.binding;
        resolved.type = binding.type;
        resolved.visibility = layoutEntry->visibility;

        if (binding.type == BindingType::UniformBuffer) {
            if (binding.buffer.offset != 0) {
                throw std::runtime_error("D3D11 constant buffer offsets require D3D11.1 and are not implemented");
            }
            const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, binding.buffer.buffer);
            if (buffer == nullptr || !buffer->buffer) {
                throw std::runtime_error("ResourceBinding uniform buffer is invalid");
            }
            resolved.buffer = buffer->buffer;
        } else if (binding.type == BindingType::StorageBuffer) {
            const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, binding.buffer.buffer);
            if (buffer == nullptr || !buffer->buffer) {
                throw std::runtime_error("ResourceBinding storage buffer is invalid");
            }
            const u64 rangeSize = binding.buffer.size == WHOLE_SIZE
                ? buffer->desc.size - binding.buffer.offset
                : binding.buffer.size;
            if ((binding.buffer.offset % 4) != 0 || (rangeSize % 4) != 0) {
                throw std::runtime_error("D3D11 raw buffer views require 4-byte aligned offset and size");
            }

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            srvDesc.BufferEx.FirstElement = static_cast<UINT>(binding.buffer.offset / 4);
            srvDesc.BufferEx.NumElements = static_cast<UINT>(rangeSize / 4);
            srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
            throwIfFailed(impl_->device->CreateShaderResourceView(buffer->buffer.Get(), &srvDesc, &resolved.srv), "Create buffer SRV failed");

            if (layoutEntry->writable) {
                D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
                uavDesc.Buffer.FirstElement = static_cast<UINT>(binding.buffer.offset / 4);
                uavDesc.Buffer.NumElements = static_cast<UINT>(rangeSize / 4);
                uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
                throwIfFailed(impl_->device->CreateUnorderedAccessView(buffer->buffer.Get(), &uavDesc, &resolved.uav), "Create buffer UAV failed");
            }
        } else if (binding.type == BindingType::SampledTexture || binding.type == BindingType::StorageTexture || binding.type == BindingType::CombinedTextureSampler) {
            const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, binding.texture.view);
            if (view == nullptr) {
                throw std::runtime_error("ResourceBinding texture view is invalid");
            }
            resolved.srv = view->srv;
            resolved.uav = view->uav;
            if (binding.type == BindingType::CombinedTextureSampler) {
                const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
                if (sampler == nullptr || !sampler->sampler) {
                    throw std::runtime_error("CombinedTextureSampler requires a valid sampler");
                }
                resolved.sampler = sampler->sampler;
            }
        } else if (binding.type == BindingType::Sampler) {
            const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
            if (sampler == nullptr || !sampler->sampler) {
                throw std::runtime_error("ResourceBinding sampler is invalid");
            }
            resolved.sampler = sampler->sampler;
        } else if (binding.type == BindingType::PushConstant) {
            continue;
        } else {
            throw std::runtime_error("ResourceBinding type is not supported by D3D11");
        }

        resource.bindings.push_back(std::move(resolved));
    }

    return makeRenderHandle<BindGroupHandle>(impl_->bindGroups, std::move(resource));
}

// PipelineLayout 在 D3D11 中主要用于统一抽象校验：确认它引用的 bind group layout 都存在。
// 真正的资源槽位绑定在 applyBindGroup 时按每个 binding 的 slot 和 visibility 执行。
PipelineLayoutHandle D3D11Renderer::createPipelineLayout(const PipelineLayoutDesc& desc) {
    for (BindGroupLayoutHandle handle : desc.bindGroupLayouts) {
        if (getRenderResource(impl_->bindGroupLayouts, handle) == nullptr) {
            throw std::runtime_error("PipelineLayoutDesc contains an invalid bind group layout");
        }
    }
    Impl::PipelineLayoutResource resource{};
    resource.desc = desc;
    return makeRenderHandle<PipelineLayoutHandle>(impl_->pipelineLayouts, std::move(resource));
}

// D3D11 没有 Vulkan/D3D12 那种 pipeline cache；保留这个资源类型是为了让统一接口完整，
// 以后接入 D3D12/Vulkan 离线缓存时，上层 API 不需要改变。
PipelineCacheHandle D3D11Renderer::createPipelineCache(const PipelineCacheDesc& desc) {
    Impl::PipelineCacheResource resource{};
    resource.desc = desc;
    return makeRenderHandle<PipelineCacheHandle>(impl_->pipelineCaches, std::move(resource));
}

// D3D11 resources 片段负责创建所有“可被 pipeline 或 pass 使用”的对象：
// buffer、texture、texture view、sampler、shader module、bind group/layout、pipeline layout/cache。
// D3D11 没有 Vulkan 的 device memory 分配步骤，CreateBuffer/CreateTexture* 会同时创建资源和隐式分配内存；
// 但 view 仍然很重要：同一 texture 可以通过 SRV/RTV/DSV/UAV 以不同用途暴露给 shader 或 render target。
// 学习时可以按“resource 本体 -> view -> bind group 绑定表”的顺序看。
