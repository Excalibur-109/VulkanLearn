#include "rhi/RHI.h"

#include <iostream>
#include <memory>

namespace rhi {

// 真实 D3D11 后端会把纹理绑定映射为 SRV/DSV，并在
// OMSetRenderTargets 与 PSSetShaderResources 之间跟踪资源冲突。
// 当前便携适配器保留相同的生命周期和命令列表合同，不要求 HWND。
std::unique_ptr<Device> createSoftwareDevice(const DeviceCreateInfo&);

namespace {

class D3D11Adapter final : public Device {
public:
    explicit D3D11Adapter(const DeviceCreateInfo& info)
        : implementation(createSoftwareDevice(info)), createInfo(info) {}

    Backend backend() const override { return Backend::D3D11; }
    const char* name() const override {
        return "D3D11 adapter (portable validation path)";
    }

    std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc,
                                         const void* data) override {
        return implementation->createBuffer(desc, data);
    }
    std::unique_ptr<Texture> createTexture(const TextureDesc& desc,
                                           const void* data) override {
        return implementation->createTexture(desc, data);
    }
    std::unique_ptr<Sampler> createSampler(const SamplerDesc& desc) override {
        return implementation->createSampler(desc);
    }
    std::unique_ptr<Shader> createShader(const ShaderDesc& desc) override {
        return implementation->createShader(desc);
    }
    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override {
        return implementation->createPipeline(desc);
    }
    std::unique_ptr<CommandList> createCommandList() override {
        return implementation->createCommandList();
    }

    void submit(CommandList& commands) override { implementation->submit(commands); }
    void waitIdle() override { implementation->waitIdle(); }
    bool present() override { return implementation->present(); }
    Extent2D drawableExtent() const override { return createInfo.extent; }

private:
    std::unique_ptr<Device> implementation;
    DeviceCreateInfo createInfo;
};

} // 匿名命名空间

std::unique_ptr<Device> createD3D11Device(const DeviceCreateInfo& info) {
    std::cout << "[RHI] D3D11 backend selected\n";
    return std::make_unique<D3D11Adapter>(info);
}

} // 命名空间 rhi
