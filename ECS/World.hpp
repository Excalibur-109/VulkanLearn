#pragma once

#include "Archetype.hpp"

#include <array>
#include <cassert>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ecs {

template <typename... Components>
class Query;

namespace detail {

/**
 * CreateEntity 的模板缓存也必须由 ECS.dll 分配 ID。若这里使用头文件内 atomic，EXE 和
 * 每个插件都会从 0 重新计数，不同组件组合可能撞到同一个 World 缓存槽。
 */
[[nodiscard]] ECS_API u32 AcquireCreationPackId(
    const ComponentTypeId* componentTypes,
    std::size_t componentCount);

template <typename... Components>
[[nodiscard]] u32 CreationPackId() {
    static const u32 id = [] {
        const std::array<ComponentTypeId, sizeof...(Components)> componentTypes{
            GetComponentTypeId<std::remove_cvref_t<Components>>()...};
        return AcquireCreationPackId(componentTypes.data(), componentTypes.size());
    }();
    return id;
}

template <typename... Types>
struct AreUnique : std::true_type {};

template <typename First, typename... Rest>
struct AreUnique<First, Rest...>
    : std::bool_constant<
          (!std::is_same_v<std::remove_cvref_t<First>, std::remove_cvref_t<Rest>> && ...) &&
          AreUnique<Rest...>::value> {};

} // namespace detail

/**
 * World 拥有注册表、Archetype 和 Entity 槽位，是结构变化的唯一入口。
 * 组件原始地址只在当前 Archetype/Chunk/row 有效，Add/Remove 后必须重新 Get。
 */
class World {
public:
    ECS_API World();
    ECS_API ~World();

    World(const World&)            = delete;
    World& operator=(const World&) = delete;
    World(World&&)                 = delete;
    World& operator=(World&&)      = delete;

    template <typename Component>
    ComponentTypeId RegisterComponent(std::string name = {}) {
        return components_.Register<Component>(std::move(name));
    }

    template <typename... Components>
    Entity CreateEntity(Components&&... components) {
        static_assert(
            detail::AreUnique<Components...>::value,
            "CreateEntity cannot contain the same component type more than once");
        AssertStructureCanChange();

        std::array<ComponentTypeId, sizeof...(Components)> types{
            components_.Ensure<std::remove_cvref_t<Components>>()...};
        const u32 packId = detail::CreationPackId<std::remove_cvref_t<Components>...>();
        if (creationArchetypes_.size() <= packId) {
            creationArchetypes_.resize(static_cast<std::size_t>(packId) + 1U, nullptr);
        }

        Archetype* archetype = creationArchetypes_[packId];
        if (archetype == nullptr) {
            Signature signature;
            for (const ComponentTypeId type : types) {
                signature.Set(type);
            }
            archetype = std::addressof(GetOrCreateArchetype(signature));
            creationArchetypes_[packId] = archetype;
        }

        std::array<ComponentTypeId, sizeof...(Components)> constructed{};
        std::size_t constructedCount = 0;

        const Entity entity  = AllocateEntitySlot();
        ChunkLocation location{};
        try {
            location = archetype->Allocate(entity);
        } catch (...) {
            RecycleUncommittedEntity(entity);
            throw;
        }

        try {
            (ConstructProvided(
                 location,
                 constructed,
                 constructedCount,
                 std::forward<Components>(components)),
             ...);
        } catch (...) {
            location.chunk->RollbackLastRow(
                std::span<const ComponentTypeId>{constructed.data(), constructedCount});
            --archetype->entityCount_;
            RecycleUncommittedEntity(entity);
            throw;
        }

        EntityRecord& record = records_[entity.index];
        record.archetype     = archetype;
        record.chunk         = location.chunk;
        record.row           = location.row;
        ++entityCount_;
        return entity;
    }

    [[nodiscard]] ECS_API bool DestroyEntity(Entity entity);
    [[nodiscard]] ECS_API bool IsAlive(Entity entity) const noexcept;

    template <typename Component, typename... Arguments>
    Component& Add(Entity entity, Arguments&&... arguments) {
        using Type = std::remove_cvref_t<Component>;
        static_assert(std::is_constructible_v<Type, Arguments...>);
        AssertStructureCanChange();

        EntityRecord& record        = RequireRecord(entity);
        const ComponentTypeId addId = components_.Ensure<Type>();
        if (record.archetype->Has(addId)) {
            throw std::logic_error("The entity already owns this ECS component");
        }

        Archetype* sourceArchetype = record.archetype;
        ArchetypeTransition& transition = GetAddTransition(*sourceArchetype, addId);
        Archetype& targetArchetype = *transition.target;
        const ChunkLocation target = targetArchetype.Allocate(entity);
        const ChunkLocation source{record.chunk, record.row};

        try {
            void* destination = target.chunk->storage_ + transition.changedTargetOffset +
                                sizeof(Type) * target.row;
            ::new (destination) Type(std::forward<Arguments>(arguments)...);
        } catch (...) {
            target.chunk->RollbackLastRow({});
            --targetArchetype.entityCount_;
            throw;
        }

        for (const ArchetypeColumnMove& move : transition.moves) {
            void* destination = target.chunk->storage_ + move.targetOffset +
                                move.info->size * target.row;
            void* sourceValue = source.chunk->storage_ + move.sourceOffset +
                                move.info->size * source.row;
            move.info->moveConstruct(destination, sourceValue);
        }

        UpdateAfterRemoval(*sourceArchetype, source);
        record.archetype = std::addressof(targetArchetype);
        record.chunk     = target.chunk;
        record.row       = target.row;
        return *reinterpret_cast<Type*>(
            target.chunk->storage_ + transition.changedTargetOffset + sizeof(Type) * target.row);
    }

    template <typename Component>
    bool Remove(Entity entity) {
        using Type = std::remove_cvref_t<Component>;
        AssertStructureCanChange();
        EntityRecord& record = RequireRecord(entity);
        const ComponentTypeId removeId = components_.Find<Type>();
        if (removeId == INVALID_COMPONENT_TYPE || !record.archetype->Has(removeId)) {
            return false;
        }

        Archetype* sourceArchetype = record.archetype;
        ArchetypeTransition& transition = GetRemoveTransition(*sourceArchetype, removeId);
        Archetype& targetArchetype = *transition.target;
        const ChunkLocation target = targetArchetype.Allocate(entity);
        const ChunkLocation source{record.chunk, record.row};

        for (const ArchetypeColumnMove& move : transition.moves) {
            void* destination = target.chunk->storage_ + move.targetOffset +
                                move.info->size * target.row;
            void* sourceValue = source.chunk->storage_ + move.sourceOffset +
                                move.info->size * source.row;
            move.info->moveConstruct(destination, sourceValue);
        }

        UpdateAfterRemoval(*sourceArchetype, source);
        record.archetype = std::addressof(targetArchetype);
        record.chunk     = target.chunk;
        record.row       = target.row;
        return true;
    }

    template <typename Component>
    [[nodiscard]] bool Has(Entity entity) const {
        const EntityRecord* record = TryRecord(entity);
        const ComponentTypeId type = components_.Find<Component>();
        return record != nullptr && type != INVALID_COMPONENT_TYPE && record->archetype->Has(type);
    }

    template <typename Component>
    [[nodiscard]] Component* TryGet(Entity entity) {
        using Type = std::remove_cvref_t<Component>;
        EntityRecord* record       = TryRecord(entity);
        const ComponentTypeId type = components_.Find<Type>();
        if (record == nullptr || type == INVALID_COMPONENT_TYPE || !record->archetype->Has(type)) {
            return nullptr;
        }
        return static_cast<Type*>(record->chunk->ComponentAt(type, record->row));
    }

    template <typename Component>
    [[nodiscard]] const Component* TryGet(Entity entity) const {
        using Type = std::remove_cvref_t<Component>;
        const EntityRecord* record = TryRecord(entity);
        const ComponentTypeId type = components_.Find<Type>();
        if (record == nullptr || type == INVALID_COMPONENT_TYPE || !record->archetype->Has(type)) {
            return nullptr;
        }
        return static_cast<const Type*>(record->chunk->ComponentAt(type, record->row));
    }

    template <typename Component>
    [[nodiscard]] Component& Get(Entity entity) {
        if (Component* value = TryGet<Component>(entity); value != nullptr) {
            return *value;
        }
        throw std::out_of_range("The entity does not own the requested ECS component");
    }

    template <typename Component>
    [[nodiscard]] const Component& Get(Entity entity) const {
        if (const Component* value = TryGet<Component>(entity); value != nullptr) {
            return *value;
        }
        throw std::out_of_range("The entity does not own the requested ECS component");
    }

    template <typename Component, typename Value>
    Component& Set(Entity entity, Value&& value) {
        static_assert(std::is_assignable_v<Component&, Value&&>);
        Component& component = Get<Component>(entity);
        component            = std::forward<Value>(value);
        return component;
    }

    template <typename Component, typename... Arguments>
    Component& GetOrAdd(Entity entity, Arguments&&... arguments) {
        if (Component* value = TryGet<Component>(entity); value != nullptr) {
            return *value;
        }
        return Add<Component>(entity, std::forward<Arguments>(arguments)...);
    }

    template <typename... Components>
    [[nodiscard]] Query<Components...> MakeQuery();

    [[nodiscard]] ECS_API const Signature& GetSignature(Entity entity) const;
    [[nodiscard]] std::size_t EntityCount() const noexcept { return entityCount_; }
    [[nodiscard]] std::size_t ArchetypeCount() const noexcept { return archetypes_.size(); }
    [[nodiscard]] const ComponentRegistry& Components() const noexcept { return components_; }
    ECS_API void ReserveEntities(std::size_t capacity);
    ECS_API void Clear();

private:
    template <typename... Components>
    friend class Query;

    struct EntityRecord {
        u32 generation       = 1;
        u32 row              = 0;
        Archetype* archetype = nullptr;
        Chunk* chunk         = nullptr;
    };

    class IterationScope {
    public:
        explicit IterationScope(World& world) noexcept
            : world_(world) { ++world_.activeIterations_; }
        ~IterationScope() { --world_.activeIterations_; }
        IterationScope(const IterationScope&)            = delete;
        IterationScope& operator=(const IterationScope&) = delete;

    private:
        World& world_;
    };

    template <std::size_t ComponentCount, typename Component>
    void ConstructProvided(
        const ChunkLocation& location,
        std::array<ComponentTypeId, ComponentCount>& constructed,
        std::size_t& constructedCount,
        Component&& component) {
        using Type = std::remove_cvref_t<Component>;
        const ComponentTypeId type = components_.Find<Type>();
        void* destination = location.chunk->ComponentAt(type, location.row);
        ::new (destination) Type(std::forward<Component>(component));
        constructed[constructedCount++] = type;
    }

    [[nodiscard]] ECS_API Entity AllocateEntitySlot();
    ECS_API void RecycleUncommittedEntity(Entity entity) noexcept;
    [[nodiscard]] ECS_API Archetype& GetOrCreateArchetype(const Signature& signature);
    [[nodiscard]] ECS_API ArchetypeTransition& GetAddTransition(
        Archetype& source,
        ComponentTypeId componentType);
    [[nodiscard]] ECS_API ArchetypeTransition& GetRemoveTransition(
        Archetype& source,
        ComponentTypeId componentType);
    [[nodiscard]] ECS_API std::unique_ptr<ArchetypeTransition> BuildTransition(
        Archetype& source,
        Archetype& target,
        ComponentTypeId changedType);
    [[nodiscard]] ECS_API EntityRecord& RequireRecord(Entity entity);
    [[nodiscard]] ECS_API const EntityRecord& RequireRecord(Entity entity) const;
    [[nodiscard]] ECS_API EntityRecord* TryRecord(Entity entity) noexcept;
    [[nodiscard]] ECS_API const EntityRecord* TryRecord(Entity entity) const noexcept;
    ECS_API void AssertStructureCanChange() const;
    ECS_API void UpdateAfterRemoval(
        Archetype& archetype,
        const ChunkLocation& location) noexcept;

    ComponentRegistry components_;
    std::unordered_map<Signature, std::unique_ptr<Archetype>, SignatureHash> archetypes_;
    std::vector<Archetype*> creationArchetypes_;
    std::vector<EntityRecord> records_;
    std::vector<u32> freeEntityIndices_;
    std::size_t entityCount_ = 0;
    u64 archetypeVersion_    = 0;
    u32 activeIterations_    = 0;
};

} // namespace ecs

#include "Query.hpp"
