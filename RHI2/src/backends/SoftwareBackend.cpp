#include "rhi/RHI.h"

#include <cstring>
#include <iostream>
#include <memory>
#include <utility>
#include <vector>

namespace rhi {
namespace {

struct SoftwareBuffer final : Buffer {
    BufferDesc description;
    std::vector<uint8_t> bytes;

    const BufferDesc& desc() const override { return description; }
};

struct SoftwareTexture final : Texture {
    TextureDesc description;

    const TextureDesc& desc() const override { return description; }
};

struct SoftwareSampler final : Sampler {
    SamplerDesc description;

    const SamplerDesc& desc() const override { return description; }
};

struct SoftwareShader final : Shader {
    ShaderDesc description;

    ShaderStage stage() const override { return description.stage; }
};

struct SoftwarePipeline final : Pipeline {
    PipelineDesc description;
};

// 验证用命令列表。它不执行真正光栅化，而是检查示例是否按照合法顺序
// 记录渲染通道和绘制命令，并向学习者报告有用的统计信息。
class SoftwareCommandList final : public CommandList {
public:
    void begin() override { recording = true; }
    void end() override { recording = false; }

    void beginRenderPass(const RenderPassDesc&, Texture*, Texture*) override {
        if (recording) inRenderPass = true;
    }
    void endRenderPass() override { inRenderPass = false; }

    void setPipeline(Pipeline*) override {}
    void setViewport(float, float, float, float, float, float) override {}
    void setScissor(int, int, uint32_t, uint32_t) override {}
    void setVertexBuffer(Buffer*, uint32_t) override {}
    void setIndexBuffer(Buffer*) override {}
    void setUniformBuffer(Buffer*, uint32_t) override {}

    void draw(uint32_t, uint32_t) override {
        if (inRenderPass) ++drawCountValue;
    }
    void drawIndexed(uint32_t, uint32_t, int32_t) override {
        if (inRenderPass) ++drawCountValue;
    }
    void dispatch(uint32_t, uint32_t, uint32_t) override {}

    void setTexture(Texture*, uint32_t) override {
        if (recording) ++bindingCountValue;
    }
    void setSampler(Sampler*, uint32_t) override {
        if (recording) ++bindingCountValue;
    }
    void setPushConstants(const void*, size_t, ShaderStage) override {
        if (recording) ++bindingCountValue;
    }
    void transition(Texture*, ResourceState, ResourceState) override {
        if (recording) ++transitionCountValue;
    }

    uint64_t drawCount() const { return drawCountValue; }
    uint64_t transitionCount() const { return transitionCountValue; }
    uint64_t bindingCount() const { return bindingCountValue; }

private:
    bool recording = false;
    bool inRenderPass = false;
    uint64_t drawCountValue = 0;
    uint64_t transitionCountValue = 0;
    uint64_t bindingCountValue = 0;
};

class SoftwareDevice final : public Device {
public:
    explicit SoftwareDevice(DeviceCreateInfo info)
        : createInfo(std::move(info)) {}

    Backend backend() const override { return Backend::Software; }
    const char* name() const override { return "Software validation backend"; }

    std::unique_ptr<Buffer> createBuffer(const BufferDesc& desc,
                                         const void* data) override {
        auto buffer = std::make_unique<SoftwareBuffer>();
        buffer->description = desc;
        buffer->bytes.resize(desc.size);
        if (data != nullptr && desc.size != 0) {
            std::memcpy(buffer->bytes.data(), data, desc.size);
        }
        return buffer;
    }

    std::unique_ptr<Texture> createTexture(const TextureDesc& desc,
                                           const void*) override {
        auto texture = std::make_unique<SoftwareTexture>();
        texture->description = desc;
        return texture;
    }

    std::unique_ptr<Sampler> createSampler(const SamplerDesc& desc) override {
        auto sampler = std::make_unique<SoftwareSampler>();
        sampler->description = desc;
        return sampler;
    }

    std::unique_ptr<Shader> createShader(const ShaderDesc& desc) override {
        auto shader = std::make_unique<SoftwareShader>();
        shader->description = desc;
        return shader;
    }

    std::unique_ptr<Pipeline> createPipeline(const PipelineDesc& desc) override {
        auto pipeline = std::make_unique<SoftwarePipeline>();
        pipeline->description = desc;
        return pipeline;
    }

    std::unique_ptr<CommandList> createCommandList() override {
        return std::make_unique<SoftwareCommandList>();
    }

    void submit(CommandList& commands) override {
        auto* validation = dynamic_cast<SoftwareCommandList*>(&commands);
        if (validation == nullptr) return;

        std::cout << "[RHI] submitted " << validation->drawCount()
                  << " draw(s), " << validation->transitionCount()
                  << " transition(s), " << validation->bindingCount()
                  << " binding(s)\n";
    }

    void waitIdle() override {}
    bool present() override { return true; }
    Extent2D drawableExtent() const override { return createInfo.extent; }

private:
    DeviceCreateInfo createInfo;
};

} // 匿名命名空间

std::unique_ptr<Device> createSoftwareDevice(const DeviceCreateInfo& info) {
    return std::make_unique<SoftwareDevice>(info);
}

} // 命名空间 rhi
