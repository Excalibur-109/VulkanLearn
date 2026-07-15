#include "Chunk.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <stdexcept>

namespace ecs {
namespace {

[[nodiscard]] std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

[[nodiscard]] ChunkLayout BuildForCapacity(
    std::span<const ComponentInfo* const> componentInfos,
    u32 capacity) {
    ChunkLayout layout{};
    layout.capacity         = capacity;
    layout.storageAlignment = alignof(Entity);

    std::size_t offset = AlignUp(0, alignof(Entity));
    layout.entityOffset = offset;
    offset += sizeof(Entity) * capacity;

    layout.columns.reserve(componentInfos.size());
    for (const ComponentInfo* info : componentInfos) {
        layout.storageAlignment = std::max(layout.storageAlignment, info->alignment);
        offset                  = AlignUp(offset, info->alignment);
        if (layout.columnIndices.size() <= info->id) {
            layout.columnIndices.resize(
                static_cast<std::size_t>(info->id) + 1U,
                INVALID_COMPONENT_TYPE);
        }
        layout.columnIndices[info->id] = static_cast<u32>(layout.columns.size());
        layout.columns.push_back(ComponentColumn{info->id, info, offset});
        offset += info->size * capacity;
    }

    layout.storageAlignment = std::max(layout.storageAlignment, alignof(std::max_align_t));
    layout.storageSize      = AlignUp(offset, layout.storageAlignment);
    return layout;
}

} // namespace

const ComponentColumn* ChunkLayout::FindColumn(ComponentTypeId componentType) const noexcept {
    if (componentType >= columnIndices.size()) {
        return nullptr;
    }
    const u32 columnIndex = columnIndices[componentType];
    return columnIndex == INVALID_COMPONENT_TYPE ? nullptr : std::addressof(columns[columnIndex]);
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

    const std::size_t estimate =
        std::max<std::size_t>(1, targetChunkSize / std::max<std::size_t>(1, bytesPerEntity));
    const u32 firstCapacity = static_cast<u32>(
        std::min<std::size_t>(estimate, std::numeric_limits<u32>::max()));

    // 列起始对齐会增加少量 padding，因此从估算容量向下找到实际可容纳的最大值。
    for (u32 capacity = firstCapacity; capacity > 1; --capacity) {
        ChunkLayout layout = BuildForCapacity(componentInfos, capacity);
        if (layout.storageSize <= targetChunkSize) {
            return layout;
        }
    }
    return BuildForCapacity(componentInfos, 1);
}

Chunk::Chunk(const ChunkLayout& layout)
    : layout_(&layout),
      storage_(static_cast<std::byte*>(
          AllocateRawMemory(layout.storageSize, layout.storageAlignment))) {
}

Chunk::~Chunk() {
    for (u32 row = 0; row < count_; ++row) {
        for (const ComponentColumn& column : layout_->columns) {
            column.info->destroy(storage_ + column.offset + column.info->size * row);
        }
        std::destroy_at(EntityData() + row);
    }
    FreeRawMemory(storage_, layout_->storageAlignment);
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
        void* removed = storage_ + column.offset + column.info->size * row;
        column.info->destroy(removed);
        if (row != lastRow) {
            void* last = storage_ + column.offset + column.info->size * lastRow;
            column.info->moveConstruct(removed, last);
            column.info->destroy(last);
        }
    }

    std::optional<Entity> movedEntity;
    if (row != lastRow) {
        movedEntity       = EntityData()[lastRow];
        EntityData()[row] = *movedEntity;
    }
    std::destroy_at(EntityData() + lastRow);
    --count_;
    return movedEntity;
}

Entity Chunk::MoveLastRowTo(Chunk& destination, u32 destinationRow) noexcept {
    assert(this != std::addressof(destination));
    assert(count_ > 0);
    assert(destinationRow < destination.count_);

    const u32 sourceRow = count_ - 1U;
    for (const ComponentColumn& column : layout_->columns) {
        void* destinationValue =
            destination.storage_ + column.offset + column.info->size * destinationRow;
        void* sourceValue = storage_ + column.offset + column.info->size * sourceRow;
        column.info->destroy(destinationValue);
        column.info->moveConstruct(destinationValue, sourceValue);
        column.info->destroy(sourceValue);
    }

    const Entity movedEntity = EntityData()[sourceRow];
    destination.EntityData()[destinationRow] = movedEntity;
    std::destroy_at(EntityData() + sourceRow);
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
