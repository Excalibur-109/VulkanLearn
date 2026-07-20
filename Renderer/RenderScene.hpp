#pragma once

#include "RHI.hpp"

#include <optional>
#include <vector>

namespace renderer {

/**
 * @brief 场景中可渲染实例的稳定句柄。
 *
 * 低 32 位保存 slot + 1，高 32 位保存 generation。删除对象后 slot 可以复用，
 * 但 generation 会递增，因此旧句柄不会误指向后来占用同一 slot 的新对象。
 */
struct RenderObjectHandle {
    rhi::u64 value = rhi::RHI_INVALID_HANDLE_VALUE;

    constexpr RenderObjectHandle() noexcept = default;
    explicit constexpr RenderObjectHandle(rhi::u64 handleValue) noexcept
        : value(handleValue) {
    }

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return value != rhi::RHI_INVALID_HANDLE_VALUE;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return IsValid();
    }

    friend constexpr bool operator==(RenderObjectHandle lhs, RenderObjectHandle rhs) noexcept {
        return lhs.value == rhs.value;
    }
};

/// 遍历对象 slot 时返回的只读视图；object 只在 RenderScene 未发生修改时有效。
struct RenderObjectSlotView {
    RenderObjectHandle handle{};
    const rhi::RHIRenderObjectDesc* object = nullptr;
};

/**
 * @brief Renderer 的 CPU 场景数据库。
 *
 * Mesh、Material、Object、Camera、Light 仍然分开保存。RenderScene 不拥有 GPU
 * 资源，只登记已有 RHI 句柄及其描述。这样资产系统、ECS 和编辑器可以独立更新各类
 * 数据，而 RenderCollector 只在构建当前帧时读取它们。
 */
class RenderScene {
public:
    RenderScene() = default;

    /// 登记一个由调用方创建好 GPU buffer 的 mesh 描述，并返回 Renderer 逻辑句柄。
    [[nodiscard]] rhi::RHIMesh RegisterMesh(rhi::RHIMeshDesc desc);
    [[nodiscard]] bool UpdateMesh(rhi::RHIMesh mesh, rhi::RHIMeshDesc desc);
    [[nodiscard]] bool UnregisterMesh(rhi::RHIMesh mesh);
    [[nodiscard]] const rhi::RHIMeshDesc* FindMesh(rhi::RHIMesh mesh) const noexcept;

    /// 登记材质。材质继续引用原 RHI 创建的 Pipeline、BindSet、TextureView 和 Sampler。
    [[nodiscard]] rhi::RHIMaterial RegisterMaterial(rhi::RHIMaterialDesc desc);
    [[nodiscard]] bool UpdateMaterial(rhi::RHIMaterial material, rhi::RHIMaterialDesc desc);
    [[nodiscard]] bool UnregisterMaterial(rhi::RHIMaterial material);
    [[nodiscard]] const rhi::RHIMaterialDesc* FindMaterial(rhi::RHIMaterial material) const noexcept;

    /// 添加一个实例。实例只引用 mesh/material，不复制体积较大的资源描述。
    [[nodiscard]] RenderObjectHandle CreateObject(rhi::RHIRenderObjectDesc object);
    [[nodiscard]] bool UpdateObject(RenderObjectHandle handle, rhi::RHIRenderObjectDesc object);
    [[nodiscard]] bool DestroyObject(RenderObjectHandle handle);
    [[nodiscard]] const rhi::RHIRenderObjectDesc* FindObject(RenderObjectHandle handle) const noexcept;

    /// slot 接口让收集器可以无额外临时分配地线性扫描全部对象。
    [[nodiscard]] rhi::u32 ObjectSlotCount() const noexcept;
    [[nodiscard]] RenderObjectSlotView ObjectAtSlot(rhi::u32 slotIndex) const noexcept;
    [[nodiscard]] rhi::u32 ActiveObjectCount() const noexcept;

    void SetMainCamera(const rhi::RHICameraData& camera) noexcept;
    [[nodiscard]] rhi::u32 AddAdditionalCamera(const rhi::RHICameraData& camera);
    [[nodiscard]] bool RemoveAdditionalCamera(rhi::u32 cameraIndex);
    void ClearAdditionalCameras() noexcept;

    [[nodiscard]] rhi::u32 AddLight(const rhi::RHILightData& light);
    [[nodiscard]] bool UpdateLight(rhi::u32 lightIndex, const rhi::RHILightData& light);
    [[nodiscard]] bool RemoveLight(rhi::u32 lightIndex);
    void ClearLights() noexcept;

    void SetEnvironment(const rhi::RHISceneEnvironmentDesc& environment) noexcept;

    [[nodiscard]] const rhi::RHIRenderCameraSetDesc& Cameras() const noexcept;
    [[nodiscard]] const rhi::RHIRenderLightSetDesc& Lights() const noexcept;
    [[nodiscard]] const rhi::RHISceneEnvironmentDesc& Environment() const noexcept;

private:
    struct MeshSlot {
        std::optional<rhi::RHIMeshDesc> value;
        rhi::u32 generation = 1;
    };

    struct MaterialSlot {
        std::optional<rhi::RHIMaterialDesc> value;
        rhi::u32 generation = 1;
    };

    struct ObjectSlot {
        std::optional<rhi::RHIRenderObjectDesc> value;
        rhi::u32 generation = 1;
    };

    [[nodiscard]] static rhi::u64 EncodeHandle(rhi::u32 slotIndex, rhi::u32 generation) noexcept;
    [[nodiscard]] static bool DecodeHandle(rhi::u64 value, rhi::u32* slotIndex, rhi::u32* generation) noexcept;
    static void AdvanceGeneration(rhi::u32* generation) noexcept;

    std::vector<MeshSlot> meshes_;
    std::vector<rhi::u32> freeMeshSlots_;
    std::vector<MaterialSlot> materials_;
    std::vector<rhi::u32> freeMaterialSlots_;
    std::vector<ObjectSlot> objects_;
    std::vector<rhi::u32> freeObjectSlots_;
    rhi::u32 activeObjectCount_ = 0;

    rhi::RHIRenderCameraSetDesc cameras_{};
    rhi::RHIRenderLightSetDesc lights_{};
    rhi::RHISceneEnvironmentDesc environment_{};
};

} // namespace renderer
