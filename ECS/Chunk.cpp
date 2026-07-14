#include "Chunk.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <new>
#include <stdexcept>

namespace ecs {
namespace {

[[nodiscard]] std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

[[nodiscard]] ChunkLayout BuildLayoutForCapacity(
    std::span<const ComponentInfo* const> componentInfos,
    u32 capacity) {
    ChunkLayout layout{};
    layout.capacity         = capacity;
    layout.storageAlignment = alignof(Entity);

    std::size_t offset = 0;
    offset = AlignUp(offset, alignof(Entity));
    layout.entityOffset = offset;
    offset += sizeof(Entity) * capacity;

    layout.columns.reserve(componentInfos.size());
    for (const ComponentInfo* info : componentInfos) {
        layout.storageAlignment = std::max(layout.storageAlignment, info->alignment);
        offset                  = AlignUp(offset, info->alignment);
        layout.columns.push_back(ComponentColumn{info->id, info, offset});
        offset += info->size * capacity;
    }

    layout.storageAlignment = std::max(layout.storageAlignment, alignof(std::max_align_t));
    layout.storageSize      = AlignUp(offset, layout.storageAlignment);
    return layout;
}

} // namespace

const ComponentColumn* ChunkLayout::FindColumn(ComponentTypeId componentType) const noexcept {
    const auto iterator = std::lower_bound(
        columns.begin(),
        columns.end(),
        componentType,
        [](const ComponentColumn& column, ComponentTypeId value) {
            return column.componentType < value;
        });
    return iterator != columns.end() && iterator->componentType == componentType
               ? std::addressof(*iterator)
               : nullptr;
}

ChunkLayout BuildChunkLayout(
    std::span<const ComponentInfo* const> componentInfos,
    std::size_t targetChunkSize) {
    if (targetChunkSize == 0) {
        throw std::invalid_argument("The ECS target chunk size must be greater than zero");
    }

    std::size_t bytesPerEntity = sizeof(Entity);
    for (const ComponentInfo* info : componentInfos) {
        bytesPerEntity += info->size;
    }

    const std::size_t estimatedCapacity =
        std::max<std::size_t>(1, targetChunkSize / std::max<std::size_t>(1, bytesPerEntity));
    const u32 firstCapacity = static_cast<u32>(std::min<std::size_t>(
        estimatedCapacity,
        std::numeric_limits<u32>::max()));

    // 对齐填充会让简单的 targetSize / bytesPerEntity 略微偏大，向下寻找首个可用容量。
    for (u32 capacity = firstCapacity; capacity > 1; --capacity) {
        ChunkLayout layout = BuildLayoutForCapacity(componentInfos, capacity);
        if (layout.storageSize <= targetChunkSize) {
            return layout;
        }
    }

    // 单个超大组件可能已经超过 16 KiB。仍允许 capacity=1，而不是拒绝这种组件。
    return BuildLayoutForCapacity(componentInfos, 1);
}

Chunk::Chunk(const ChunkLayout& layout)
    : layout_(&layout) {
    storage_ = static_cast<std::byte*>(
        ::operator new(layout.storageSize, std::align_val_t(layout.storageAlignment)));
}

Chunk::~Chunk() {
    for (u32 row = 0; row < count_; ++row) {
        for (const ComponentColumn& column : layout_->columns) {
            column.info->destroy(storage_ + column.offset + column.info->size * row);
        }
        std::destroy_at(EntityData() + row);
    }
    ::operator delete(storage_, std::align_val_t(layout_->storageAlignment));
}

Entity Chunk::EntityAt(u32 row) const {
    if (row >= count_) {
        throw std::out_of_range("The ECS chunk row is out of range");
    }
    return EntityData()[row];
}

std::span<const Entity> Chunk::Entities() const noexcept {
    return {EntityData(), count_};
}

void* Chunk::ComponentAt(ComponentTypeId componentType, u32 row) {
    if (row >= count_) {
        throw std::out_of_range("The ECS chunk row is out of range");
    }
    const ComponentColumn* column = layout_->FindColumn(componentType);
    if (column == nullptr) {
        throw std::out_of_range("The ECS component does not belong to this chunk");
    }
    return storage_ + column->offset + column->info->size * row;
}

const void* Chunk::ComponentAt(ComponentTypeId componentType, u32 row) const {
    return const_cast<Chunk*>(this)->ComponentAt(componentType, row);
}

void* Chunk::ComponentColumnData(ComponentTypeId componentType) {
    const ComponentColumn* column = layout_->FindColumn(componentType);
    if (column == nullptr) {
        throw std::out_of_range("The ECS component does not belong to this chunk");
    }
    return storage_ + column->offset;
}

const void* Chunk::ComponentColumnData(ComponentTypeId componentType) const {
    return const_cast<Chunk*>(this)->ComponentColumnData(componentType);
}

u32 Chunk::AllocateUninitialized(Entity entity) {
    if (Full()) {
        throw std::overflow_error("The ECS chunk has no free rows");
    }

    const u32 row = count_++;
    std::construct_at(EntityData() + row, entity);
    return row;
}

void Chunk::RollbackLastRow(std::span<const ComponentTypeId> constructedTypes) noexcept {
    assert(count_ > 0);
    const u32 row = count_ - 1U;
    for (auto iterator = constructedTypes.rbegin(); iterator != constructedTypes.rend(); ++iterator) {
        const ComponentColumn* column = layout_->FindColumn(*iterator);
        assert(column != nullptr);
        column->info->destroy(storage_ + column->offset + column->info->size * row);
    }
    std::destroy_at(EntityData() + row);
    --count_;
}

std::optional<Entity> Chunk::RemoveRow(u32 row) noexcept {
    assert(row < count_);
    const u32 lastRow = count_ - 1U;

    for (const ComponentColumn& column : layout_->columns) {
        void* removedValue = storage_ + column.offset + column.info->size * row;
        column.info->destroy(removedValue);

        if (row != lastRow) {
            void* lastValue = storage_ + column.offset + column.info->size * lastRow;
            column.info->moveConstruct(removedValue, lastValue);
            column.info->destroy(lastValue);
        }
    }

    std::optional<Entity> movedEntity;
    if (row != lastRow) {
        movedEntity      = EntityData()[lastRow];
        EntityData()[row] = *movedEntity;
    }

    std::destroy_at(EntityData() + lastRow);
    --count_;
    return movedEntity;
}

Entity* Chunk::EntityData() noexcept {
    return reinterpret_cast<Entity*>(storage_ + layout_->entityOffset);
}

const Entity* Chunk::EntityData() const noexcept {
    return reinterpret_cast<const Entity*>(storage_ + layout_->entityOffset);
}

} // namespace ecs
