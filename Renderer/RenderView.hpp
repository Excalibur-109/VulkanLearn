#pragma once

#include "Math.hpp"
#include "Renderer/RenderScene.hpp"

#include <array>
#include <vector>

namespace renderer {

/// 包围体相对视锥的位置，用于区分完全剔除、相交和完全位于内部三种情况。
enum class RenderFrustumResult : rhi::u8 {
    Outside,
    Intersecting,
    Inside
};

/// 平面方程 dot(normal, point) + distance = 0，法线统一指向视锥内部。
struct RenderPlane {
    float3 normal{0.0F};
    float distance = 0.0F;
};

/**
 * @brief 从相机 ViewProjection 提取出的六平面视锥。
 *
 * 这里采用 RHI 统一的零到一深度约定：裁剪空间满足 -w<=x<=w、-w<=y<=w、
 * 0<=z<=w。Vulkan 和 D3D 都可以使用这一形式；Vulkan 的 Y 翻转不改变剔除原理。
 */
class RenderFrustum {
public:
    [[nodiscard]] static RenderFrustum FromViewProjectionZO(const float4x4& viewProjection) noexcept;

    [[nodiscard]] RenderFrustumResult Classify(const rhi::RHIBoundingSphere& sphere) const noexcept;
    [[nodiscard]] RenderFrustumResult Classify(const rhi::RHIBoundingBox& box) const noexcept;

    [[nodiscard]] const std::array<RenderPlane, 6>& Planes() const noexcept;

private:
    std::array<RenderPlane, 6> planes_{};
};

/**
 * @brief 收集阶段保存的轻量 draw item。
 *
 * item 不复制 MeshDesc/MaterialDesc，只保存稳定对象句柄和排序所需的快照。真正生成
 * RHIDrawCommand 时，RenderFrameBuilder 再从 RenderScene 解析资源描述。
 */
struct RenderDrawItem {
    RenderObjectHandle object{};
    float cameraDistanceSquared = 0.0F;
    rhi::u64 sortingKey = 0;
    rhi::u64 pipelineKey = 0;
    rhi::u64 materialKey = 0;
    rhi::u64 meshKey = 0;
    rhi::u32 collectionOrder = 0;
};

/// 各类内容分队列保存，避免执行阶段反复通过 if/switch 判断物体类型。
struct RenderDrawLists {
    std::vector<RenderDrawItem> background;
    std::vector<RenderDrawItem> opaque;
    std::vector<RenderDrawItem> alphaTest;
    std::vector<RenderDrawItem> transparent;
    std::vector<RenderDrawItem> overlay;
    std::vector<RenderDrawItem> shadowCasters;
};

/// 一个相机对应一个 RenderView；多相机、反射探针和编辑器 SceneView 各自收集一次。
struct RenderView {
    rhi::RHICameraData camera{};
    RenderFrustum frustum{};
    rhi::u32 layerMask = 0xFFFFFFFFU;
    RenderDrawLists draws{};
    rhi::u32 visibleObjectCount = 0;
    rhi::u32 frustumCulledCount = 0;
    rhi::u32 invalidResourceCount = 0;
};

} // namespace renderer
