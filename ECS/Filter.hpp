#pragma once

#include "Signature.hpp"

namespace ecs {

/// all 全部需要、any 至少一个、none 一个也不能有。
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

    [[nodiscard]] bool Matches(const Signature& signature) const noexcept {
        return signature.ContainsAll(all_) &&
               (any_.Empty() || signature.Intersects(any_)) &&
               !signature.Intersects(none_);
    }

    [[nodiscard]] const Signature& All() const noexcept { return all_; }
    [[nodiscard]] const Signature& Any() const noexcept { return any_; }
    [[nodiscard]] const Signature& None() const noexcept { return none_; }

private:
    Signature all_;
    Signature any_;
    Signature none_;
};

} // namespace ecs

