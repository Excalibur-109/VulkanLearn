#pragma once

#include "Signature.hpp"

namespace ecs {

/**
 * @brief Query 对 Archetype 的匹配条件。
 *
 * - all：Archetype 必须包含其中每一种组件。
 * - any：any 非空时，Archetype 至少包含其中一种组件。
 * - none：Archetype 不能包含其中任何组件。
 */
class Filter {
public:
    Filter& Require(ComponentTypeId componentType) {
        all_.Set(componentType);
        return *this;
    }

    Filter& RequireAny(ComponentTypeId componentType) {
        any_.Set(componentType);
        return *this;
    }

    Filter& Exclude(ComponentTypeId componentType) {
        none_.Set(componentType);
        return *this;
    }

    [[nodiscard]] bool Matches(const Signature& archetypeSignature) const noexcept {
        return archetypeSignature.ContainsAll(all_) &&
               (any_.Empty() || archetypeSignature.Intersects(any_)) &&
               !archetypeSignature.Intersects(none_);
    }

    [[nodiscard]] const Signature& All() const noexcept {
        return all_;
    }

    [[nodiscard]] const Signature& Any() const noexcept {
        return any_;
    }

    [[nodiscard]] const Signature& None() const noexcept {
        return none_;
    }

private:
    Signature all_;
    Signature any_;
    Signature none_;
};

} // namespace ecs

