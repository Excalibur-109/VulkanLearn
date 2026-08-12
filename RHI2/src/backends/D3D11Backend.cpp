#include "rhi/RHI.h"

#if defined(_WIN32)

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace rhi {
namespace {

using Microsoft::WRL::ComPtr;

void throwIfFailed(HRESULT result, const char* operation) {
    if (SUCCEEDED(result)) return;
    throw std::runtime_error(std::string("D3D11 ") + operation + " 失败，HRESULT=" +
                             std::to_string(static_cast<unsigned long>(result)));
}

DXGI_FORMAT toDxgiFormat(Format format) {
    switch (format) {
    case Format::RGBA8_UNorm: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case Format::BGRA8_UNorm: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case Format::RGBA16_Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case Format::RGBA32_Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case Format::RGB32_Float: return DXGI_FORMAT_R32G32B32_FLOAT;
    case Format::RG32_Float: return DXGI_FORMAT_R32G32_FLOAT;
    case Format::R32_Float: return DXGI_FORMAT_R32_FLOAT;
    case Format::D32_Float: return DXGI_FORMAT_D32_FLOAT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

D3D11_PRIMITIVE_TOPOLOGY toTopology(PrimitiveTopology topology) {
    switch (topology) {
    case PrimitiveTopology::TriangleList: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    case PrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case PrimitiveTopology::LineList: return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
    }
    return D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
}

D3D11_COMPARISON_FUNC toComparison(CompareOp comparison) {
    switch (comparison) {
    case CompareOp::Never: return D3D11_COMPARISON_NEVER;
    case CompareOp::Less: return D3D11_COMPARISON_LESS;
    case CompareOp::LessEqual: return D3D11_COMPARISON_LESS_EQUAL;
    case CompareOp::Equal: return D3D11_COMPARISON_EQUAL;
    case CompareOp::GreaterEqual: return D3D11_COMPARISON_GREATER_EQUAL;
    case CompareOp::Greater: return D3D11_COMPARISON_GREATER;
    case CompareOp::Always: return D3D11_COMPARISON_ALWAYS;
    }
    return D3D11_COMPARISON_LESS_EQUAL;
}

D3D11_CULL_MODE toCullMode(CullMode cullMode) {
    switch (cullMode) {
    case CullMode::None: return D3D11_CULL_NONE;
    case CullMode::Front: return D3D11_CULL_FRONT;
    case CullMode::Back: return D3D11_CULL_BACK;
    }
    return D3D11_CULL_BACK;
}

class D3D11Buffer final : public Buffer {
public:
    BufferDesc description;
    ComPtr<ID3D11Buffer> native;

    const BufferDesc& desc() const override { return description; }
};

class D3D11Texture final : public Texture {
public:
    TextureDesc description;
    ComPtr<ID3D11Texture2D> native;
    ComPtr<ID3D11RenderTargetView> renderTargetView;
    ComPtr<ID3D11DepthStencilView> depthStencilView;
    ComPtr<ID3D11ShaderResourceView> shaderResourceView;

    const TextureDesc& desc() const override { return description; }
};

class D3D11Sampler final : public Sampler {
public:
    SamplerDesc description;
    ComPtr<ID3D11SamplerState> native;

    const SamplerDesc& desc() const override { return description; }
};

class D3D11Shader final : public Shader {
public:
    ShaderDesc description;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11ComputeShader> computeShader;
    ComPtr<ID3DBlob> bytecode;

    ShaderStage stage() const override { return description.stage; }
};

class D3D11Pipeline final : public Pipeline {
public:
    PipelineDesc description;
    ComPtr<ID3D11InputLayout> inputLayout;
    ComPtr<ID3D11DepthStencilState> depthStencilState;
    ComPtr<ID3D11RasterizerState> rasterizerState;
    ComPtr<ID3D11BlendState> blendState;
};

class D3D11Device;

class D3D11CommandList final : public CommandList {
public:
    explicit D3D11CommandList(D3D11Device& owner) : device(owner) {}

    void begin() override;
    void end() override;
    void beginRenderPass(const RenderPassDesc&, Texture*, Texture*) override;
    void endRenderPass() override;
    void setPipeline(Pipeline*) override;
    void setViewport(float, float, float, float, float, float) override;
    void setScissor(int, int, uint32_t, uint32_t) override;
    void setVertexBuffer(Buffer*, uint32_t) override;
    void setIndexBuffer(Buffer*) override;
    void setUniformBuffer(Buffer*, uint32_t) override;
    void draw(uint32_t, uint32_t) override;
    void drawIndexed(uint32_t, uint32_t, int32_t) override;
    void dispatch(uint32_t, uint32_t, uint32_t) override;
    void setTexture(Texture*, uint32_t) override;
    void setSampler(Sampler*, uint32_t) override;
    void transition(Texture*, ResourceState, ResourceState) override {}

private:
    D3D11Device& device;
    bool recording = false;
};

class D3D11Device final : public Device {
public:
    explicit D3D11Device(const DeviceCreateInfo& info) : createInfo(info) {
        const UINT requestedFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT |
            (info.enableDebugLayer ? D3D11_CREATE_DEVICE_DEBUG : 0);
        D3D_FEATURE_LEVEL featureLevel{};
        HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                           requestedFlags, nullptr, 0,
                                           D3D11_SDK_VERSION, nativeDevice.GetAddressOf(),
                                           &featureLevel, immediateContext.GetAddressOf());
        // Debug runtime is optional on machines without Graphics Tools.
        if (FAILED(result) && info.enableDebugLayer) {
            result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                       D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                       D3D11_SDK_VERSION, nativeDevice.GetAddressOf(),
                                       &featureLevel, immediateContext.GetAddressOf());
        }
        throwIfFailed(result, "CreateDevice");
        if (info.nativeWindow != nullptr) createSwapChain();
    }

    Backend backend() const override { return Backend::D3D11; }
    const char* name() const override { return "D3D11 原生后端"; }

    std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc,
                                         const void* data) override {
        D3D11_BUFFER_DESC nativeDesc{};
        nativeDesc.ByteWidth = static_cast<UINT>(desc.size);
        nativeDesc.Usage = desc.cpuVisible ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
        nativeDesc.CPUAccessFlags = desc.cpuVisible ? D3D11_CPU_ACCESS_WRITE : 0;
        if (has(desc.usage, ResourceUsage::Vertex)) nativeDesc.BindFlags |= D3D11_BIND_VERTEX_BUFFER;
        if (has(desc.usage, ResourceUsage::Index)) nativeDesc.BindFlags |= D3D11_BIND_INDEX_BUFFER;
        if (has(desc.usage, ResourceUsage::Uniform)) nativeDesc.BindFlags |= D3D11_BIND_CONSTANT_BUFFER;
        if (has(desc.usage, ResourceUsage::Storage)) nativeDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        if (nativeDesc.BindFlags == 0) throw std::runtime_error("D3D11 缓冲区缺少用途标志");
        if (has(desc.usage, ResourceUsage::Uniform) && (nativeDesc.ByteWidth % 16) != 0) {
            throw std::runtime_error("D3D11 常量缓冲区大小必须为 16 字节的倍数");
        }

        D3D11_SUBRESOURCE_DATA initialData{};
        initialData.pSysMem = data;
        auto buffer = std::make_unique<D3D11Buffer>();
        buffer->description = desc;
        throwIfFailed(nativeDevice->CreateBuffer(&nativeDesc, data ? &initialData : nullptr,
                                                 buffer->native.GetAddressOf()),
                      "CreateBuffer");
        return buffer;
    }

    std::unique_ptr<Texture> createTexture(const TextureDesc& desc,
                                           const void* data) override {
        if (data != nullptr) throw std::runtime_error("D3D11 示例尚未实现纹理初始数据上传");
        auto texture = std::make_unique<D3D11Texture>();
        texture->description = desc;
        createTextureResources(*texture);
        return texture;
    }

    std::unique_ptr<Sampler> createSampler(const SamplerDesc& desc) override {
        D3D11_SAMPLER_DESC nativeDesc{};
        nativeDesc.Filter = desc.linear ? D3D11_FILTER_MIN_MAG_MIP_LINEAR
                                        : D3D11_FILTER_MIN_MAG_MIP_POINT;
        nativeDesc.AddressU = nativeDesc.AddressV = nativeDesc.AddressW =
            desc.repeat ? D3D11_TEXTURE_ADDRESS_WRAP : D3D11_TEXTURE_ADDRESS_CLAMP;
        nativeDesc.MaxAnisotropy = static_cast<UINT>(desc.anisotropy);
        nativeDesc.MaxLOD = D3D11_FLOAT32_MAX;
        auto sampler = std::make_unique<D3D11Sampler>();
        sampler->description = desc;
        throwIfFailed(nativeDevice->CreateSamplerState(&nativeDesc, sampler->native.GetAddressOf()),
                      "CreateSamplerState");
        return sampler;
    }

    std::unique_ptr<Shader> createShader(const ShaderDesc& desc) override {
        const char* profile = desc.stage == ShaderStage::Vertex ? "vs_5_0" :
                              desc.stage == ShaderStage::Fragment ? "ps_5_0" : "cs_5_0";
        ComPtr<ID3DBlob> errors;
        auto shader = std::make_unique<D3D11Shader>();
        shader->description = desc;
        HRESULT result = D3DCompile(desc.source.data(), desc.source.size(), desc.debugName.c_str(),
                                    nullptr, nullptr, desc.entry.c_str(), profile,
                                    D3DCOMPILE_ENABLE_STRICTNESS, 0,
                                    shader->bytecode.GetAddressOf(), errors.GetAddressOf());
        if (FAILED(result)) {
            const char* details = errors ? static_cast<const char*>(errors->GetBufferPointer()) : "";
            throw std::runtime_error(std::string("HLSL 编译失败: ") + details);
        }
        if (desc.stage == ShaderStage::Vertex) {
            throwIfFailed(nativeDevice->CreateVertexShader(shader->bytecode->GetBufferPointer(),
                                                            shader->bytecode->GetBufferSize(), nullptr,
                                                            shader->vertexShader.GetAddressOf()), "CreateVertexShader");
        } else if (desc.stage == ShaderStage::Fragment) {
            throwIfFailed(nativeDevice->CreatePixelShader(shader->bytecode->GetBufferPointer(),
                                                           shader->bytecode->GetBufferSize(), nullptr,
                                                           shader->pixelShader.GetAddressOf()), "CreatePixelShader");
        } else {
            throwIfFailed(nativeDevice->CreateComputeShader(shader->bytecode->GetBufferPointer(),
                                                             shader->bytecode->GetBufferSize(), nullptr,
                                                             shader->computeShader.GetAddressOf()), "CreateComputeShader");
        }
        return shader;
    }

    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override {
        auto* vertex = dynamic_cast<D3D11Shader*>(desc.vertexShader.get());
        if (vertex == nullptr || vertex->vertexShader == nullptr) {
            throw std::runtime_error("D3D11 图形管线必须指定顶点着色器");
        }

        std::vector<D3D11_INPUT_ELEMENT_DESC> attributes;
        attributes.reserve(desc.attributes.size());
        for (const VertexAttribute& attribute : desc.attributes) {
            D3D11_INPUT_ELEMENT_DESC element{};
            element.SemanticName = "ATTRIBUTE";
            element.SemanticIndex = attribute.location;
            element.Format = toDxgiFormat(attribute.format);
            element.InputSlot = 0;
            element.AlignedByteOffset = attribute.offset;
            element.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
            attributes.push_back(element);
        }

        auto pipeline = std::make_unique<D3D11Pipeline>();
        pipeline->description = desc;
        throwIfFailed(nativeDevice->CreateInputLayout(attributes.data(), static_cast<UINT>(attributes.size()),
                                                      vertex->bytecode->GetBufferPointer(),
                                                      vertex->bytecode->GetBufferSize(),
                                                      pipeline->inputLayout.GetAddressOf()), "CreateInputLayout");

        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = desc.depthTest;
        depthDesc.DepthWriteMask = desc.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
        depthDesc.DepthFunc = toComparison(desc.depthCompare);
        throwIfFailed(nativeDevice->CreateDepthStencilState(&depthDesc,
                                                            pipeline->depthStencilState.GetAddressOf()),
                      "CreateDepthStencilState");

        D3D11_RASTERIZER_DESC rasterizerDesc{};
        rasterizerDesc.FillMode = D3D11_FILL_SOLID;
        rasterizerDesc.CullMode = toCullMode(desc.cullMode);
        rasterizerDesc.DepthClipEnable = TRUE;
        rasterizerDesc.DepthBias = static_cast<INT>(desc.depthBiasConstant);
        rasterizerDesc.SlopeScaledDepthBias = desc.depthBiasSlope;
        throwIfFailed(nativeDevice->CreateRasterizerState(&rasterizerDesc,
                                                          pipeline->rasterizerState.GetAddressOf()),
                      "CreateRasterizerState");

        D3D11_BLEND_DESC blendDesc{};
        blendDesc.RenderTarget[0].BlendEnable = desc.blend;
        blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (desc.blend) {
            blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        }
        throwIfFailed(nativeDevice->CreateBlendState(&blendDesc, pipeline->blendState.GetAddressOf()),
                      "CreateBlendState");
        return pipeline;
    }

    std::unique_ptr<CommandList> createCommandList() override {
        return std::make_unique<D3D11CommandList>(*this);
    }

    bool updateBuffer(Buffer& buffer, const void* data, size_t size, size_t offset) override {
        auto* target = dynamic_cast<D3D11Buffer*>(&buffer);
        if (target == nullptr || data == nullptr || offset + size > target->description.size) return false;
        if (target->description.cpuVisible) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(immediateContext->Map(target->native.Get(), 0, D3D11_MAP_WRITE_DISCARD,
                                             0, &mapped))) return false;
            std::memcpy(static_cast<uint8_t*>(mapped.pData) + offset, data, size);
            immediateContext->Unmap(target->native.Get(), 0);
        } else {
            D3D11_BOX box{};
            box.left = static_cast<UINT>(offset);
            box.right = static_cast<UINT>(offset + size);
            box.bottom = 1;
            box.back = 1;
            immediateContext->UpdateSubresource(target->native.Get(), 0, &box, data, 0, 0);
        }
        return true;
    }

    Texture* currentColorTarget() override { return backBuffer.get(); }
    Texture* currentDepthTarget() override { return backBufferDepth.get(); }

    bool resizeDrawable(Extent2D extent) override {
        if (!swapChain || extent.width == 0 || extent.height == 0) return false;
        immediateContext->OMSetRenderTargets(0, nullptr, nullptr);
        backBuffer.reset();
        backBufferDepth.reset();
        if (FAILED(swapChain->ResizeBuffers(0, extent.width, extent.height, DXGI_FORMAT_UNKNOWN, 0))) return false;
        createInfo.extent = extent;
        createBackBufferTargets();
        return true;
    }

    void submit(CommandList&) override { immediateContext->Flush(); }
    void waitIdle() override { immediateContext->Flush(); }
    bool present() override {
        return !swapChain || SUCCEEDED(swapChain->Present(createInfo.enableVsync ? 1 : 0, 0));
    }
    Extent2D drawableExtent() const override { return createInfo.extent; }

    ID3D11DeviceContext* context() const { return immediateContext.Get(); }

private:
    void createSwapChain() {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory> factory;
        throwIfFailed(nativeDevice.As(&dxgiDevice), "Query IDXGIDevice");
        throwIfFailed(dxgiDevice->GetAdapter(adapter.GetAddressOf()), "GetAdapter");
        throwIfFailed(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())), "GetFactory");

        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Width = createInfo.extent.width;
        desc.BufferDesc.Height = createInfo.extent.height;
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.BufferDesc.RefreshRate.Numerator = 0;
        desc.BufferDesc.RefreshRate.Denominator = 1;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = static_cast<HWND>(createInfo.nativeWindow);
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        throwIfFailed(factory->CreateSwapChain(nativeDevice.Get(), &desc, swapChain.GetAddressOf()),
                      "CreateSwapChain");
        createBackBufferTargets();
    }

    void createBackBufferTargets() {
        auto color = std::make_unique<D3D11Texture>();
        color->description.extent = createInfo.extent;
        color->description.format = Format::BGRA8_UNorm;
        color->description.usage = ResourceUsage::RenderTarget;
        color->description.debugName = "D3D11 交换链后备缓冲";
        throwIfFailed(swapChain->GetBuffer(0, IID_PPV_ARGS(color->native.GetAddressOf())), "GetBuffer");
        throwIfFailed(nativeDevice->CreateRenderTargetView(color->native.Get(), nullptr,
                                                           color->renderTargetView.GetAddressOf()), "CreateRTV");
        backBuffer = std::move(color);

        TextureDesc depthDesc;
        depthDesc.extent = createInfo.extent;
        depthDesc.format = Format::D32_Float;
        depthDesc.usage = ResourceUsage::DepthStencil;
        depthDesc.debugName = "D3D11 交换链深度缓冲";
        backBufferDepth = std::unique_ptr<D3D11Texture>(
            static_cast<D3D11Texture*>(createTexture(depthDesc, nullptr).release()));
    }

    void createTextureResources(D3D11Texture& texture) {
        const TextureDesc& desc = texture.description;
        D3D11_TEXTURE2D_DESC nativeDesc{};
        nativeDesc.Width = desc.extent.width;
        nativeDesc.Height = desc.extent.height;
        nativeDesc.MipLevels = desc.mipLevels;
        nativeDesc.ArraySize = desc.layers;
        nativeDesc.SampleDesc.Count = 1;
        nativeDesc.Usage = D3D11_USAGE_DEFAULT;

        const bool depth = has(desc.usage, ResourceUsage::DepthStencil);
        const bool sampleDepth = depth && has(desc.usage, ResourceUsage::Texture);
        nativeDesc.Format = sampleDepth ? DXGI_FORMAT_R32_TYPELESS : toDxgiFormat(desc.format);
        if (has(desc.usage, ResourceUsage::RenderTarget)) nativeDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
        if (depth) nativeDesc.BindFlags |= D3D11_BIND_DEPTH_STENCIL;
        if (has(desc.usage, ResourceUsage::Texture)) nativeDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
        throwIfFailed(nativeDevice->CreateTexture2D(&nativeDesc, nullptr, texture.native.GetAddressOf()),
                      "CreateTexture2D");

        if (has(desc.usage, ResourceUsage::RenderTarget)) {
            throwIfFailed(nativeDevice->CreateRenderTargetView(texture.native.Get(), nullptr,
                                                               texture.renderTargetView.GetAddressOf()), "CreateRTV");
        }
        if (depth) {
            D3D11_DEPTH_STENCIL_VIEW_DESC viewDesc{};
            viewDesc.Format = DXGI_FORMAT_D32_FLOAT;
            viewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
            throwIfFailed(nativeDevice->CreateDepthStencilView(texture.native.Get(), &viewDesc,
                                                                texture.depthStencilView.GetAddressOf()), "CreateDSV");
        }
        if (has(desc.usage, ResourceUsage::Texture)) {
            D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc{};
            viewDesc.Format = sampleDepth ? DXGI_FORMAT_R32_FLOAT : toDxgiFormat(desc.format);
            viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            viewDesc.Texture2D.MipLevels = desc.mipLevels;
            throwIfFailed(nativeDevice->CreateShaderResourceView(texture.native.Get(), &viewDesc,
                                                                  texture.shaderResourceView.GetAddressOf()), "CreateSRV");
        }
    }

    DeviceCreateInfo createInfo;
    ComPtr<ID3D11Device> nativeDevice;
    ComPtr<ID3D11DeviceContext> immediateContext;
    ComPtr<IDXGISwapChain> swapChain;
    std::unique_ptr<D3D11Texture> backBuffer;
    std::unique_ptr<D3D11Texture> backBufferDepth;

    friend class D3D11CommandList;
};

void D3D11CommandList::begin() { recording = true; }
void D3D11CommandList::end() { recording = false; }
void D3D11CommandList::endRenderPass() {}

void D3D11CommandList::beginRenderPass(const RenderPassDesc& desc, Texture* color, Texture* depth) {
    auto* colorTexture = dynamic_cast<D3D11Texture*>(color);
    auto* depthTexture = dynamic_cast<D3D11Texture*>(depth);
    ID3D11RenderTargetView* rtv = colorTexture ? colorTexture->renderTargetView.Get() : nullptr;
    ID3D11DepthStencilView* dsv = depthTexture ? depthTexture->depthStencilView.Get() : nullptr;
    device.context()->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, dsv);
    if (rtv && desc.colorLoad == LoadOp::Clear) {
        const float clear[] = {desc.clear.color.r, desc.clear.color.g, desc.clear.color.b, desc.clear.color.a};
        device.context()->ClearRenderTargetView(rtv, clear);
    }
    if (dsv && desc.depthLoad == LoadOp::Clear) {
        device.context()->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                                desc.clear.depth, static_cast<UINT8>(desc.clear.stencil));
    }
}

void D3D11CommandList::setPipeline(Pipeline* pipeline) {
    auto* native = dynamic_cast<D3D11Pipeline*>(pipeline);
    if (native == nullptr) throw std::runtime_error("D3D11 命令列表收到了非 D3D11 管线");
    auto* vertex = dynamic_cast<D3D11Shader*>(native->description.vertexShader.get());
    auto* fragment = dynamic_cast<D3D11Shader*>(native->description.fragmentShader.get());
    device.context()->IASetInputLayout(native->inputLayout.Get());
    device.context()->IASetPrimitiveTopology(toTopology(native->description.topology));
    device.context()->VSSetShader(vertex ? vertex->vertexShader.Get() : nullptr, nullptr, 0);
    device.context()->PSSetShader(fragment ? fragment->pixelShader.Get() : nullptr, nullptr, 0);
    device.context()->OMSetDepthStencilState(native->depthStencilState.Get(), 0);
    device.context()->RSSetState(native->rasterizerState.Get());
    const float factors[] = {0, 0, 0, 0};
    device.context()->OMSetBlendState(native->blendState.Get(), factors, 0xffffffffu);
}

void D3D11CommandList::setViewport(float x, float y, float width, float height,
                                   float minDepth, float maxDepth) {
    D3D11_VIEWPORT viewport{x, y, width, height, minDepth, maxDepth};
    device.context()->RSSetViewports(1, &viewport);
}

void D3D11CommandList::setScissor(int x, int y, uint32_t width, uint32_t height) {
    D3D11_RECT rect{x, y, x + static_cast<LONG>(width), y + static_cast<LONG>(height)};
    device.context()->RSSetScissorRects(1, &rect);
}

void D3D11CommandList::setVertexBuffer(Buffer* buffer, uint32_t slot) {
    auto* native = dynamic_cast<D3D11Buffer*>(buffer);
    if (native == nullptr) throw std::runtime_error("D3D11 顶点缓冲类型不匹配");
    const UINT stride = native->description.stride;
    const UINT offset = 0;
    ID3D11Buffer* raw = native->native.Get();
    device.context()->IASetVertexBuffers(slot, 1, &raw, &stride, &offset);
}

void D3D11CommandList::setIndexBuffer(Buffer* buffer) {
    auto* native = dynamic_cast<D3D11Buffer*>(buffer);
    if (native == nullptr) throw std::runtime_error("D3D11 索引缓冲类型不匹配");
    const DXGI_FORMAT format = native->description.indexFormat == IndexFormat::UInt16
        ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
    device.context()->IASetIndexBuffer(native->native.Get(), format, 0);
}

void D3D11CommandList::setUniformBuffer(Buffer* buffer, uint32_t slot) {
    auto* native = dynamic_cast<D3D11Buffer*>(buffer);
    if (native == nullptr) throw std::runtime_error("D3D11 常量缓冲类型不匹配");
    ID3D11Buffer* raw = native->native.Get();
    device.context()->VSSetConstantBuffers(slot, 1, &raw);
    device.context()->PSSetConstantBuffers(slot, 1, &raw);
}

void D3D11CommandList::draw(uint32_t vertexCount, uint32_t firstVertex) {
    device.context()->Draw(vertexCount, firstVertex);
}
void D3D11CommandList::drawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) {
    device.context()->DrawIndexed(indexCount, firstIndex, vertexOffset);
}
void D3D11CommandList::dispatch(uint32_t x, uint32_t y, uint32_t z) {
    device.context()->Dispatch(x, y, z);
}
void D3D11CommandList::setTexture(Texture* texture, uint32_t slot) {
    auto* native = dynamic_cast<D3D11Texture*>(texture);
    ID3D11ShaderResourceView* view = native ? native->shaderResourceView.Get() : nullptr;
    device.context()->PSSetShaderResources(slot, 1, &view);
}
void D3D11CommandList::setSampler(Sampler* sampler, uint32_t slot) {
    auto* native = dynamic_cast<D3D11Sampler*>(sampler);
    ID3D11SamplerState* state = native ? native->native.Get() : nullptr;
    device.context()->PSSetSamplers(slot, 1, &state);
}

} // namespace

std::unique_ptr<Device> createD3D11Device(const DeviceCreateInfo& info) {
    return std::make_unique<D3D11Device>(info);
}

} // namespace rhi

#else
namespace rhi {
std::unique_ptr<Device> createD3D11Device(const DeviceCreateInfo&) {
    throw std::runtime_error("D3D11 仅支持 Windows 平台");
}
} // namespace rhi
#endif
