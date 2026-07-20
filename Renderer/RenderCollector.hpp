#pragma once

#include "Renderer/RenderView.hpp"

namespace renderer {

/// 选择收集阶段使用的包围体。SphereThenBox 先做便宜粗测，相交时再做精确 AABB 测试。
enum class RenderBoundsMode : rhi::u8 {
    Disabled,
    Sphere,
    Box,
    SphereThenBox
};

struct RenderCollectOptions {
    rhi::u32 layerMask = 0xFFFFFFFFU;
    RenderBoundsMode boundsMode = RenderBoundsMode::SphereThenBox;
    bool collectShadowCasters = true;
    bool includeTransparentShadowCasters = false;
};

/**
 * @brief 将分散的场景数据收集成某个相机的一组有序 DrawList。
 *
 * 收集器不依赖 ECS。以后 ECS 的 RenderSystem 只需把组件数据写入 RenderScene，
 * 或直接仿照这里构造 RenderView；RHI 和具体场景框架之间不会形成反向依赖。
 */
class RenderCollector {
public:
    [[nodiscard]] static RenderView Collect(
        const RenderScene& scene,
        const rhi::RHICameraData& camera,
        const RenderCollectOptions& options = {});
};

} // namespace renderer
