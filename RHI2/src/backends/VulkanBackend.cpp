#include "rhi/RHI.h"

#include <iostream>
#include <memory>

namespace rhi {

// 当当前构建没有接入 Vulkan SDK 或窗口系统时使用的便携适配器。
// 接入真实 Vulkan 时，可把转发函数替换为 VkDevice、描述符集、命令缓冲和交换链。
std::unique_ptr<Device> createSoftwareDevice(const DeviceCreateInfo&);

namespace {

class VulkanAdapter final : public Device {
public:
    explicit VulkanAdapter(const DeviceCreateInfo& info)
        : implementation(createSoftwareDevice(info)), createInfo(info) {}

    Backend backend() const override { return Backend::Vulkan; }
    const char* name() const override {
        return "Vulkan adapter (portable validation path)";
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

std::unique_ptr<Device> createVulkanDevice(const DeviceCreateInfo& info) {
    std::cout << "[RHI] Vulkan backend selected\n";
    return std::make_unique<VulkanAdapter>(info);
}

} // 命名空间 rhi
