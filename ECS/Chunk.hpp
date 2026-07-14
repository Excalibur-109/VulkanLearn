#pragma once

#include "Component.hpp"

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
 * @brief 一个 Archetype 的固定 Chunk 布局。
 *
 * 同一 Archetype 的所有 Chunk 共享此布局。例如 capacity=128 时，内存依次为：
 * [128 个 Entity][对齐填充][128 个 Position][对齐填充][128 个 Velocity]。
 */
struct ChunkLayout {
    u32 capacity                = 0;
    std::size_t storageSize     = 0;
    std::size_t storageAlignment = alignof(std::max_align_t);
    std::size_t entityOffset    = 0;
    std::vector<ComponentColumn> columns;

    [[nodiscard]] const ComponentColumn* FindColumn(ComponentTypeId componentType) const noexcept;
};

[[nodiscard]] ChunkLayout BuildChunkLayout(
    std::span<const ComponentInfo* const> componentInfos,
    std::size_t targetChunkSize = DEFAULT_CHUNK_SIZE);

/**
 * @brief Archetype 中的一块固定容量 SoA 存储。
 *
 * Chunk 不单独分配每个组件数组，而是只分配一次对齐的大内存，再通过 ChunkLayout 的
 * offset 找到各列。这样可以减少堆分配，并让系统线性遍历某一组件列时更容易命中缓存。
 */
class Chunk {
public:
    explicit Chunk(const ChunkLayout& layout);
    ~Chunk();

    Chunk(const Chunk&)            = delete;
    Chunk& operator=(const Chunk&) = delete;
    Chunk(Chunk&&)                 = delete;
    Chunk& operator=(Chunk&&)      = delete;

    [[nodiscard]] u32 Count() const noexcept {
        return count_;
    }

    [[nodiscard]] u32 Capacity() const noexcept {
        return layout_->capacity;
    }

    [[nodiscard]] bool Full() const noexcept {
        return count_ == Capacity();
    }

    [[nodiscard]] Entity EntityAt(u32 row) const;
    [[nodiscard]] std::span<const Entity> Entities() const noexcept;
    [[nodiscard]] void* ComponentAt(ComponentTypeId componentType, u32 row);
    [[nodiscard]] const void* ComponentAt(ComponentTypeId componentType, u32 row) const;
    [[nodiscard]] void* ComponentColumnData(ComponentTypeId componentType);
    [[nodiscard]] const void* ComponentColumnData(ComponentTypeId componentType) const;

private:
    friend class Archetype;
    friend class World;

    // 只预留一行并写入 Entity；该行的组件随后由 World 逐列构造。
    [[nodiscard]] u32 AllocateUninitialized(Entity entity);
    void RollbackLastRow(std::span<const ComponentTypeId> constructedTypes) noexcept;

    // 删除 row，并将最后一行移动到空位。返回被换到 row 的实体。
    [[nodiscard]] std::optional<Entity> RemoveRow(u32 row) noexcept;

    [[nodiscard]] Entity* EntityData() noexcept;
    [[nodiscard]] const Entity* EntityData() const noexcept;

    const ChunkLayout* layout_ = nullptr;
    std::byte* storage_        = nullptr;
    u32 count_                 = 0;
};

struct ChunkLocation {
    Chunk* chunk = nullptr;
    u32 row      = 0;
};

} // namespace ecs
