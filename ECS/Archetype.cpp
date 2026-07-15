#include "Archetype.hpp"

#include <cassert>

namespace ecs {

Archetype::Archetype(Signature signature, const ComponentRegistry& registry)
    : signature_(std::move(signature)) {
    const std::vector<ComponentTypeId> types = signature_.ComponentTypes();
    componentInfos_.reserve(types.size());
    for (const ComponentTypeId type : types) {
        componentInfos_.push_back(std::addressof(registry.Get(type)));
    }
    layout_ = BuildChunkLayout(componentInfos_);
}

ChunkLocation Archetype::Allocate(Entity entity) {
    if (chunks_.empty() || chunks_.back()->Full()) {
        chunks_.push_back(std::make_unique<Chunk>(layout_));
    }

    Chunk* chunk = chunks_.back().get();
    const u32 row = chunk->AllocateUninitialized(entity);
    ++entityCount_;
    return ChunkLocation{chunk, row};
}

std::optional<Entity> Archetype::Remove(Chunk& chunk, u32 row) noexcept {
    assert(entityCount_ > 0);
    assert(!chunks_.empty());

    Chunk* lastChunk = chunks_.back().get();
    std::optional<Entity> movedEntity;
    if (std::addressof(chunk) == lastChunk) {
        movedEntity = chunk.RemoveRow(row);
    } else {
        movedEntity = lastChunk->MoveLastRowTo(chunk, row);
    }

    --entityCount_;
    if (lastChunk->Count() == 0) {
        chunks_.pop_back();
    }
    return movedEntity;
}

Archetype::~Archetype() = default;

} // namespace ecs

