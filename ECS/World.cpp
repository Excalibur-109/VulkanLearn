#include "World.hpp"

#include <limits>

namespace ecs {

World::World() {
    (void)GetOrCreateArchetype(Signature{});
}

bool World::DestroyEntity(Entity entity) {
    AssertStructureCanChange();
    EntityRecord* record = TryRecord(entity);
    if (record == nullptr) {
        return false;
    }

    UpdateRecordAfterSourceRemoval(*record->archetype, ChunkLocation{record->chunk, record->row});
    record->alive     = false;
    record->archetype = nullptr;
    record->chunk     = nullptr;
    record->row       = 0;
    ++record->generation;
    if (record->generation == 0) {
        record->generation = 1;
    }

    freeEntityIndices_.push_back(entity.index);
    --entityCount_;
    return true;
}

bool World::IsAlive(Entity entity) const noexcept {
    return TryRecord(entity) != nullptr;
}

const Signature& World::GetSignature(Entity entity) const {
    return RequireRecord(entity).archetype->GetSignature();
}

void World::Clear() {
    AssertStructureCanChange();
    archetypes_.clear();
    freeEntityIndices_.clear();

    // 不能直接清空 records_ 后让 generation 从 1 重新开始，否则 Clear 前的 Entity{0, 1}
    // 可能在下一次 CreateEntity 后重新指向新实体。保留槽位并推进 generation，保证所有
    // 已经发给外部的句柄永久失效。
    freeEntityIndices_.reserve(records_.size());
    for (u32 index = 0; index < records_.size(); ++index) {
        EntityRecord& record = records_[index];
        record.alive         = false;
        record.archetype     = nullptr;
        record.chunk         = nullptr;
        record.row           = 0;
        ++record.generation;
        if (record.generation == 0) {
            record.generation = 1;
        }
        freeEntityIndices_.push_back(index);
    }

    entityCount_ = 0;
    (void)GetOrCreateArchetype(Signature{});
}

Entity World::AllocateEntitySlot() {
    if (!freeEntityIndices_.empty()) {
        const u32 index = freeEntityIndices_.back();
        freeEntityIndices_.pop_back();
        return Entity{index, records_[index].generation};
    }

    if (records_.size() >= INVALID_ENTITY_INDEX) {
        throw std::overflow_error("The ECS entity index space is exhausted");
    }

    const u32 index = static_cast<u32>(records_.size());
    records_.push_back(EntityRecord{});
    return Entity{index, records_.back().generation};
}

void World::RecycleUncommittedEntity(Entity entity) noexcept {
    EntityRecord& record = records_[entity.index];
    record.alive         = false;
    record.archetype     = nullptr;
    record.chunk         = nullptr;
    record.row           = 0;
    freeEntityIndices_.push_back(entity.index);
}

Archetype& World::GetOrCreateArchetype(const Signature& signature) {
    if (const auto iterator = archetypes_.find(signature); iterator != archetypes_.end()) {
        return *iterator->second;
    }

    auto archetype = std::make_unique<Archetype>(signature, components_);
    Archetype* result = archetype.get();
    archetypes_.emplace(signature, std::move(archetype));
    ++archetypeVersion_;
    return *result;
}

World::EntityRecord& World::RequireRecord(Entity entity) {
    if (EntityRecord* record = TryRecord(entity); record != nullptr) {
        return *record;
    }
    throw std::out_of_range("The ECS entity handle is stale or invalid");
}

const World::EntityRecord& World::RequireRecord(Entity entity) const {
    if (const EntityRecord* record = TryRecord(entity); record != nullptr) {
        return *record;
    }
    throw std::out_of_range("The ECS entity handle is stale or invalid");
}

World::EntityRecord* World::TryRecord(Entity entity) noexcept {
    if (!entity.IsValid() || entity.index >= records_.size()) {
        return nullptr;
    }
    EntityRecord& record = records_[entity.index];
    return record.alive && record.generation == entity.generation ? std::addressof(record) : nullptr;
}

const World::EntityRecord* World::TryRecord(Entity entity) const noexcept {
    return const_cast<World*>(this)->TryRecord(entity);
}

void World::AssertStructureCanChange() const {
    if (activeIterations_ != 0) {
        throw std::logic_error(
            "ECS structural changes are forbidden during query iteration; use CommandBuffer");
    }
}

void World::UpdateRecordAfterSourceRemoval(
    Archetype& archetype,
    const ChunkLocation& location) noexcept {
    const std::optional<Entity> movedEntity = archetype.Remove(*location.chunk, location.row);
    if (movedEntity.has_value()) {
        EntityRecord& movedRecord = records_[movedEntity->index];
        movedRecord.chunk         = location.chunk;
        movedRecord.row           = location.row;
    }
}

} // namespace ecs
