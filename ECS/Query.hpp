#pragma once

#include "Filter.hpp"
#include "JobSystem.hpp"
#include "World.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <span>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs {
namespace detail {

template <typename Component>
using QueryRaw = std::remove_cvref_t<Component>;

template <typename Component>
using QueryReference = std::conditional_t<
    std::is_const_v<std::remove_reference_t<Component>>,
    const QueryRaw<Component>&,
    QueryRaw<Component>&>;

template <typename Component>
using QuerySpan = std::span<std::conditional_t<
    std::is_const_v<std::remove_reference_t<Component>>,
    const QueryRaw<Component>,
    QueryRaw<Component>>>;

} // namespace detail

/**
 * Query 按 Filter 缓存匹配 Archetype，不逐实体判断 Signature。
 * const 组件参数表示只读，例如 Query<const Transform, Velocity>。
 */
template <typename... Components>
class Query {
public:
    explicit Query(World& world)
        : world_(std::addressof(world)),
          componentTypes_{world.components_.Ensure<detail::QueryRaw<Components>>()...} {
        static_assert(detail::AreUnique<detail::QueryRaw<Components>...>::value);
        for (const ComponentTypeId type : componentTypes_) {
            filter_.Require(type);
        }
    }

    template <typename... Required>
    Query& With() {
        (filter_.Require(world_->components_.Ensure<detail::QueryRaw<Required>>()), ...);
        cacheDirty_ = true;
        return *this;
    }

    template <typename... Optional>
    Query& WithAny() {
        (filter_.RequireAny(world_->components_.Ensure<detail::QueryRaw<Optional>>()), ...);
        cacheDirty_ = true;
        return *this;
    }

    template <typename... Excluded>
    Query& Without() {
        (filter_.Exclude(world_->components_.Ensure<detail::QueryRaw<Excluded>>()), ...);
        cacheDirty_ = true;
        return *this;
    }

    [[nodiscard]] const Filter& GetFilter() const noexcept { return filter_; }

    [[nodiscard]] std::size_t Count() {
        Refresh();
        std::size_t result = 0;
        for (const MatchedArchetype& matched : archetypes_) {
            result += matched.archetype->EntityCount();
        }
        return result;
    }

    [[nodiscard]] bool Empty() { return Count() == 0; }

    template <typename Function>
    void Each(Function&& function) {
        Refresh();
        typename World::IterationScope scope(*world_);
        for (const MatchedArchetype& matched : archetypes_) {
            for (const std::unique_ptr<Chunk>& owner : matched.archetype->chunks_) {
                Chunk& chunk = *owner;
                const Entity* entities = chunk.EntityData();
                const auto columns = MakeColumnPointers(
                    chunk,
                    matched.offsets,
                    std::index_sequence_for<Components...>{});
                for (u32 row = 0; row < chunk.Count(); ++row) {
                    InvokeRow(
                        function,
                        entities[row],
                        columns,
                        row,
                        std::index_sequence_for<Components...>{});
                }
            }
        }
    }

    /// 回调签名：(span<const Entity>, span<C0>, span<C1>...)。
    template <typename Function>
    void EachChunk(Function&& function) {
        Refresh();
        typename World::IterationScope scope(*world_);
        for (const MatchedArchetype& matched : archetypes_) {
            for (const std::unique_ptr<Chunk>& owner : matched.archetype->chunks_) {
                Chunk& chunk = *owner;
                if (chunk.Count() != 0) {
                    const auto columns = MakeColumnPointers(
                        chunk,
                        matched.offsets,
                        std::index_sequence_for<Components...>{});
                    InvokeChunk(
                        function,
                        chunk,
                        columns,
                        std::index_sequence_for<Components...>{});
                }
            }
        }
    }

    /**
     * 由固定线程池并行处理 Chunk。回调会被多个线程同时调用，捕获的外部可写状态需要
     * 调用者同步；每个任务拿到的组件 span 互不重叠。
     */
    template <typename Function>
    void ParallelEachChunk(JobSystem& jobs, Function&& function) {
        Refresh();
        typename World::IterationScope scope(*world_);
        for (const MatchedArchetype& matched : archetypes_) {
            const auto& chunks = matched.archetype->chunks_;
            const std::size_t targetBatches = (jobs.WorkerCount() + 1U) * 4U;
            const std::size_t grainSize =
                std::max<std::size_t>(1U, chunks.size() / targetBatches);
            jobs.ParallelFor(chunks.size(), grainSize, [&](std::size_t chunkIndex) {
                Chunk& chunk = *chunks[chunkIndex];
                if (chunk.Count() == 0) {
                    return;
                }
                const auto columns = MakeColumnPointers(
                    chunk,
                    matched.offsets,
                    std::index_sequence_for<Components...>{});
                InvokeChunk(
                    function,
                    chunk,
                    columns,
                    std::index_sequence_for<Components...>{});
            });
        }
    }

private:
    struct MatchedArchetype {
        Archetype* archetype = nullptr;
        std::array<std::size_t, sizeof...(Components)> offsets{};
    };

    void Refresh() {
        if (!cacheDirty_ && cachedVersion_ == world_->archetypeVersion_) {
            return;
        }
        archetypes_.clear();
        for (const auto& [signature, archetype] : world_->archetypes_) {
            if (filter_.Matches(signature)) {
                MatchedArchetype matched{};
                matched.archetype = archetype.get();
                BindOffsets(
                    matched,
                    archetype->layout_,
                    std::index_sequence_for<Components...>{});
                archetypes_.push_back(matched);
            }
        }
        cachedVersion_ = world_->archetypeVersion_;
        cacheDirty_    = false;
    }

    template <std::size_t... Indices>
    void BindOffsets(
        MatchedArchetype& matched,
        const ChunkLayout& layout,
        std::index_sequence<Indices...>) {
        ((matched.offsets[Indices] = layout.FindColumn(componentTypes_[Indices])->offset), ...);
    }

    template <typename Component>
    using ColumnPointer = std::conditional_t<
        std::is_const_v<std::remove_reference_t<Component>>,
        const detail::QueryRaw<Component>*,
        detail::QueryRaw<Component>*>;

    template <std::size_t... Indices>
    static auto MakeColumnPointers(
        Chunk& chunk,
        const std::array<std::size_t, sizeof...(Components)>& offsets,
        std::index_sequence<Indices...>) {
        return std::tuple<ColumnPointer<Components>...>{
            reinterpret_cast<ColumnPointer<Components>>(chunk.storage_ + offsets[Indices])...};
    }

    template <typename Function, typename Columns, std::size_t... Indices>
    void InvokeRow(
        Function& function,
        Entity entity,
        const Columns& columns,
        u32 row,
        std::index_sequence<Indices...>) {
        if constexpr (std::is_invocable_v<Function&, Entity, detail::QueryReference<Components>...>) {
            std::invoke(
                function,
                entity,
                std::get<Indices>(columns)[row]...);
        } else {
            static_assert(std::is_invocable_v<Function&, detail::QueryReference<Components>...>);
            std::invoke(function, std::get<Indices>(columns)[row]...);
        }
    }

    template <typename Function, typename Columns, std::size_t... Indices>
    void InvokeChunk(
        Function& function,
        Chunk& chunk,
        const Columns& columns,
        std::index_sequence<Indices...>) {
        static_assert(std::is_invocable_v<
                      Function&,
                      std::span<const Entity>,
                      detail::QuerySpan<Components>...>);
        std::invoke(
            function,
            std::as_const(chunk).Entities(),
            detail::QuerySpan<Components>{std::get<Indices>(columns), chunk.Count()}...);
    }

    World* world_ = nullptr;
    std::array<ComponentTypeId, sizeof...(Components)> componentTypes_{};
    Filter filter_;
    std::vector<MatchedArchetype> archetypes_;
    u64 cachedVersion_ = 0;
    bool cacheDirty_    = true;
};

// Types 避免 MSVC 与 World::Components() 成员函数发生名称查找歧义。
template <typename... Types>
ecs::Query<Types...> World::MakeQuery() {
    return ecs::Query<Types...>{*this};
}

} // namespace ecs
