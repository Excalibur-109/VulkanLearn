#include "Renderer/RenderCollector.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

namespace renderer {

namespace {

bool IsValidBox(const rhi::RHIBoundingBox& box) noexcept {
    return std::isfinite(box.min.x) &&
           std::isfinite(box.min.y) &&
           std::isfinite(box.min.z) &&
           std::isfinite(box.max.x) &&
           std::isfinite(box.max.y) &&
           std::isfinite(box.max.z) &&
           box.min.x <= box.max.x &&
           box.min.y <= box.max.y &&
           box.min.z <= box.max.z;
}

bool IsValidSphere(const rhi::RHIBoundingSphere& sphere) noexcept {
    return sphere.radius > 0.0F &&
           std::isfinite(sphere.radius) &&
           std::isfinite(sphere.center.x) &&
           std::isfinite(sphere.center.y) &&
           std::isfinite(sphere.center.z);
}

glm::vec3 BoundsCenter(const rhi::RHIRenderObjectDesc& object) noexcept {
    if (IsValidSphere(object.worldBoundsSphere)) {
        return object.worldBoundsSphere.center;
    }
    if (IsValidBox(object.worldBounds)) {
        return (object.worldBounds.min + object.worldBounds.max) * 0.5F;
    }
    return glm::vec3(object.transform.localToWorld[3]);
}

bool IsOutsideFrustum(
    const RenderFrustum& frustum,
    const rhi::RHIRenderObjectDesc& object,
    RenderBoundsMode mode) noexcept {
    if (mode == RenderBoundsMode::Disabled) {
        return false;
    }

    const bool hasSphere = IsValidSphere(object.worldBoundsSphere);
    const bool hasBox = IsValidBox(object.worldBounds);
    if (mode == RenderBoundsMode::Sphere && hasSphere) {
        return frustum.Classify(object.worldBoundsSphere) == RenderFrustumResult::Outside;
    }
    if (mode == RenderBoundsMode::Box && hasBox) {
        return frustum.Classify(object.worldBounds) == RenderFrustumResult::Outside;
    }
    if (mode == RenderBoundsMode::SphereThenBox) {
        if (hasSphere) {
            const RenderFrustumResult sphereResult = frustum.Classify(object.worldBoundsSphere);
            if (sphereResult == RenderFrustumResult::Outside) {
                return true;
            }
            if (sphereResult == RenderFrustumResult::Inside || !hasBox) {
                return false;
            }
        }
        if (hasBox) {
            return frustum.Classify(object.worldBounds) == RenderFrustumResult::Outside;
        }
    }
    // 没有有效包围体时选择保守可见，避免因资产数据缺失而让物体凭空消失。
    return false;
}

void SortStateFriendly(std::vector<RenderDrawItem>* items) {
    std::stable_sort(
        items->begin(),
        items->end(),
        [](const RenderDrawItem& lhs, const RenderDrawItem& rhs) {
            return std::tie(
                       lhs.pipelineKey,
                       lhs.materialKey,
                       lhs.meshKey,
                       lhs.sortingKey,
                       lhs.cameraDistanceSquared,
                       lhs.collectionOrder) <
                   std::tie(
                       rhs.pipelineKey,
                       rhs.materialKey,
                       rhs.meshKey,
                       rhs.sortingKey,
                       rhs.cameraDistanceSquared,
                       rhs.collectionOrder);
        });
}

void SortExplicit(std::vector<RenderDrawItem>* items) {
    std::stable_sort(
        items->begin(),
        items->end(),
        [](const RenderDrawItem& lhs, const RenderDrawItem& rhs) {
            return std::tie(lhs.sortingKey, lhs.collectionOrder) <
                   std::tie(rhs.sortingKey, rhs.collectionOrder);
        });
}

} // namespace

RenderView RenderCollector::Collect(
    const RenderScene& scene,
    const rhi::RHICameraData& camera,
    const RenderCollectOptions& options) {
    RenderView view{};
    view.camera = camera;
    view.layerMask = options.layerMask;
    view.frustum = RenderFrustum::FromViewProjectionZO(camera.viewProjection);

    for (rhi::u32 slotIndex = 0; slotIndex < scene.ObjectSlotCount(); ++slotIndex) {
        const RenderObjectSlotView slot = scene.ObjectAtSlot(slotIndex);
        if (slot.object == nullptr) {
            continue;
        }

        const rhi::RHIRenderObjectDesc& object = *slot.object;
        if (!object.visible || (object.layerMask & options.layerMask) == 0) {
            continue;
        }

        const rhi::RHIMeshDesc* mesh = scene.FindMesh(object.mesh);
        const rhi::RHIMaterialDesc* material = scene.FindMaterial(object.material);
        if (mesh == nullptr || material == nullptr || object.submeshIndex >= mesh->submeshes.size()) {
            ++view.invalidResourceCount;
            continue;
        }

        const glm::vec3 center = BoundsCenter(object);
        const glm::vec3 cameraToObject = center - camera.position;
        RenderDrawItem item{};
        item.object = slot.handle;
        item.cameraDistanceSquared = glm::dot(cameraToObject, cameraToObject);
        item.sortingKey = object.sortingKey;
        item.pipelineKey = material->pipeline.value;
        item.materialKey = object.material.value;
        item.meshKey = object.mesh.value;
        item.collectionOrder = slotIndex;

        // 阴影 caster 不能只取主相机可见物体：相机外的物体仍可能把阴影投进画面。
        // 真正的大场景应再用光源视锥裁剪这份列表，本层先保守收集全部有效 caster。
        const bool queueCanCastShadow =
            object.queue != rhi::RHIRenderQueue::Background &&
            object.queue != rhi::RHIRenderQueue::Overlay &&
            (object.queue != rhi::RHIRenderQueue::Transparent ||
             options.includeTransparentShadowCasters);
        if (options.collectShadowCasters && object.castsShadow && queueCanCastShadow) {
            view.draws.shadowCasters.push_back(item);
        }

        if (IsOutsideFrustum(view.frustum, object, options.boundsMode)) {
            ++view.frustumCulledCount;
            continue;
        }

        ++view.visibleObjectCount;
        switch (object.queue) {
        case rhi::RHIRenderQueue::Background:
            view.draws.background.push_back(item);
            break;
        case rhi::RHIRenderQueue::Opaque:
            view.draws.opaque.push_back(item);
            break;
        case rhi::RHIRenderQueue::AlphaTest:
            view.draws.alphaTest.push_back(item);
            break;
        case rhi::RHIRenderQueue::Transparent:
            view.draws.transparent.push_back(item);
            break;
        case rhi::RHIRenderQueue::Overlay:
            view.draws.overlay.push_back(item);
            break;
        }
    }

    SortExplicit(&view.draws.background);
    SortStateFriendly(&view.draws.opaque);
    SortStateFriendly(&view.draws.alphaTest);
    SortStateFriendly(&view.draws.shadowCasters);
    SortExplicit(&view.draws.overlay);

    // 透明混合依赖已经写入 color attachment 的背景颜色，因此必须严格由远到近。
    // stable_sort 让距离相同时仍保持显式 sortingKey 和场景提交顺序的可重复性。
    std::stable_sort(
        view.draws.transparent.begin(),
        view.draws.transparent.end(),
        [](const RenderDrawItem& lhs, const RenderDrawItem& rhs) {
            if (lhs.cameraDistanceSquared != rhs.cameraDistanceSquared) {
                return lhs.cameraDistanceSquared > rhs.cameraDistanceSquared;
            }
            return std::tie(lhs.sortingKey, lhs.collectionOrder) <
                   std::tie(rhs.sortingKey, rhs.collectionOrder);
        });
    return view;
}

} // namespace renderer
