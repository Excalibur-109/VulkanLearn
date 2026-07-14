#pragma once

#include "Entity.hpp"

#include <deque>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace ecs {

using ComponentTypeId = u32;
inline constexpr ComponentTypeId INVALID_COMPONENT_TYPE =
    std::numeric_limits<ComponentTypeId>::max();

/**
 * @brief 一个组件类型的运行期操作表。
 *
 * Archetype 并不知道 Position、Mesh 或 Light 的 C++ 类型，只保存 ComponentInfo。
 * 函数指针让它仍然可以在 Chunk 迁移时移动构造和析构具体组件。
 */
struct ComponentInfo {
    using DefaultConstructFunction = void (*)(void* destination);
    using MoveConstructFunction    = void (*)(void* destination, void* source) noexcept;
    using DestroyFunction          = void (*)(void* value) noexcept;

    ComponentTypeId id = INVALID_COMPONENT_TYPE;
    std::type_index type{typeid(void)};
    std::string name;
    std::size_t size      = 0;
    std::size_t alignment = 1;
    DefaultConstructFunction defaultConstruct = nullptr;
    MoveConstructFunction moveConstruct       = nullptr;
    DestroyFunction destroy                   = nullptr;
};

/**
 * @brief World 私有的组件类型注册表。
 *
 * ComponentTypeId 只要求在同一个 World 内稳定。使用 deque 保存元数据，是因为 Chunk
 * 会长期保存 ComponentInfo 指针，而 deque 在尾部添加元素时不会移动已有元素。
 */
class ComponentRegistry {
public:
    template <typename Component>
    ComponentTypeId Register(std::string name = {}) {
        using Type = std::remove_cv_t<std::remove_reference_t<Component>>;

        static_assert(std::is_destructible_v<Type>, "ECS components must be destructible");
        static_assert(
            std::is_nothrow_move_constructible_v<Type>,
            "ECS components must be nothrow move constructible because archetype migration "
            "must not leave the source entity partially moved");

        const std::type_index type = typeid(Type);
        if (const auto iterator = typeToId_.find(type); iterator != typeToId_.end()) {
            return iterator->second;
        }

        if (infos_.size() >= INVALID_COMPONENT_TYPE) {
            throw std::overflow_error("The ECS component type id space is exhausted");
        }

        ComponentInfo info{};
        info.id        = static_cast<ComponentTypeId>(infos_.size());
        info.type      = type;
        info.name      = name.empty() ? type.name() : std::move(name);
        info.size      = sizeof(Type);
        info.alignment = alignof(Type);

        if constexpr (std::is_default_constructible_v<Type>) {
            info.defaultConstruct = [](void* destination) {
                ::new (destination) Type();
            };
        }

        info.moveConstruct = [](void* destination, void* source) noexcept {
            ::new (destination) Type(std::move(*static_cast<Type*>(source)));
        };
        info.destroy = [](void* value) noexcept {
            std::destroy_at(static_cast<Type*>(value));
        };

        const ComponentTypeId id = info.id;
        infos_.push_back(std::move(info));
        typeToId_.emplace(type, id);
        return id;
    }

    template <typename Component>
    [[nodiscard]] ComponentTypeId Ensure() {
        using Type = std::remove_cv_t<std::remove_reference_t<Component>>;
        if (const auto id = Find<Type>(); id != INVALID_COMPONENT_TYPE) {
            return id;
        }
        return Register<Type>();
    }

    template <typename Component>
    [[nodiscard]] ComponentTypeId Find() const noexcept {
        using Type = std::remove_cv_t<std::remove_reference_t<Component>>;
        const auto iterator = typeToId_.find(std::type_index(typeid(Type)));
        return iterator == typeToId_.end() ? INVALID_COMPONENT_TYPE : iterator->second;
    }

    [[nodiscard]] const ComponentInfo& Get(ComponentTypeId id) const {
        if (id >= infos_.size()) {
            throw std::out_of_range("The ECS component type id is not registered");
        }
        return infos_[id];
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return infos_.size();
    }

private:
    std::deque<ComponentInfo> infos_;
    std::unordered_map<std::type_index, ComponentTypeId> typeToId_;
};

} // namespace ecs
