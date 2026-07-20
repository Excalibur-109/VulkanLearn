#pragma once

#include "Renderer/RenderCollector.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace renderer {

/// 标准帧中 draw 所属的阶段。回调可据此选择 shadow/depth/PBR/UI 等不同 Pipeline。
enum class RenderDrawPhase : rhi::u8 {
    Shadow,
    Background,
    Opaque,
    AlphaTest,
    Transparent,
    Overlay
};

/**
 * @brief 交给绑定回调的完整上下文。
 *
 * 默认情况下 FrameBuilder 使用 MaterialDesc::pipeline 和 MaterialDesc::bindSets。
 * 引擎实际项目通常还要绑定 frame/view/object 常量，因此可通过回调替换 pipeline，或按
 * set 号把全局 BindSet、相机 BindSet、物体 BindSet 追加到 bindSets。
 */
struct RenderDrawBindingContext {
    RenderDrawPhase phase = RenderDrawPhase::Opaque;
    RenderObjectHandle objectHandle{};
    const rhi::RHIRenderObjectDesc& object;
    const rhi::RHIMeshDesc& mesh;
    const rhi::RHIMaterialDesc& material;
    const rhi::RHISubmeshDesc& submesh;
};

using RenderDrawBindingResolver = std::function<void(
    const RenderDrawBindingContext& context,
    rhi::RHIPipeline* pipeline,
    std::vector<rhi::RHIBindSet>* bindSets)>;

/// RenderGraph 中一个颜色或深度纹理的声明。有效 texture 表示导入已有 RHI 资源。
struct RenderGraphTextureTarget {
    std::string name;
    rhi::RHITexture texture{};
    rhi::RHITextureDesc desc{};
    rhi::RHITextureAspect aspect = rhi::RHITextureAspect::Color;
    rhi::RHIClearValue clearValue{};
    bool isSwapchainImage = false;
};

/// 标准 pass 名集中保存，方便调试器、Profiler 和外部 submission 使用同一套名称。
struct RenderStandardPassNames {
    std::string shadow = "Renderer.Shadow";
    std::string background = "Renderer.Background";
    std::string opaque = "Renderer.Opaque";
    std::string transparent = "Renderer.Transparent";
    std::string postProcess = "Renderer.PostProcess";
    std::string overlay = "Renderer.UI";
    std::string present = "Renderer.Present";
};

/**
 * @brief 从 RenderView 构建 RHIFramePacket 所需的参数。
 *
 * GPU 对象仍由调用方通过 RHIDevice 创建。FrameBuilder 只引用这些对象并生成命令和
 * RenderGraph 依赖；它不会偷偷创建、销毁或缓存后端资源。
 */
struct RenderFrameBuildDesc {
    rhi::RHIFrameRenderSettings settings{};
    rhi::RHISwapchainDesc swapchain{};
    rhi::RHIUploadBatchDesc uploads{};

    RenderGraphTextureTarget outputColor{};
    std::optional<RenderGraphTextureTarget> depth;
    std::optional<RenderGraphTextureTarget> shadowDepth;
    std::optional<RenderGraphTextureTarget> sceneColor;

    /// Shadow pass 默认使用这条 depth-only pipeline；复杂项目可在 bindingResolver 中逐物体覆盖。
    rhi::RHIPipeline shadowPipeline{};
    std::vector<rhi::RHIBindSet> shadowBindSets;
    RenderDrawBindingResolver bindingResolver;

    /// 开启后处理后，场景先写 sceneColor，再由该 workload 读取它并写 outputColor。
    bool enablePostProcess = false;
    std::optional<rhi::RHIRenderPassWorkload> postProcessWorkload;

    /// UI 系统可提供额外 draw；它会和 Overlay 队列物体合并到同一个 UI pass。
    std::optional<rhi::RHIRenderPassWorkload> overlayWorkload;

    /// outputColor 是交换链图像时启用。同步对象仍通过 submissions/present 由窗口层传入。
    bool addPresentPass = false;
    std::vector<rhi::RHIQueueSubmitDesc> submissions;
    std::optional<rhi::RHIPresentDesc> present;

    /// 供材质、上传或自定义绑定引用的额外图资源；FrameBuilder 不猜测 buffer 大小和初始状态。
    std::vector<rhi::RHIRenderGraphBufferDesc> graphBuffers;
    std::vector<rhi::RHIRenderGraphTextureDesc> graphTextures;
    RenderStandardPassNames passNames{};
};

struct RenderFrameBuildResult {
    rhi::RHIFramePacket packet{};
    std::vector<std::string> errors;

    [[nodiscard]] bool Succeeded() const noexcept {
        return errors.empty();
    }

    [[nodiscard]] std::string ErrorMessage() const;
};

/**
 * @brief 把可见 DrawList 翻译成后端无关 RHIFramePacket。
 *
 * 调用链为 RenderScene -> RenderCollector -> RenderView -> RenderFrameBuilder ->
 * CompileRHIRenderGraph -> RHIDevice::SubmitFrame。该类没有继承层次，也没有 API 分支。
 */
class RenderFrameBuilder {
public:
    [[nodiscard]] static RenderFrameBuildResult Build(
        const RenderScene& scene,
        const RenderView& view,
        const RenderFrameBuildDesc& desc);
};

} // namespace renderer
