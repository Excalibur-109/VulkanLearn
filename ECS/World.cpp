#include "World.hpp"

#include <algorithm>
#include <mutex>

namespace ecs {
namespace {

struct ComponentPackHash {
    [[nodiscard]] std::size_t operator()(
        const std::vector<ComponentTypeId>& componentTypes) const noexcept {
        std::size_t seed = componentTypes.size();
        for (const ComponentTypeId componentType : componentTypes) {
            seed ^= std::hash<ComponentTypeId>{}(componentType) +
                    static_cast<std::size_t>(0x9e3779b9U) + (seed << 6U) + (seed >> 2U);
        }
        return seed;
    }
};

struct CreationPackRegistry {
    std::mutex mutex;
    std::unordered_map<std::vector<ComponentTypeId>, u32, ComponentPackHash> ids;
    u32 nextId = 0;
};

CreationPackRegistry& GetCreationPackRegistry() {
    static CreationPackRegistry registry;
    return registry;
}

} // namespace

u32 detail::AcquireCreationPackId(
    const ComponentTypeId* componentTypes,
    std::size_t componentCount) {
    if (componentCount != 0 && componentTypes == nullptr) {
        throw std::invalid_argument("Component type array cannot be null");
    }

    std::vector<ComponentTypeId> key;
    if (componentCount != 0) {
        key.assign(componentTypes, componentTypes + componentCount);
        std::sort(key.begin(), key.end());
        if (std::adjacent_find(key.begin(), key.end()) != key.end()) {
            throw std::invalid_argument("A component creation pack cannot contain duplicates");
        }
    }

    CreationPackRegistry& registry = GetCreationPackRegistry();
    std::lock_guard lock(registry.mutex);
    if (const auto iterator = registry.ids.find(key); iterator != registry.ids.end()) {
        return iterator->second;
    }
    if (registry.nextId == std::numeric_limits<u32>::max()) {
        throw std::overflow_error("The ECS creation pack id space is exhausted");
    }

    const u32 id = registry.nextId++;
    registry.ids.emplace(std::move(key), id);
    return id;
}

World::World() {
    (void)GetOrCreateArchetype(Signature{});
}

bool World::DestroyEntity(Entity entity) {
    AssertStructureCanChange();
    EntityRecord* record = TryRecord(entity);
    if (record == nullptr) {
        return false;
    }

    UpdateAfterRemoval(*record->archetype, ChunkLocation{record->chunk, record->row});
    record->archetype = nullptr;
    record->chunk     = nullptr;
    record->row       = 0;
    if (++record->generation == 0) {
        record->generation = 1;
    }
    --entityCount_;
    // push_back 若因内存不足失败，实体已经正确死亡且计数正确，只是该槽位暂不复用。
    freeEntityIndices_.push_back(entity.index);
    return true;
}

bool World::IsAlive(Entity entity) const noexcept {
    return TryRecord(entity) != nullptr;
}

const Signature& World::GetSignature(Entity entity) const {
    return RequireRecord(entity).archetype->GetSignature();
}

World::~World() = default;

void World::ReserveEntities(std::size_t capacity) {
    AssertStructureCanChange();
    records_.reserve(capacity);
    freeEntityIndices_.reserve(capacity);
}

void World::Clear() {
    AssertStructureCanChange();
    // 先完成唯一可能抛异常的扩容，再销毁 Archetype，保持失败时 World 仍可使用。
    freeEntityIndices_.reserve(records_.size());
    archetypes_.clear();
    std::fill(creationArchetypes_.begin(), creationArchetypes_.end(), nullptr);
    freeEntityIndices_.clear();

    // 保留槽位并推进 generation，Clear 前的句柄以后也不会重新指向新实体。
    for (u32 index = 0; index < records_.size(); ++index) {
        EntityRecord& record = records_[index];
        record.archetype     = nullptr;
        record.chunk         = nullptr;
        record.row           = 0;
        if (++record.generation == 0) {
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

    // 每个实体最多进入 free-list 一次。提前保证 capacity 后，Destroy 和构造回滚中的
    // push_back 不再分配内存，可以安全完成状态收尾。
    if (freeEntityIndices_.capacity() < records_.size() + 1U) {
        const std::size_t doubled = std::max<std::size_t>(1, freeEntityIndices_.capacity() * 2U);
        freeEntityIndices_.reserve(std::max(records_.size() + 1U, doubled));
    }
    const u32 index = static_cast<u32>(records_.size());
    records_.push_back(EntityRecord{});
    return Entity{index, records_.back().generation};
}

void World::RecycleUncommittedEntity(Entity entity) noexcept {
    EntityRecord& record = records_[entity.index];
    record.archetype     = nullptr;
    record.chunk         = nullptr;
    record.row           = 0;
    freeEntityIndices_.push_back(entity.index);
}

Archetype& World::GetOrCreateArchetype(const Signature& signature) {
    if (const auto iterator = archetypes_.find(signature); iterator != archetypes_.end()) {
        return *iterator->second;
    }
    auto archetype   = std::make_unique<Archetype>(signature, components_);
    Archetype* result = archetype.get();
    archetypes_.emplace(signature, std::move(archetype));
    ++archetypeVersion_;
    return *result;
}

ArchetypeTransition& World::GetAddTransition(
    Archetype& source,
    ComponentTypeId componentType) {
    if (componentType < source.addTransitions_.size() &&
        source.addTransitions_[componentType] != nullptr) {
        return *source.addTransitions_[componentType];
    }

    Signature targetSignature = source.GetSignature();
    targetSignature.Set(componentType);
    Archetype& target = GetOrCreateArchetype(targetSignature);
    auto forward      = BuildTransition(source, target, componentType);
    auto reverse      = BuildTransition(target, source, componentType);

    source.addTransitions_.resize(
        std::max<std::size_t>(source.addTransitions_.size(), componentType + 1U));
    target.removeTransitions_.resize(
        std::max<std::size_t>(target.removeTransitions_.size(), componentType + 1U));
    source.addTransitions_[componentType]     = std::move(forward);
    target.removeTransitions_[componentType] = std::move(reverse);
    return *source.addTransitions_[componentType];
}

ArchetypeTransition& World::GetRemoveTransition(
    Archetype& source,
    ComponentTypeId componentType) {
    if (componentType < source.removeTransitions_.size() &&
        source.removeTransitions_[componentType] != nullptr) {
        return *source.removeTransitions_[componentType];
    }

    Signature targetSignature = source.GetSignature();
    targetSignature.Reset(componentType);
    Archetype& target = GetOrCreateArchetype(targetSignature);
    auto forward      = BuildTransition(source, target, componentType);
    auto reverse      = BuildTransition(target, source, componentType);

    source.removeTransitions_.resize(
        std::max<std::size_t>(source.removeTransitions_.size(), componentType + 1U));
    target.addTransitions_.resize(
        std::max<std::size_t>(target.addTransitions_.size(), componentType + 1U));
    source.removeTransitions_[componentType] = std::move(forward);
    target.addTransitions_[componentType]    = std::move(reverse);
    return *source.removeTransitions_[componentType];
}

std::unique_ptr<ArchetypeTransition> World::BuildTransition(
    Archetype& source,
    Archetype& target,
    ComponentTypeId changedType) {
    auto transition    = std::make_unique<ArchetypeTransition>();
    transition->target = std::addressof(target);

    if (const ComponentColumn* changedColumn = target.layout_.FindColumn(changedType);
        changedColumn != nullptr) {
        transition->changedTargetOffset = changedColumn->offset;
    }

    transition->moves.reserve(
        std::min(source.componentInfos_.size(), target.componentInfos_.size()));
    for (const ComponentInfo* info : source.componentInfos_) {
        const ComponentColumn* targetColumn = target.layout_.FindColumn(info->id);
        if (targetColumn == nullptr) {
            continue;
        }
        const ComponentColumn* sourceColumn = source.layout_.FindColumn(info->id);
        transition->moves.push_back(ArchetypeColumnMove{
            info,
            sourceColumn->offset,
            targetColumn->offset});
    }
    return transition;
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
    return record.chunk != nullptr && record.generation == entity.generation
               ? std::addressof(record)
               : nullptr;
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

void World::UpdateAfterRemoval(Archetype& archetype, const ChunkLocation& location) noexcept {
    const std::optional<Entity> moved = archetype.Remove(*location.chunk, location.row);
    if (moved.has_value()) {
        EntityRecord& record = records_[moved->index];
        record.chunk         = location.chunk;
        record.row           = location.row;
    }
}

} // namespace ecs
