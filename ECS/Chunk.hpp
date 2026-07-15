#pragma once

#include "Component.hpp"
#include "Memory.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace ecs {

inline constexpr std::size_t DEFAULT_CHUNK_SIZE = 16U * 1024U;

struct ComponentColumn {
    ComponentTypeId componentType = INVALID_COMPONENT_TYPE;
    const ComponentInfo* info     = nullptr;
    std::size_t offset            = 0;
};

/**
 * 同一 Archetype 的 Chunk 共享布局：
 * [Entity x capacity][padding][ComponentA x capacity][padding][ComponentB x capacity]
 */
struct ChunkLayout {
    u32 capacity                 = 0;
    std::size_t storageSize      = 0;
    std::size_t storageAlignment = alignof(std::max_align_t);
    std::size_t entityOffset     = 0;
    std::vector<ComponentColumn> columns;
    std::vector<u32> columnIndices;

    [[nodiscard]] ECS_API const ComponentColumn* FindColumn(
        ComponentTypeId componentType) const noexcept;
};

[[nodiscard]] ECS_API ChunkLayout BuildChunkLayout(
    std::span<const ComponentInfo* const> componentInfos,
    std::size_t targetChunkSize = DEFAULT_CHUNK_SIZE);

/**
 * Chunk 是原始组件内存的 RAII 边界。storage_ 来自 malloc/aligned C 堆，Chunk 析构时
 * 逐对象析构后释放；不会为每个组件列单独分配。
 */
class Chunk {
public:
    ECS_API explicit Chunk(const ChunkLayout& layout);
    ECS_API ~Chunk();

    Chunk(const Chunk&)            = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&)                 = delete;
    Chunk& operator=(Chunk&&)      = delete;

    [[nodiscard]] u32 Count() const noexcept { return count_; }
    [[nodiscard]] u32 Capacity() const noexcept { return layout_->capacity; }
    [[nodiscard]] bool Full() const noexcept { return count_ == Capacity(); }
    [[nodiscard]] ECS_API Entity EntityAt(u32 row) const;
    [[nodiscard]] ECS_API std::span<const Entity> Entities() const noexcept;
    [[nodiscard]] ECS_API void* ComponentAt(ComponentTypeId componentType, u32 row);
    [[nodiscard]] ECS_API const void* ComponentAt(
        ComponentTypeId componentType,
        u32 row) const;
    [[nodiscard]] ECS_API void* ComponentColumnData(ComponentTypeId componentType);

private:
    friend class Archetype;
    friend class World;
    template <typename... Components>
    friend class Query;

    [[nodiscard]] u32 AllocateUninitialized(Entity entity);
    ECS_API void RollbackLastRow(
        std::span<const ComponentTypeId> constructedTypes) noexcept;
    [[nodiscard]] std::optional<Entity> RemoveRow(u32 row) noexcept;
    [[nodiscard]] Entity MoveLastRowTo(Chunk& destination, u32 destinationRow) noexcept;
    [[nodiscard]] ECS_API Entity* EntityData() noexcept;
    [[nodiscard]] ECS_API const Entity* EntityData() const noexcept;

    const ChunkLayout* layout_ = nullptr;
    std::byte* storage_        = nullptr;
    u32 count_                 = 0;
};

struct ChunkLocation {
    Chunk* chunk = nullptr;
    u32 row      = 0;
};

} // namespace ecs
