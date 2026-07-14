#pragma once

#include <cstdint>
#include <functional>
#include <limits>

namespace ecs {

using u8  = std::uint8_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

inline constexpr u32 INVALID_ENTITY_INDEX = std::numeric_limits<u32>::max();

/**
 * @brief 面向使用者的实体句柄。
 *
 * index 用来定位 World 中的实体记录，generation 用来识别该槽位是否已被回收再利用。
 * 例如 Entity{7, 2} 被销毁后，第 7 个槽位可能变成 Entity{7, 3}；旧句柄因此不会
 * 意外访问新实体。Entity 本身不保存组件，也不拥有任何内存。
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

