#pragma once

#include "ECSApi.hpp"
#include "Entity.hpp"

#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs {

using ComponentTypeId = u32;
inline constexpr ComponentTypeId INVALID_COMPONENT_TYPE =
    std::numeric_limits<ComponentTypeId>::max();

/**
 * 跨 DLL、序列化和插件边界使用的稳定组件身份。运行期 ComponentTypeId 只用于数组索引。
 */
struct ComponentGuid {
    u64 high = 0;
    u64 low  = 0;

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return high != 0 || low != 0;
    }

    friend constexpr bool operator==(ComponentGuid lhs, ComponentGuid rhs) noexcept = default;
};

/// 唯一实现在 ECS DLL 中；所有模块都通过它获取进程内一致的紧凑 ID。
ECS_API ComponentTypeId AcquireComponentTypeId(ComponentGuid guid, const char* debugName);
ECS_API std::size_t RegisteredComponentTypeCount() noexcept;

namespace detail {

consteval u64 HashTypeSignature(std::string_view signature, u64 seed) noexcept {
    u64 result = seed;
    for (const char character : signature) {
        result ^= static_cast<unsigned char>(character);
        result *= 1099511628211ULL;
    }
    return result;
}

template <typename Component>
consteval std::string_view ComponentTypeSignature() noexcept {
#if defined(_MSC_VER)
    return __FUNCSIG__;
#else
    return __PRETTY_FUNCTION__;
#endif
}

template <typename Component>
consteval ComponentGuid ResolveComponentGuid() noexcept {
    using Type = std::remove_cvref_t<Component>;
    if constexpr (requires { Type::ECS_COMPONENT_GUID_VALUE; }) {
        return Type::ECS_COMPONENT_GUID_VALUE;
    } else {
        constexpr std::string_view signature = ComponentTypeSignature<Type>();
        return ComponentGuid{
            HashTypeSignature(signature, 14695981039346656037ULL),
            HashTypeSignature(signature, 1099511628211ULL)};
    }
}

template <typename Component>
[[nodiscard]] ComponentTypeId StaticComponentTypeId() {
    using Type = std::remove_cvref_t<Component>;
    constexpr ComponentGuid guid = ResolveComponentGuid<Type>();
    static_assert(guid.IsValid(), "ECS component GUID cannot be zero");
    static const ComponentTypeId id = AcquireComponentTypeId(guid, typeid(Type).name());
    return id;
}

} // namespace detail

template <typename Component>
[[nodiscard]] ComponentTypeId GetComponentTypeId() {
    return detail::StaticComponentTypeId<std::remove_cvref_t<Component>>();
}

template <typename Component>
[[nodiscard]] consteval ComponentGuid GetComponentGuid() noexcept {
    return detail::ResolveComponentGuid<std::remove_cvref_t<Component>>();
}

/**
 * Archetype 不知道具体 C++ 类型，通过这张函数指针操作表移动和析构组件。
 * 这是静态多态注册后的类型擦除，不需要 Component 基类和 virtual。
 */
struct ComponentInfo {
    using MoveConstructFunction = void (*)(void* destination, void* source) noexcept;
    using DestroyFunction       = void (*)(void* value) noexcept;

    ComponentTypeId id = INVALID_COMPONENT_TYPE;
    std::type_index type{typeid(void)};
    std::string name;
    std::size_t size                       = 0;
    std::size_t alignment                  = 1;
    MoveConstructFunction moveConstruct    = nullptr;
    DestroyFunction destroy                = nullptr;
};

/**
 * 组件类型 ID 在进程内按 C++ 类型静态分配。每个 World 的 Registry 按 ID 直接索引
 * unique_ptr；vector 扩容只移动 unique_ptr，ComponentInfo 原始地址保持稳定。
 */
class ComponentRegistry {
public:
    template <typename Component>
    ComponentTypeId Register(std::string name = {}) {
        using Type = std::remove_cvref_t<Component>;
        static_assert(std::is_destructible_v<Type>, "ECS components must be destructible");
        static_assert(
            std::is_nothrow_move_constructible_v<Type>,
            "ECS components must be nothrow move constructible for safe archetype migration");

        const ComponentTypeId id = detail::StaticComponentTypeId<Type>();
        if (id < infos_.size() && infos_[id] != nullptr) {
            return id;
        }

        auto info       = std::make_unique<ComponentInfo>();
        info->id        = id;
        info->type      = std::type_index(typeid(Type));
        info->name      = name.empty() ? typeid(Type).name() : std::move(name);
        info->size      = sizeof(Type);
        info->alignment = alignof(Type);
        info->moveConstruct = [](void* destination, void* source) noexcept {
            ::new (destination) Type(std::move(*static_cast<Type*>(source)));
        };
        info->destroy = [](void* value) noexcept {
            std::destroy_at(static_cast<Type*>(value));
        };

        if (infos_.size() <= id) {
            infos_.resize(static_cast<std::size_t>(id) + 1U);
        }
        infos_[id] = std::move(info);
        ++registeredCount_;
        return id;
    }

    template <typename Component>
    [[nodiscard]] ComponentTypeId Ensure() {
        using Type = std::remove_cvref_t<Component>;
        const ComponentTypeId existing = Find<Type>();
        return existing == INVALID_COMPONENT_TYPE ? Register<Type>() : existing;
    }

    template <typename Component>
    [[nodiscard]] ComponentTypeId Find() const {
        using Type = std::remove_cvref_t<Component>;
        const ComponentTypeId id = detail::StaticComponentTypeId<Type>();
        return id < infos_.size() && infos_[id] != nullptr ? id : INVALID_COMPONENT_TYPE;
    }

    [[nodiscard]] const ComponentInfo& Get(ComponentTypeId id) const {
        if (id >= infos_.size() || infos_[id] == nullptr) {
            throw std::out_of_range("The ECS component type id is not registered");
        }
        return *infos_[id];
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return registeredCount_;
    }

private:
    std::vector<std::unique_ptr<ComponentInfo>> infos_;
    std::size_t registeredCount_ = 0;
};

} // namespace ecs

/** 在组件结构体内部声明稳定 GUID。跨 DLL 或需要序列化的组件必须显式声明。 */
#define ECS_DECLARE_COMPONENT_GUID(HIGH, LOW)                                      \
    inline static constexpr ::ecs::ComponentGuid ECS_COMPONENT_GUID_VALUE{         \
        static_cast<::ecs::u64>(HIGH), static_cast<::ecs::u64>(LOW)}
