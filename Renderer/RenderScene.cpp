#include "Renderer/RenderScene.hpp"

#include <limits>
#include <utility>

namespace renderer {

namespace {

constexpr rhi::u64 SLOT_MASK = 0xFFFFFFFFULL;

} // namespace

rhi::u64 RenderScene::EncodeHandle(rhi::u32 slotIndex, rhi::u32 generation) noexcept {
    return (static_cast<rhi::u64>(generation) << 32U) |
           (static_cast<rhi::u64>(slotIndex) + 1ULL);
}

bool RenderScene::DecodeHandle(
    rhi::u64 value,
    rhi::u32* slotIndex,
    rhi::u32* generation) noexcept {
    const rhi::u64 encodedSlot = value & SLOT_MASK;
    const rhi::u32 decodedGeneration = static_cast<rhi::u32>(value >> 32U);
    if (encodedSlot == 0 || decodedGeneration == 0) {
        return false;
    }

    *slotIndex = static_cast<rhi::u32>(encodedSlot - 1ULL);
    *generation = decodedGeneration;
    return true;
}

void RenderScene::AdvanceGeneration(rhi::u32* generation) noexcept {
    ++(*generation);
    if (*generation == 0) {
        // generation=0 被保留给无效句柄；发生整数回绕时跳过它。
        *generation = 1;
    }
}

rhi::RHIMesh RenderScene::RegisterMesh(rhi::RHIMeshDesc desc) {
    rhi::u32 slotIndex = 0;
    if (!freeMeshSlots_.empty()) {
        slotIndex = freeMeshSlots_.back();
        freeMeshSlots_.pop_back();
        meshes_[slotIndex].value = std::move(desc);
    } else {
        slotIndex = static_cast<rhi::u32>(meshes_.size());
        meshes_.push_back(MeshSlot{std::move(desc), 1});
    }
    return rhi::RHIMesh(EncodeHandle(slotIndex, meshes_[slotIndex].generation));
}

bool RenderScene::UpdateMesh(rhi::RHIMesh mesh, rhi::RHIMeshDesc desc) {
    rhi::u32 slotIndex = 0;
    rhi::u32 generation = 0;
    if (!DecodeHandle(mesh.value, &slotIndex, &generation) ||
        slotIndex >= meshes_.size() ||
        meshes_[slotIndex].generation != generation ||
        !meshes_[slotIndex].value.has_value()) {
        return false;
    }
    meshes_[slotIndex].value = std::move(desc);
    return true;
}

bool RenderScene::UnregisterMesh(rhi::RHIMesh mesh) {
    rhi::u32 slotIndex = 0;
    rhi::u32 generation = 0;
    if (!DecodeHandle(mesh.value, &slotIndex, &generation) ||
        slotIndex >= meshes_.size() ||
        meshes_[slotIndex].generation != generation ||
        !meshes_[slotIndex].value.has_value()) {
        return false;
    }
    meshes_[slotIndex].value.reset();
    AdvanceGeneration(&meshes_[slotIndex].generation);
    freeMeshSlots_.push_back(slotIndex);
    return true;
}

const rhi::RHIMeshDesc* RenderScene::FindMesh(rhi::RHIMesh mesh) const noexcept {
    rhi::u32 slotIndex = 0;
    rhi::u32 generation = 0;
    if (!DecodeHandle(mesh.value, &slotIndex, &generation) ||
        slotIndex >= meshes_.size() ||
        meshes_[slotIndex].generation != generation ||
        !meshes_[slotIndex].value.has_value()) {
        return nullptr;
    }
    return &*meshes_[slotIndex].value;
}

rhi::RHIMaterial RenderScene::RegisterMaterial(rhi::RHIMaterialDesc desc) {
    rhi::u32 slotIndex = 0;
    if (!freeMaterialSlots_.empty()) {
        slotIndex = freeMaterialSlots_.back();
        freeMaterialSlots_.pop_back();
        materials_[slotIndex].value = std::move(desc);
    } else {
        slotIndex = static_cast<rhi::u32>(materials_.size());
        materials_.push_back(MaterialSlot{std::move(desc), 1});
    }
    return rhi::RHIMaterial(EncodeHandle(slotIndex, materials_[slotIndex].generation));
}

bool RenderScene::UpdateMaterial(rhi::RHIMaterial material, rhi::RHIMaterialDesc desc) {
    rhi::u32 slotIndex = 0;
    rhi::u32 generation = 0;
    if (!DecodeHandle(material.value, &slotIndex, &generation) ||
        slotIndex >= materials_.size() ||
        materials_[slotIndex].generation != generation ||
        !materials_[slotIndex].value.has_value()) {
        return false;
    }
    materials_[slotIndex].value = std::move(desc);
    return true;
}

bool RenderScene::UnregisterMaterial(rhi::RHIMaterial material) {
    rhi::u32 slotIndex = 0;
    rhi::u32 generation = 0;
    if (!DecodeHandle(material.value, &slotIndex, &generation) ||
        slotIndex >= materials_.size() ||
        materials_[slotIndex].generation != generation ||
        !materials_[slotIndex].value.has_value()) {
        return false;
    }
    materials_[slotIndex].value.reset();
    AdvanceGeneration(&materials_[slotIndex].generation);
    freeMaterialSlots_.push_back(slotIndex);
    return true;
}

const rhi::RHIMaterialDesc* RenderScene::FindMaterial(rhi::RHIMaterial material) const noexcept {
    rhi::u32 slotIndex = 0;
    rhi::u32 generation = 0;
    if (!DecodeHandle(material.value, &slotIndex, &generation) ||
        slotIndex >= materials_.size() ||
        materials_[slotIndex].generation != generation ||
        !materials_[slotIndex].value.has_value()) {
        return nullptr;
    }
    return &*materials_[slotIndex].value;
}

RenderObjectHandle RenderScene::CreateObject(rhi::RHIRenderObjectDesc object) {
    rhi::u32 slotIndex = 0;
    if (!freeObjectSlots_.empty()) {
        slotIndex = freeObjectSlots_.back();
        freeObjectSlots_.pop_back();
        objects_[slotIndex].value = std::move(object);
    } else {
        slotIndex = static_cast<rhi::u32>(objects_.size());
        objects_.push_back(ObjectSlot{std::move(object), 1});
    }
    ++activeObjectCount_;
    return RenderObjectHandle(EncodeHandle(slotIndex, objects_[slotIndex].generation));
}

bool RenderScene::UpdateObject(RenderObjectHandle handle, rhi::RHIRenderObjectDesc object) {
    rhi::u32 slotIndex = 0;
    rhi::u32 generation = 0;
    if (!DecodeHandle(handle.value, &slotIndex, &generation) ||
        slotIndex >= objects_.size() ||
        objects_[slotIndex].generation != generation ||
        !objects_[slotIndex].value.has_value()) {
        return false;
    }
    objects_[slotIndex].value = std::move(object);
    return true;
}

bool RenderScene::DestroyObject(RenderObjectHandle handle) {
    rhi::u32 slotIndex = 0;
    rhi::u32 generation = 0;
    if (!DecodeHandle(handle.value, &slotIndex, &generation) ||
        slotIndex >= objects_.size() ||
        objects_[slotIndex].generation != generation ||
        !objects_[slotIndex].value.has_value()) {
        return false;
    }
    objects_[slotIndex].value.reset();
    AdvanceGeneration(&objects_[slotIndex].generation);
    freeObjectSlots_.push_back(slotIndex);
    --activeObjectCount_;
    return true;
}

const rhi::RHIRenderObjectDesc* RenderScene::FindObject(RenderObjectHandle handle) const noexcept {
    rhi::u32 slotIndex = 0;
    rhi::u32 generation = 0;
    if (!DecodeHandle(handle.value, &slotIndex, &generation) ||
        slotIndex >= objects_.size() ||
        objects_[slotIndex].generation != generation ||
        !objects_[slotIndex].value.has_value()) {
        return nullptr;
    }
    return &*objects_[slotIndex].value;
}

rhi::u32 RenderScene::ObjectSlotCount() const noexcept {
    return static_cast<rhi::u32>(objects_.size());
}

RenderObjectSlotView RenderScene::ObjectAtSlot(rhi::u32 slotIndex) const noexcept {
    if (slotIndex >= objects_.size() || !objects_[slotIndex].value.has_value()) {
        return {};
    }
    return {
        RenderObjectHandle(EncodeHandle(slotIndex, objects_[slotIndex].generation)),
        &*objects_[slotIndex].value};
}

rhi::u32 RenderScene::ActiveObjectCount() const noexcept {
    return activeObjectCount_;
}

void RenderScene::SetMainCamera(const rhi::RHICameraData& camera) noexcept {
    cameras_.main = camera;
}

rhi::u32 RenderScene::AddAdditionalCamera(const rhi::RHICameraData& camera) {
    cameras_.additional.push_back(camera);
    return static_cast<rhi::u32>(cameras_.additional.size() - 1);
}

bool RenderScene::RemoveAdditionalCamera(rhi::u32 cameraIndex) {
    if (cameraIndex >= cameras_.additional.size()) {
        return false;
    }
    cameras_.additional.erase(cameras_.additional.begin() + cameraIndex);
    return true;
}

void RenderScene::ClearAdditionalCameras() noexcept {
    cameras_.additional.clear();
}

rhi::u32 RenderScene::AddLight(const rhi::RHILightData& light) {
    lights_.items.push_back(light);
    return static_cast<rhi::u32>(lights_.items.size() - 1);
}

bool RenderScene::UpdateLight(rhi::u32 lightIndex, const rhi::RHILightData& light) {
    if (lightIndex >= lights_.items.size()) {
        return false;
    }
    lights_.items[lightIndex] = light;
    return true;
}

bool RenderScene::RemoveLight(rhi::u32 lightIndex) {
    if (lightIndex >= lights_.items.size()) {
        return false;
    }
    lights_.items.erase(lights_.items.begin() + lightIndex);
    return true;
}

void RenderScene::ClearLights() noexcept {
    lights_.items.clear();
}

void RenderScene::SetEnvironment(const rhi::RHISceneEnvironmentDesc& environment) noexcept {
    environment_ = environment;
}

const rhi::RHIRenderCameraSetDesc& RenderScene::Cameras() const noexcept {
    return cameras_;
}

const rhi::RHIRenderLightSetDesc& RenderScene::Lights() const noexcept {
    return lights_;
}

const rhi::RHISceneEnvironmentDesc& RenderScene::Environment() const noexcept {
    return environment_;
}

} // namespace renderer
