#include "rhi/RHI.h"

#include <iostream>
#include <memory>

namespace rhi {

// 真实 D3D12 后端会把绑定映射为描述符表句柄，把 transition() 映射为
// D3D12_RESOURCE_BARRIER。阴影深度资源同时暴露 DSV 和 SRV 视图，
// depthBias 字段则转换为光栅化状态。
std::unique_ptr<Device> createSoftwareDevice(const DeviceCreateInfo&);

namespace {

class D3D12Adapter final : public Device {
public:
    explicit D3D12Adapter(const DeviceCreateInfo& info)
        : implementation(createSoftwareDevice(info)), createInfo(info) {}

    Backend backend() const override { return Backend::D3D12; }
    const char* name() const override {
        return "D3D12 adapter (portable validation path)";
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

std::unique_ptr<Device> createD3D12Device(const DeviceCreateInfo& info) {
    std::cout << "[RHI] D3D12 backend selected\n";
    return std::make_unique<D3D12Adapter>(info);
}

} // 命名空间 rhi
