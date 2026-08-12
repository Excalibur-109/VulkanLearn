#pragma once

#include "Win32Window.h"

#include "PbrMath.h"

#include "rhi/RHI.h"

#include <memory>
#include <vector>

namespace pbrdemo {

// 基于 RHI 的真实场景渲染器。窗口层只提供 HWND；本类负责创建设备、
// 交换链资源、阴影贴图、图形管线和每帧命令流。
class NativeSceneRenderer final : public WindowRenderer {
public:
    struct FrameConstants;

    explicit NativeSceneRenderer(rhi::Backend backend);

    bool initialize(void* nativeWindow, rhi::Extent2D extent) override;
    void resize(rhi::Extent2D extent) override;
    void renderFrame() override;

private:
    void createResources();
    void createPipelines();
    void updateConstants(bool sphere);

    rhi::Backend selectedBackend;
    rhi::Extent2D drawableExtent{};
    PbrMaterial sphereMaterial;
    std::unique_ptr<rhi::Device> device;
    std::unique_ptr<rhi::Buffer> sphereVertexBuffer;
    std::unique_ptr<rhi::Buffer> sphereIndexBuffer;
    std::unique_ptr<rhi::Buffer> groundVertexBuffer;
    std::unique_ptr<rhi::Buffer> groundIndexBuffer;
    std::unique_ptr<rhi::Buffer> frameConstantBuffer;
    std::unique_ptr<rhi::Texture> shadowMap;
    std::unique_ptr<rhi::Sampler> shadowSampler;
    std::unique_ptr<rhi::Pipeline> shadowPipeline;
    std::unique_ptr<rhi::Pipeline> lightingPipeline;
    uint32_t sphereIndexCount = 0;
    uint32_t groundIndexCount = 0;
    float animationTime = 0.0f;
};

} // 命名空间 pbrdemo
