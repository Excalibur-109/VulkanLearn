#pragma once

#include "Component.hpp"

#include <cstddef>
#include <initializer_list>
#include <vector>

namespace ecs {

/// 动态 bitset：第 N 位表示 Archetype 是否包含 ComponentTypeId N。
class Signature {
public:
    Signature() = default;
    ECS_API Signature(std::initializer_list<ComponentTypeId> componentTypes);

    ECS_API void Set(ComponentTypeId componentType);
    ECS_API void Reset(ComponentTypeId componentType);

    [[nodiscard]] ECS_API bool Test(ComponentTypeId componentType) const noexcept;
    [[nodiscard]] ECS_API bool ContainsAll(const Signature& required) const noexcept;
    [[nodiscard]] ECS_API bool Intersects(const Signature& other) const noexcept;
    [[nodiscard]] ECS_API bool Empty() const noexcept;
    [[nodiscard]] ECS_API std::size_t Count() const noexcept;
    [[nodiscard]] ECS_API std::vector<ComponentTypeId> ComponentTypes() const;

    [[nodiscard]] const std::vector<u64>& Words() const noexcept {
        return words_;
    }

    friend bool operator==(const Signature& lhs, const Signature& rhs) noexcept = default;

private:
    static constexpr std::size_t BITS_PER_WORD = sizeof(u64) * 8U;
    void RemoveTrailingZeroWords() noexcept;

    std::vector<u64> words_;
};

struct SignatureHash {
    [[nodiscard]] ECS_API std::size_t operator()(const Signature& signature) const noexcept;
};

} // namespace ecs

