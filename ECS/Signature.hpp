#pragma once

#include "Component.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <initializer_list>
#include <vector>

namespace ecs {

/**
 * @brief 描述 Archetype 包含哪些组件类型的动态位集合。
 *
 * 第 N 位对应 ComponentTypeId N。使用动态长度而不是固定 64/128 位，可以让组件类型
 * 数量不受编译期上限限制。末尾全 0 的 word 会被移除，因此逻辑相同的 Signature
 * 具有完全相同的哈希输入。
 */
class Signature {
public:
    Signature() = default;
    Signature(std::initializer_list<ComponentTypeId> componentTypes);

    void Set(ComponentTypeId componentType);
    void Reset(ComponentTypeId componentType);

    [[nodiscard]] bool Test(ComponentTypeId componentType) const noexcept;
    [[nodiscard]] bool ContainsAll(const Signature& required) const noexcept;
    [[nodiscard]] bool Intersects(const Signature& other) const noexcept;
    [[nodiscard]] bool Empty() const noexcept;
    [[nodiscard]] std::size_t Count() const noexcept;
    [[nodiscard]] std::vector<ComponentTypeId> ComponentTypes() const;

    friend bool operator==(const Signature& lhs, const Signature& rhs) noexcept = default;

    [[nodiscard]] const std::vector<u64>& Words() const noexcept {
        return words_;
    }

private:
    static constexpr std::size_t BITS_PER_WORD = sizeof(u64) * 8U;

    void RemoveTrailingZeroWords() noexcept;

    std::vector<u64> words_;
};

struct SignatureHash {
    [[nodiscard]] std::size_t operator()(const Signature& signature) const noexcept;
};

} // namespace ecs

