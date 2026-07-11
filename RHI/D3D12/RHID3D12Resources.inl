#pragma once

#include "RHID3D12Private.inl"

namespace rhi {

RHIBuffer RHID3D12::createBuffer(const RHIBufferDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }
    if (desc.size == 0) {
        throw std::runtime_error("RHIBufferDesc::size must be greater than zero");
    }

    const D3D12_HEAP_TYPE heapType = toD3D12HeapType(desc.memoryUsage);
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = heapType;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc{};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Alignment = 0;
    bufferDesc.Width = desc.size;
    if (RHIHasAny(desc.usage, RHIBufferUsage::Uniform)) {
        bufferDesc.Width = alignUp<UINT64>(bufferDesc.Width, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    }
    if (RHIHasAny(desc.usage, RHIBufferUsage::Storage)) {
        bufferDesc.Width = alignUp<UINT64>(bufferDesc.Width, D3D12_RAW_UAV_SRV_BYTE_ALIGNMENT);
    }
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.SampleDesc.Quality = 0;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    bufferDesc.Flags = toBufferResourceFlags(desc.usage, desc.memoryUsage);

    Impl::BufferResource resource{};
    resource.desc = desc;
    resource.currentState = initialBufferState(desc.memoryUsage, RHIResourceState::Common);
    throwIfFailed(
        impl_->device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            resource.currentState,
            nullptr,
            IID_PPV_ARGS(&resource.resource)),
        "ID3D12Device::CreateCommittedResource buffer failed");
    impl_->setDebugName(resource.resource.Get(), desc.debugName);

    if (desc.persistentlyMapped || desc.memoryUsage == RHIMemoryUsage::CpuToGpu || desc.memoryUsage == RHIMemoryUsage::CpuOnly || desc.memoryUsage == RHIMemoryUsage::GpuToCpu) {
        D3D12_RANGE readRange{0, 0};
        throwIfFailed(resource.resource->Map(0, &readRange, &resource.mappedData), "ID3D12Resource::Map buffer failed");
    }

    return makeRenderHandle<RHIBuffer>(impl_->buffers, std::move(resource));
}

// D3D12 texture 创建时要同时决定 resource flags、初始 state 和可选 clear value。
// 深度格式仍然使用 typeless 存储格式创建，再由 SRV/DSV 选择读取或深度测试解释方式。
RHITexture RHID3D12::createTexture(const RHITextureDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }

    const DXGI_FORMAT storageFormat = isDepthFormat(desc.format) || hasStencilFormat(desc.format)
        ? toTypelessDepthFormat(desc.format)
        : toDxgiFormat(desc.format);
    if (storageFormat == DXGI_FORMAT_UNKNOWN) {
        throw std::runtime_error("RHITextureDesc::format is not supported by D3D12");
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC textureDesc{};
    textureDesc.Dimension = desc.dimension == RHITextureDimension::Texture1D
        ? D3D12_RESOURCE_DIMENSION_TEXTURE1D
        : (desc.dimension == RHITextureDimension::Texture3D ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D);
    textureDesc.Width = desc.extent.width;
    textureDesc.Height = desc.dimension == RHITextureDimension::Texture1D ? 1 : desc.extent.height;
    textureDesc.DepthOrArraySize = static_cast<UINT16>(desc.dimension == RHITextureDimension::Texture3D ? desc.extent.depth : desc.arrayLayers);
    textureDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
    textureDesc.Format = storageFormat;
    textureDesc.SampleDesc.Count = static_cast<UINT>(desc.samples);
    textureDesc.SampleDesc.Quality = 0;
    textureDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    textureDesc.Flags = toTextureResourceFlags(desc.usage);

    D3D12_CLEAR_VALUE clearValue{};
    D3D12_CLEAR_VALUE* clearValuePtr = nullptr;
    if (RHIHasAny(desc.usage, RHITextureUsage::DepthStencilAttachment)) {
        clearValue.Format = toDsvFormat(desc.format);
        clearValue.DepthStencil.Depth = 1.0F;
        clearValue.DepthStencil.Stencil = 0;
        clearValuePtr = &clearValue;
    } else if (RHIHasAny(desc.usage, RHITextureUsage::ColorAttachment) || RHIHasAny(desc.usage, RHITextureUsage::Present)) {
        clearValue.Format = toDxgiFormat(desc.format);
        clearValue.Color[0] = 0.0F;
        clearValue.Color[1] = 0.0F;
        clearValue.Color[2] = 0.0F;
        clearValue.Color[3] = 1.0F;
        clearValuePtr = &clearValue;
    }

    Impl::TextureResource resource{};
    resource.desc = desc;
    resource.currentState = desc.initialState == RHIResourceState::Undefined
        ? D3D12_RESOURCE_STATE_COMMON
        : toD3D12ResourceStates(desc.initialState);
    throwIfFailed(
        impl_->device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            resource.currentState,
            clearValuePtr,
            IID_PPV_ARGS(&resource.resource)),
        "ID3D12Device::CreateCommittedResource texture failed");
    impl_->setDebugName(resource.resource.Get(), desc.debugName);
    return makeRenderHandle<RHITexture>(impl_->textures, std::move(resource));
}

RHITextureView RHID3D12::createTextureView(const RHITextureViewDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }

    const Impl::TextureResource* texture = getRenderResource(impl_->textures, desc.texture);
    if (texture == nullptr || texture->resource == nullptr) {
        throw std::runtime_error("RHITextureViewDesc::texture is invalid");
    }

    const RHIFormat viewFormat = desc.format == RHIFormat::Undefined ? texture->desc.format : desc.format;
    Impl::TextureViewResource resource{};
    resource.desc = desc;

    if (RHIHasAny(texture->desc.usage, RHITextureUsage::Sampled)) {
        const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = makeTextureSrvDesc(texture->desc, desc, viewFormat);
        resource.srv = impl_->allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        impl_->device->CreateShaderResourceView(texture->resource.Get(), &srvDesc, resource.srv.handle);
    }

    const bool wantsDepth = RHIHasAny(
        desc.aspect,
        RHITextureAspect::Depth | RHITextureAspect::Stencil) ||
                            isDepthFormat(viewFormat) ||
                            hasStencilFormat(viewFormat);
    if (RHIHasAny(texture->desc.usage, RHITextureUsage::DepthStencilAttachment) && wantsDepth) {
        const D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = makeDsvDesc(texture->desc, desc, viewFormat);
        if (dsvDesc.Format == DXGI_FORMAT_UNKNOWN) {
            throw std::runtime_error("RHITextureViewDesc depth format is not supported by D3D12");
        }
        resource.dsv = impl_->allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        impl_->device->CreateDepthStencilView(texture->resource.Get(), &dsvDesc, resource.dsv.handle);
    }

    if (RHIHasAny(texture->desc.usage, RHITextureUsage::ColorAttachment) || RHIHasAny(texture->desc.usage, RHITextureUsage::Present)) {
        const D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = makeRtvDesc(texture->desc, desc, viewFormat);
        resource.rtv = impl_->allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        impl_->device->CreateRenderTargetView(texture->resource.Get(), &rtvDesc, resource.rtv.handle);
    }

    if (RHIHasAny(texture->desc.usage, RHITextureUsage::Storage)) {
        if (texture->desc.samples != RHISampleCount::Count1) {
            throw std::runtime_error("D3D12 UAV texture views do not support MSAA textures");
        }
        const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = makeTextureUavDesc(texture->desc, desc, viewFormat);
        resource.uav = impl_->allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        impl_->device->CreateUnorderedAccessView(texture->resource.Get(), nullptr, &uavDesc, resource.uav.handle);
    }

    return makeRenderHandle<RHITextureView>(impl_->textureViews, std::move(resource));
}

RHISampler RHID3D12::createSampler(const RHISamplerDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }

    D3D12_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = toD3D12Filter(desc);
    samplerDesc.AddressU = toD3D12AddressMode(desc.addressU);
    samplerDesc.AddressV = toD3D12AddressMode(desc.addressV);
    samplerDesc.AddressW = toD3D12AddressMode(desc.addressW);
    samplerDesc.MipLODBias = desc.mipLodBias;
    samplerDesc.MaxAnisotropy = static_cast<UINT>(std::clamp(desc.maxAnisotropy, 1.0F, 16.0F));
    samplerDesc.ComparisonFunc = toD3D12Compare(desc.compareOp);
    samplerDesc.MinLOD = desc.minLod;
    samplerDesc.MaxLOD = desc.maxLod;
    switch (desc.borderColor) {
    case RHIBorderColor::TransparentBlack:
        samplerDesc.BorderColor[0] = 0.0F;
        samplerDesc.BorderColor[1] = 0.0F;
        samplerDesc.BorderColor[2] = 0.0F;
        samplerDesc.BorderColor[3] = 0.0F;
        break;
    case RHIBorderColor::OpaqueBlack:
        samplerDesc.BorderColor[3] = 1.0F;
        break;
    case RHIBorderColor::OpaqueWhite:
        samplerDesc.BorderColor[0] = 1.0F;
        samplerDesc.BorderColor[1] = 1.0F;
        samplerDesc.BorderColor[2] = 1.0F;
        samplerDesc.BorderColor[3] = 1.0F;
        break;
    }

    Impl::SamplerResource resource{};
    resource.desc = desc;
    resource.sampler = impl_->allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    impl_->device->CreateSampler(&samplerDesc, resource.sampler.handle);
    return makeRenderHandle<RHISampler>(impl_->samplers, std::move(resource));
}

static std::vector<std::byte> compileHlsl(const RHIShaderDesc& desc) {
    if (!desc.bytecode.empty()) {
        return desc.bytecode;
    }
    if (desc.language != RHIShaderLanguage::Unknown && desc.language != RHIShaderLanguage::HLSL) {
        throw std::runtime_error("D3D12 shader modules require HLSL source/file input or compiled DXIL/DXBC bytecode");
    }

    const std::string profile = desc.compileOptions.targetProfile.empty()
        ? defaultProfileForStage(desc.stage)
        : desc.compileOptions.targetProfile;
    if (profile.empty()) {
        throw std::runtime_error("RHIShaderDesc::stage is not supported by D3D12");
    }

    std::vector<D3D_SHADER_MACRO> macros;
    macros.reserve(desc.compileOptions.defines.size() + 1);
    for (const RHIShaderDefine& define : desc.compileOptions.defines) {
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
        throw std::runtime_error("RHIShaderDesc requires bytecode, source, or filePath");
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

RHIShader RHID3D12::createShaderModule(const RHIShaderDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }

    Impl::ShaderResource resource{};
    resource.desc = desc;
    resource.bytecode = compileHlsl(desc);
    if (resource.bytecode.empty()) {
        throw std::runtime_error("RHIShaderDesc has no bytecode");
    }
    return makeRenderHandle<RHIShader>(impl_->shaders, std::move(resource));
}

RHIBindSetLayout RHID3D12::createBindSetLayout(const RHIBindSetLayoutDesc& desc) {
    Impl::BindSetLayoutResource resource{};
    resource.desc = desc;
    return makeRenderHandle<RHIBindSetLayout>(impl_->bindSetLayouts, std::move(resource));
}

RHIBindSet RHID3D12::createBindSet(const RHIBindSetDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }

    const Impl::BindSetLayoutResource* layout = getRenderResource(impl_->bindSetLayouts, desc.layout);
    if (layout == nullptr) {
        throw std::runtime_error("RHIBindSetDesc::layout is invalid");
    }

    Impl::BindSetResource resource{};
    resource.desc = desc;
    resource.bindings.reserve(desc.bindings.size());

    for (const RHIResourceBinding& binding : desc.bindings) {
        const RHIBindSetLayoutEntry* layoutEntry = impl_->findLayoutEntry(*layout, binding.binding);
        if (layoutEntry == nullptr) {
            throw std::runtime_error("RHIResourceBinding has no matching RHIBindSetLayoutEntry");
        }

        Impl::ResolvedBinding resolved{};
        resolved.slot = binding.binding;
        resolved.type = binding.type;
        resolved.visibility = layoutEntry->visibility;

        if (binding.type == RHIBindingType::UniformBuffer) {
            const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, binding.buffer.buffer);
            if (buffer == nullptr || !buffer->resource) {
                throw std::runtime_error("RHIResourceBinding uniform buffer is invalid");
            }
            const u64 rangeSize = binding.buffer.size == RHI_WHOLE_SIZE
                ? buffer->desc.size - binding.buffer.offset
                : binding.buffer.size;

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc{};
            cbvDesc.BufferLocation = buffer->resource->GetGPUVirtualAddress() + binding.buffer.offset;
            cbvDesc.SizeInBytes = alignUp<UINT>(static_cast<UINT>(rangeSize), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
            resolved.resourceDescriptor = impl_->allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            impl_->device->CreateConstantBufferView(&cbvDesc, resolved.resourceDescriptor.handle);
        } else if (binding.type == RHIBindingType::StorageBuffer) {
            const Impl::BufferResource* buffer = getRenderResource(impl_->buffers, binding.buffer.buffer);
            if (buffer == nullptr || !buffer->resource) {
                throw std::runtime_error("RHIResourceBinding storage buffer is invalid");
            }
            const u64 rangeSize = binding.buffer.size == RHI_WHOLE_SIZE
                ? buffer->desc.size - binding.buffer.offset
                : binding.buffer.size;
            if ((binding.buffer.offset % 4) != 0 || (rangeSize % 4) != 0) {
                throw std::runtime_error("D3D12 raw buffer descriptors require 4-byte aligned offset and size");
            }

            resolved.resourceDescriptor = impl_->allocateDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            if (layoutEntry->writable) {
                D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
                uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
                uavDesc.Buffer.FirstElement = binding.buffer.offset / 4;
                uavDesc.Buffer.NumElements = static_cast<UINT>(rangeSize / 4);
                uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
                impl_->device->CreateUnorderedAccessView(buffer->resource.Get(), nullptr, &uavDesc, resolved.resourceDescriptor.handle);
            } else {
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
                srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Buffer.FirstElement = binding.buffer.offset / 4;
                srvDesc.Buffer.NumElements = static_cast<UINT>(rangeSize / 4);
                srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
                impl_->device->CreateShaderResourceView(buffer->resource.Get(), &srvDesc, resolved.resourceDescriptor.handle);
            }
        } else if (binding.type == RHIBindingType::SampledTexture || binding.type == RHIBindingType::StorageTexture || binding.type == RHIBindingType::CombinedTextureSampler) {
            const Impl::TextureViewResource* view = getRenderResource(impl_->textureViews, binding.texture.view);
            if (view == nullptr) {
                throw std::runtime_error("RHIResourceBinding texture view is invalid");
            }
            resolved.resourceDescriptor = binding.type == RHIBindingType::StorageTexture ? view->uav : view->srv;
            if (!resolved.resourceDescriptor.valid) {
                throw std::runtime_error("RHIResourceBinding texture view does not have the required D3D12 descriptor");
            }
            if (binding.type == RHIBindingType::CombinedTextureSampler) {
                const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
                if (sampler == nullptr || !sampler->sampler.valid) {
                    throw std::runtime_error("CombinedTextureSampler requires a valid sampler");
                }
                resolved.samplerDescriptor = sampler->sampler;
            }
        } else if (binding.type == RHIBindingType::Sampler) {
            const Impl::SamplerResource* sampler = getRenderResource(impl_->samplers, binding.sampler);
            if (sampler == nullptr || !sampler->sampler.valid) {
                throw std::runtime_error("RHIResourceBinding sampler is invalid");
            }
            resolved.samplerDescriptor = sampler->sampler;
        } else if (binding.type == RHIBindingType::PushConstant) {
            continue;
        } else {
            throw std::runtime_error("RHIResourceBinding type is not supported by D3D12");
        }

        resource.bindings.push_back(std::move(resolved));
    }

    return makeRenderHandle<RHIBindSet>(impl_->bindSets, std::move(resource));
}

static D3D12_DESCRIPTOR_RANGE_TYPE toDescriptorRangeType(const RHIBindSetLayoutEntry& entry) {
    switch (entry.type) {
    case RHIBindingType::UniformBuffer:          return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    case RHIBindingType::StorageBuffer:          return entry.writable ? D3D12_DESCRIPTOR_RANGE_TYPE_UAV : D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    case RHIBindingType::SampledTexture:         return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    case RHIBindingType::StorageTexture:         return D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    case RHIBindingType::Sampler:                return D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    case RHIBindingType::CombinedTextureSampler: return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    case RHIBindingType::AccelerationStructure:  return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    case RHIBindingType::PushConstant:           return D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    }
    return D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
}

static void addDescriptorTableParameter(
    std::vector<D3D12_ROOT_PARAMETER>& parameters,
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>>& rangeStorage,
    const RHIBindSetLayoutDesc& layoutDesc,
    const RHIBindSetLayoutEntry& entry,
    D3D12_DESCRIPTOR_RANGE_TYPE rangeType) {
    rangeStorage.emplace_back(1);
    D3D12_DESCRIPTOR_RANGE& range = rangeStorage.back()[0];
    range.RangeType = rangeType;
    range.NumDescriptors = std::max(1u, entry.arrayCount);
    range.BaseShaderRegister = entry.binding;
    range.RegisterSpace = layoutDesc.set;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER parameter{};
    parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameter.ShaderVisibility = toD3D12ShaderVisibility(entry.visibility);
    parameter.DescriptorTable.NumDescriptorRanges = 1;
    parameter.DescriptorTable.pDescriptorRanges = rangeStorage.back().data();
    parameters.push_back(parameter);
}

template <typename LayoutResourceT>
static ComPtr<ID3D12RootSignature> createRootSignatureForLayout(
    ID3D12Device* device,
    const RHIPipelineLayoutDesc& desc,
    const std::vector<LayoutResourceT*>& layouts) {
    std::vector<D3D12_ROOT_PARAMETER> parameters;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> rangeStorage;
    parameters.reserve(32);
    rangeStorage.reserve(32);

    for (const LayoutResourceT* layout : layouts) {
        for (const RHIBindSetLayoutEntry& entry : layout->desc.entries) {
            if (entry.type == RHIBindingType::PushConstant) {
                continue;
            }

            addDescriptorTableParameter(parameters, rangeStorage, layout->desc, entry, toDescriptorRangeType(entry));
            if (entry.type == RHIBindingType::CombinedTextureSampler) {
                addDescriptorTableParameter(parameters, rangeStorage, layout->desc, entry, D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER);
            }
        }
    }

    for (const RHIPushConstantRange& push : desc.pushConstants) {
        if (push.size == 0) {
            continue;
        }
        D3D12_ROOT_PARAMETER parameter{};
        parameter.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameter.ShaderVisibility = toD3D12ShaderVisibility(push.stages);
        parameter.Constants.ShaderRegister = 0;
        parameter.Constants.RegisterSpace = 0;
        parameter.Constants.Num32BitValues = alignUp(push.size, 4u) / 4u;
        parameters.push_back(parameter);
    }

    D3D12_ROOT_SIGNATURE_DESC signatureDesc{};
    signatureDesc.NumParameters = static_cast<UINT>(parameters.size());
    signatureDesc.pParameters = parameters.empty() ? nullptr : parameters.data();
    signatureDesc.NumStaticSamplers = 0;
    signatureDesc.pStaticSamplers = nullptr;
    signatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&signatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (FAILED(hr)) {
        std::string message = "D3D12SerializeRootSignature failed";
        if (error) {
            message += ": ";
            message.append(static_cast<const char*>(error->GetBufferPointer()), error->GetBufferSize());
        }
        throw std::runtime_error(message);
    }

    ComPtr<ID3D12RootSignature> rootSignature;
    throwIfFailed(
        device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&rootSignature)),
        "CreateRootSignature failed");
    return rootSignature;
}

RHIPipelineLayout RHID3D12::createPipelineLayout(const RHIPipelineLayoutDesc& desc) {
    if (!isInitialized()) {
        throw std::runtime_error("RHID3D12 is not initialized");
    }

    std::vector<Impl::BindSetLayoutResource*> layouts;
    layouts.reserve(desc.bindSetLayouts.size());
    for (RHIBindSetLayout handle : desc.bindSetLayouts) {
        Impl::BindSetLayoutResource* layout = getRenderResource(impl_->bindSetLayouts, handle);
        if (layout == nullptr) {
            throw std::runtime_error("RHIPipelineLayoutDesc contains an invalid bind set layout");
        }
        layouts.push_back(layout);
    }

    Impl::PipelineLayoutResource resource{};
    resource.desc = desc;
    resource.rootSignature = createRootSignatureForLayout(impl_->device.Get(), desc, layouts);
    impl_->setDebugName(resource.rootSignature.Get(), desc.debugName);
    return makeRenderHandle<RHIPipelineLayout>(impl_->pipelineLayouts, std::move(resource));
}

RHIPipelineCache RHID3D12::createPipelineCache(const RHIPipelineCacheDesc& desc) {
    Impl::PipelineCacheResource resource{};
    resource.desc = desc;
    return makeRenderHandle<RHIPipelineCache>(impl_->pipelineCaches, std::move(resource));
}

// D3D12 resources 片段负责“能被 GPU 使用的对象”：
// - buffer/texture 是 ID3D12Resource；
// - texture view、sampler、CBV/SRV/UAV 是 descriptor heap 中的槽位；
// - BindSet 先解析成 CPU descriptor，后续完整命令录制时再拷贝到 shader-visible heap；
// - PipelineLayout 会真正生成 D3D12 root signature，这是 D3D12 资源绑定模型的核心。

} // namespace rhi





