#include "Renderer/RenderView.hpp"

#include <algorithm>
#include <cmath>

namespace renderer {

namespace {

RenderPlane NormalizePlane(const float4& equation) noexcept {
    const float3 normal{equation.x, equation.y, equation.z};
    const float length = Length(normal);
    if (length <= 1.0e-6F || !std::isfinite(length)) {
        return {};
    }
    return {normal / length, equation.w / length};
}

float SignedDistance(const RenderPlane& plane, const float3& point) noexcept {
    return Dot(plane.normal, point) + plane.distance;
}

} // namespace

RenderFrustum RenderFrustum::FromViewProjectionZO(const float4x4& viewProjection) noexcept {
    // Math 的 Matrix 是行主序，operator[] 直接返回数学意义上的一行。
    // 因此裁剪平面可直接由四行组合，不需要额外转置读取。
    const float4 row0 = viewProjection[0];
    const float4 row1 = viewProjection[1];
    const float4 row2 = viewProjection[2];
    const float4 row3 = viewProjection[3];

    RenderFrustum result{};
    result.planes_[0] = NormalizePlane(row3 + row0); // left:   x + w >= 0
    result.planes_[1] = NormalizePlane(row3 - row0); // right: -x + w >= 0
    result.planes_[2] = NormalizePlane(row3 + row1); // bottom:  y + w >= 0
    result.planes_[3] = NormalizePlane(row3 - row1); // top:    -y + w >= 0
    result.planes_[4] = NormalizePlane(row2);        // near:    z     >= 0 (ZO)
    result.planes_[5] = NormalizePlane(row3 - row2); // far:    -z + w >= 0
    return result;
}

RenderFrustumResult RenderFrustum::Classify(const rhi::RHIBoundingSphere& sphere) const noexcept {
    bool intersects = false;
    const float radius = std::max(sphere.radius, 0.0F);
    for (const RenderPlane& plane : planes_) {
        const float distance = SignedDistance(plane, sphere.center);
        if (distance < -radius) {
            return RenderFrustumResult::Outside;
        }
        intersects = intersects || distance < radius;
    }
    return intersects ? RenderFrustumResult::Intersecting : RenderFrustumResult::Inside;
}

RenderFrustumResult RenderFrustum::Classify(const rhi::RHIBoundingBox& box) const noexcept {
    bool intersects = false;
    for (const RenderPlane& plane : planes_) {
        // positiveVertex 是沿平面法线方向最远的角。它仍在平面外侧时，整个 AABB 都在外面。
        // negativeVertex 是反方向最远的角。它在外面而 positive 在里面时，AABB 与平面相交。
        const float3 positiveVertex{
            plane.normal.x >= 0.0F ? box.max.x : box.min.x,
            plane.normal.y >= 0.0F ? box.max.y : box.min.y,
            plane.normal.z >= 0.0F ? box.max.z : box.min.z};
        const float3 negativeVertex{
            plane.normal.x >= 0.0F ? box.min.x : box.max.x,
            plane.normal.y >= 0.0F ? box.min.y : box.max.y,
            plane.normal.z >= 0.0F ? box.min.z : box.max.z};

        if (SignedDistance(plane, positiveVertex) < 0.0F) {
            return RenderFrustumResult::Outside;
        }
        intersects = intersects || SignedDistance(plane, negativeVertex) < 0.0F;
    }
    return intersects ? RenderFrustumResult::Intersecting : RenderFrustumResult::Inside;
}

const std::array<RenderPlane, 6>& RenderFrustum::Planes() const noexcept {
    return planes_;
}

} // namespace renderer
