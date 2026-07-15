#pragma once

#include <cstdint>
#include <functional>
#include <limits>

namespace ecs {

using u32 = std::uint32_t;
using u64 = std::uint64_t;

inline constexpr u32 INVALID_ENTITY_INDEX = std::numeric_limits<u32>::max();

/**
 * Entity 只是句柄，不保存组件。index 定位 World 槽位，generation 防止槽位复用后
 * 旧句柄错误访问新实体。
 */
struct Entity {
    u32 index      = INVALID_ENTITY_INDEX;
    u32 generation = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return index != INVALID_ENTITY_INDEX && generation != 0;
    }

    [[nodiscard]] explicit constexpr operator bool() const noexcept {
        return IsValid();
    }

    friend constexpr bool operator==(Entity lhs, Entity rhs) noexcept = default;
};

inline constexpr Entity NULL_ENTITY{};

struct EntityHash {
    [[nodiscard]] std::size_t operator()(Entity entity) const noexcept {
        return std::hash<u64>{}((static_cast<u64>(entity.generation) << 32U) | entity.index);
    }
};

} // namespace ecs

