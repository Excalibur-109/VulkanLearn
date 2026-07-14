#include "Archetype.hpp"

#include <algorithm>
#include <cassert>

namespace ecs {

Archetype::Archetype(Signature signature, const ComponentRegistry& registry)
    : signature_(std::move(signature)) {
    const std::vector<ComponentTypeId> componentTypes = signature_.ComponentTypes();
    componentInfos_.reserve(componentTypes.size());
    for (const ComponentTypeId componentType : componentTypes) {
        componentInfos_.push_back(std::addressof(registry.Get(componentType)));
    }

    // Signature::ComponentTypes 按 id 递增返回，ChunkLayout 的列也保持有序，便于二分查找。
    layout_ = BuildChunkLayout(componentInfos_);
}

ChunkLocation Archetype::Allocate(Entity entity) {
    auto iterator = std::find_if(chunks_.begin(), chunks_.end(), [](const auto& chunk) {
        return !chunk->Full();
    });

    if (iterator == chunks_.end()) {
        chunks_.push_back(std::make_unique<Chunk>(layout_));
        iterator = std::prev(chunks_.end());
    }

    Chunk& chunk = **iterator;
    const u32 row = chunk.AllocateUninitialized(entity);
    ++entityCount_;
    return ChunkLocation{std::addressof(chunk), row};
}

std::optional<Entity> Archetype::Remove(Chunk& chunk, u32 row) noexcept {
    assert(entityCount_ > 0);
    --entityCount_;
    return chunk.RemoveRow(row);
}

} // namespace ecs

