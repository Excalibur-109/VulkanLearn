#pragma once

#include "Archetype.hpp"

#include <array>
#include <cassert>
#include <new>
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

template <typename... Types>
struct AreUnique : std::true_type {};

template <typename First, typename... Rest>
struct AreUnique<First, Rest...>
    : std::bool_constant<
          (!std::is_same_v<std::remove_cvref_t<First>, std::remove_cvref_t<Rest>> && ...) &&
          AreUnique<Rest...>::value> {};

} // namespace detail

/**
 * @brief ECS 的所有权与一致性边界。
 *
 * World 拥有组件注册表、全部 Archetype 和实体槽位。Entity 只是一把带 generation 的
 * “钥匙”；真正的数据地址必须通过 World 查询。任何会改变组件组合的操作都会让实体
 * 迁移，因此查询回调内禁止直接执行结构变化，应使用 CommandBuffer 延迟到遍历结束。
 */
class World {
public:
    World();
    ~World() = default;

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

        std::array<ComponentTypeId, sizeof...(Components)> componentTypes{
            components_.Ensure<std::remove_cvref_t<Components>>()...};
        Signature signature;
        for (const ComponentTypeId componentType : componentTypes) {
            signature.Set(componentType);
        }

        Archetype& archetype = GetOrCreateArchetype(signature);
        const Entity entity  = AllocateEntitySlot();
        const ChunkLocation location = archetype.Allocate(entity);

        std::vector<ComponentTypeId> constructedTypes;
        constructedTypes.reserve(sizeof...(Components));

        try {
            (ConstructProvidedComponent(
                 location,
                 constructedTypes,
                 std::forward<Components>(components)),
             ...);
        } catch (...) {
            location.chunk->RollbackLastRow(constructedTypes);
            --archetype.entityCount_;
            RecycleUncommittedEntity(entity);
            throw;
        }

        EntityRecord& record = records_[entity.index];
        record.alive         = true;
        record.archetype     = std::addressof(archetype);
        record.chunk         = location.chunk;
        record.row           = location.row;
        ++entityCount_;
        return entity;
    }

    [[nodiscard]] bool DestroyEntity(Entity entity);
    [[nodiscard]] bool IsAlive(Entity entity) const noexcept;

    template <typename Component, typename... Arguments>
    Component& Add(Entity entity, Arguments&&... arguments) {
        using Type = std::remove_cv_t<Component>;
        static_assert(!std::is_const_v<Component>, "A stored component type cannot be const");
        static_assert(
            std::is_constructible_v<Type, Arguments...>,
            "The component cannot be constructed from the supplied arguments");

        AssertStructureCanChange();
        EntityRecord& record          = RequireRecord(entity);
        const ComponentTypeId addedId = components_.Ensure<Type>();
        if (record.archetype->Has(addedId)) {
            throw std::logic_error("The entity already owns this ECS component");
        }

        Signature targetSignature = record.archetype->GetSignature();
        targetSignature.Set(addedId);
        Archetype& targetArchetype    = GetOrCreateArchetype(targetSignature);
        const ChunkLocation target    = targetArchetype.Allocate(entity);
        const ChunkLocation source{record.chunk, record.row};
        Archetype* sourceArchetype    = record.archetype;
        std::vector<ComponentTypeId> constructedTypes;
        constructedTypes.reserve(targetSignature.Count());

        try {
            // 先构造唯一可能抛异常的新组件；成功后，旧列只执行 noexcept 移动。
            void* addedDestination = target.chunk->ComponentAt(addedId, target.row);
            ::new (addedDestination) Type(std::forward<Arguments>(arguments)...);
            constructedTypes.push_back(addedId);

            for (const ComponentInfo* info : sourceArchetype->componentInfos_) {
                void* destination = target.chunk->ComponentAt(info->id, target.row);
                void* sourceValue = source.chunk->ComponentAt(info->id, source.row);
                info->moveConstruct(destination, sourceValue);
                constructedTypes.push_back(info->id);
            }
        } catch (...) {
            target.chunk->RollbackLastRow(constructedTypes);
            --targetArchetype.entityCount_;
            throw;
        }

        UpdateRecordAfterSourceRemoval(*sourceArchetype, source);
        record.archetype = std::addressof(targetArchetype);
        record.chunk     = target.chunk;
        record.row       = target.row;
        return *static_cast<Type*>(target.chunk->ComponentAt(addedId, target.row));
    }

    template <typename Component>
    bool Remove(Entity entity) {
        using Type = std::remove_cv_t<Component>;
        AssertStructureCanChange();
        EntityRecord& record = RequireRecord(entity);
        const ComponentTypeId removedId = components_.Find<Type>();
        if (removedId == INVALID_COMPONENT_TYPE || !record.archetype->Has(removedId)) {
            return false;
        }

        Signature targetSignature = record.archetype->GetSignature();
        targetSignature.Reset(removedId);
        Archetype& targetArchetype = GetOrCreateArchetype(targetSignature);
        const ChunkLocation target = targetArchetype.Allocate(entity);
        const ChunkLocation source{record.chunk, record.row};
        Archetype* sourceArchetype = record.archetype;

        // 所有已注册组件均保证 noexcept move，因此这个迁移阶段不会半途失败。
        for (const ComponentInfo* info : targetArchetype.componentInfos_) {
            void* destination = target.chunk->ComponentAt(info->id, target.row);
            void* sourceValue = source.chunk->ComponentAt(info->id, source.row);
            info->moveConstruct(destination, sourceValue);
        }

        UpdateRecordAfterSourceRemoval(*sourceArchetype, source);
        record.archetype = std::addressof(targetArchetype);
        record.chunk     = target.chunk;
        record.row       = target.row;
        return true;
    }

    template <typename Component>
    [[nodiscard]] bool Has(Entity entity) const noexcept {
        const EntityRecord* record = TryRecord(entity);
        if (record == nullptr) {
            return false;
        }
        const ComponentTypeId componentType = components_.Find<Component>();
        return componentType != INVALID_COMPONENT_TYPE && record->archetype->Has(componentType);
    }

    template <typename Component>
    [[nodiscard]] Component* TryGet(Entity entity) noexcept {
        using Type = std::remove_cv_t<Component>;
        EntityRecord* record = TryRecord(entity);
        const ComponentTypeId componentType = components_.Find<Type>();
        if (record == nullptr || componentType == INVALID_COMPONENT_TYPE ||
            !record->archetype->Has(componentType)) {
            return nullptr;
        }
        return static_cast<Type*>(record->chunk->ComponentAt(componentType, record->row));
    }

    template <typename Component>
    [[nodiscard]] const Component* TryGet(Entity entity) const noexcept {
        using Type = std::remove_cv_t<Component>;
        const EntityRecord* record = TryRecord(entity);
        const ComponentTypeId componentType = components_.Find<Type>();
        if (record == nullptr || componentType == INVALID_COMPONENT_TYPE ||
            !record->archetype->Has(componentType)) {
            return nullptr;
        }
        return static_cast<const Type*>(record->chunk->ComponentAt(componentType, record->row));
    }

    template <typename Component>
    [[nodiscard]] Component& Get(Entity entity) {
        if (Component* component = TryGet<Component>(entity); component != nullptr) {
            return *component;
        }
        throw std::out_of_range("The entity does not own the requested ECS component");
    }

    template <typename Component>
    [[nodiscard]] const Component& Get(Entity entity) const {
        if (const Component* component = TryGet<Component>(entity); component != nullptr) {
            return *component;
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

    template <typename... Components>
    [[nodiscard]] Query<Components...> MakeQuery();

    [[nodiscard]] const Signature& GetSignature(Entity entity) const;

    [[nodiscard]] std::size_t EntityCount() const noexcept {
        return entityCount_;
    }

    [[nodiscard]] std::size_t ArchetypeCount() const noexcept {
        return archetypes_.size();
    }

    [[nodiscard]] const ComponentRegistry& Components() const noexcept {
        return components_;
    }

    void Clear();

private:
    template <typename... Components>
    friend class Query;

    struct EntityRecord {
        u32 generation          = 1;
        bool alive              = false;
        Archetype* archetype    = nullptr;
        Chunk* chunk            = nullptr;
        u32 row                 = 0;
    };

    class IterationScope {
    public:
        explicit IterationScope(World& world) noexcept
            : world_(world) {
            ++world_.activeIterations_;
        }

        ~IterationScope() {
            assert(world_.activeIterations_ > 0);
            --world_.activeIterations_;
        }

        IterationScope(const IterationScope&)            = delete;
        IterationScope& operator=(const IterationScope&) = delete;

    private:
        World& world_;
    };

    template <typename Component>
    void ConstructProvidedComponent(
        const ChunkLocation& location,
        std::vector<ComponentTypeId>& constructedTypes,
        Component&& component) {
        using Type = std::remove_cvref_t<Component>;
        const ComponentTypeId componentType = components_.Find<Type>();
        assert(componentType != INVALID_COMPONENT_TYPE);
        void* destination = location.chunk->ComponentAt(componentType, location.row);
        ::new (destination) Type(std::forward<Component>(component));
        constructedTypes.push_back(componentType);
    }

    [[nodiscard]] Entity AllocateEntitySlot();
    void RecycleUncommittedEntity(Entity entity) noexcept;
    [[nodiscard]] Archetype& GetOrCreateArchetype(const Signature& signature);
    [[nodiscard]] EntityRecord& RequireRecord(Entity entity);
    [[nodiscard]] const EntityRecord& RequireRecord(Entity entity) const;
    [[nodiscard]] EntityRecord* TryRecord(Entity entity) noexcept;
    [[nodiscard]] const EntityRecord* TryRecord(Entity entity) const noexcept;
    void AssertStructureCanChange() const;
    void UpdateRecordAfterSourceRemoval(Archetype& archetype, const ChunkLocation& location) noexcept;

    ComponentRegistry components_;
    std::unordered_map<Signature, std::unique_ptr<Archetype>, SignatureHash> archetypes_;
    std::vector<EntityRecord> records_;
    std::vector<u32> freeEntityIndices_;
    std::size_t entityCount_       = 0;
    u64 archetypeVersion_          = 0;
    u32 activeIterations_          = 0;
};

} // namespace ecs

#include "Query.hpp"

