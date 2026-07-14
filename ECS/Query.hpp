#pragma once

#include "Filter.hpp"
#include "World.hpp"

#include <array>
#include <functional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs {
namespace detail {

template <typename Component>
using QueryRawComponent = std::remove_cv_t<std::remove_reference_t<Component>>;

template <typename Component>
using QueryComponentReference = std::conditional_t<
    std::is_const_v<std::remove_reference_t<Component>>,
    const QueryRawComponent<Component>&,
    QueryRawComponent<Component>&>;

template <typename Component>
using QueryComponentSpan = std::span<std::conditional_t<
    std::is_const_v<std::remove_reference_t<Component>>,
    const QueryRawComponent<Component>,
    QueryRawComponent<Component>>>;

} // namespace detail

/**
 * @brief 可复用的类型安全查询。
 *
 * Query 首先按 Filter 匹配 Archetype，而不是逐实体检查组件；匹配结果只在出现新
 * Archetype 或修改 Filter 后重建。真正遍历时直接读取 Chunk 的 SoA 列。
 *
 * Query<const Transform, Velocity> 表示 Transform 只读、Velocity 可写。const 在这里是
 * 访问约束，不会注册成另一种组件类型。
 */
template <typename... Components>
class Query {
public:
    explicit Query(World& world)
        : world_(std::addressof(world)),
          componentTypes_{world.components_.Ensure<detail::QueryRawComponent<Components>>()...} {
        static_assert(
            detail::AreUnique<detail::QueryRawComponent<Components>...>::value,
            "A query cannot request the same component type more than once");
        for (const ComponentTypeId componentType : componentTypes_) {
            filter_.Require(componentType);
        }
    }

    template <typename... Required>
    Query& With() {
        (filter_.Require(world_->components_.Ensure<detail::QueryRawComponent<Required>>()), ...);
        cacheDirty_ = true;
        return *this;
    }

    template <typename... Optional>
    Query& WithAny() {
        (filter_.RequireAny(world_->components_.Ensure<detail::QueryRawComponent<Optional>>()), ...);
        cacheDirty_ = true;
        return *this;
    }

    template <typename... Excluded>
    Query& Without() {
        (filter_.Exclude(world_->components_.Ensure<detail::QueryRawComponent<Excluded>>()), ...);
        cacheDirty_ = true;
        return *this;
    }

    [[nodiscard]] const Filter& GetFilter() const noexcept {
        return filter_;
    }

    [[nodiscard]] std::size_t Count() {
        RefreshArchetypes();
        std::size_t result = 0;
        for (const Archetype* archetype : matchingArchetypes_) {
            result += archetype->EntityCount();
        }
        return result;
    }

    [[nodiscard]] bool Empty() {
        return Count() == 0;
    }

    template <typename Function>
    void Each(Function&& function) {
        RefreshArchetypes();
        typename World::IterationScope iterationScope(*world_);

        for (Archetype* archetype : matchingArchetypes_) {
            for (const std::unique_ptr<Chunk>& chunkOwner : archetype->chunks_) {
                Chunk& chunk = *chunkOwner;
                for (u32 row = 0; row < chunk.Count(); ++row) {
                    InvokeRow(function, chunk, row, std::index_sequence_for<Components...>{});
                }
            }
        }
    }

    /**
     * @brief 以整列 span 交给系统，适合批处理、SIMD 或作业系统拆分。
     *
     * 回调签名为 `(span<const Entity>, span<C0>, span<C1>...)`。每个 span 的长度相同，
     * 第 i 个位置始终属于同一个实体，但不同组件列在内存中彼此分开。
     */
    template <typename Function>
    void EachChunk(Function&& function) {
        RefreshArchetypes();
        typename World::IterationScope iterationScope(*world_);

        for (Archetype* archetype : matchingArchetypes_) {
            for (const std::unique_ptr<Chunk>& chunkOwner : archetype->chunks_) {
                Chunk& chunk = *chunkOwner;
                InvokeChunk(function, chunk, std::index_sequence_for<Components...>{});
            }
        }
    }

private:
    void RefreshArchetypes() {
        if (!cacheDirty_ && cachedArchetypeVersion_ == world_->archetypeVersion_) {
            return;
        }

        matchingArchetypes_.clear();
        for (const auto& [signature, archetype] : world_->archetypes_) {
            if (filter_.Matches(signature)) {
                matchingArchetypes_.push_back(archetype.get());
            }
        }

        cachedArchetypeVersion_ = world_->archetypeVersion_;
        cacheDirty_             = false;
    }

    template <typename Function, std::size_t... Indices>
    void InvokeRow(Function& function, Chunk& chunk, u32 row, std::index_sequence<Indices...>) {
        const Entity entity = chunk.EntityAt(row);

        if constexpr (std::is_invocable_v<
                          Function&,
                          Entity,
                          detail::QueryComponentReference<Components>...>) {
            std::invoke(
                function,
                entity,
                ComponentAt<Components>(chunk, row, componentTypes_[Indices])...);
        } else if constexpr (
            std::is_invocable_v<Function&, detail::QueryComponentReference<Components>...>) {
            std::invoke(
                function,
                ComponentAt<Components>(chunk, row, componentTypes_[Indices])...);
        } else {
            static_assert(
                std::is_invocable_v<
                    Function&,
                    Entity,
                    detail::QueryComponentReference<Components>...> ||
                    std::is_invocable_v<Function&, detail::QueryComponentReference<Components>...>,
                "Query::Each callback must accept (Entity, Components&...) or (Components&...)");
        }
    }

    template <typename Component>
    [[nodiscard]] static detail::QueryComponentReference<Component> ComponentAt(
        Chunk& chunk,
        u32 row,
        ComponentTypeId componentType) {
        using Type = detail::QueryRawComponent<Component>;
        return *static_cast<Type*>(chunk.ComponentAt(componentType, row));
    }

    template <typename Function, std::size_t... Indices>
    void InvokeChunk(Function& function, Chunk& chunk, std::index_sequence<Indices...>) {
        static_assert(
            std::is_invocable_v<
                Function&,
                std::span<const Entity>,
                detail::QueryComponentSpan<Components>...>,
            "Query::EachChunk callback must accept spans for Entity and every component");

        std::invoke(
            function,
            std::as_const(chunk).Entities(),
            MakeComponentSpan<Components>(chunk, componentTypes_[Indices])...);
    }

    template <typename Component>
    [[nodiscard]] static detail::QueryComponentSpan<Component> MakeComponentSpan(
        Chunk& chunk,
        ComponentTypeId componentType) {
        using Type = detail::QueryRawComponent<Component>;
        using Element = std::conditional_t<
            std::is_const_v<std::remove_reference_t<Component>>,
            const Type,
            Type>;
        return {static_cast<Element*>(chunk.ComponentColumnData(componentType)), chunk.Count()};
    }

    World* world_ = nullptr;
    std::array<ComponentTypeId, sizeof...(Components)> componentTypes_{};
    Filter filter_;
    std::vector<Archetype*> matchingArchetypes_;
    u64 cachedArchetypeVersion_ = 0;
    bool cacheDirty_             = true;
};

// 这里使用 Types 而不是 Components，避免 MSVC 在类外成员定义中与 World::Components()
// 成员函数发生名称查找歧义。
template <typename... Types>
ecs::Query<Types...> World::MakeQuery() {
    return ecs::Query<Types...>{*this};
}

} // namespace ecs
