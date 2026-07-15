#pragma once

#include "CommandBuffer.hpp"
#include "Memory.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs {

struct SystemDesc {
    std::string name;
    int order    = 0;
    bool enabled = true;
};

/**
 * System 是组合式拥有者，不是业务系统的基类。它在 C 堆保存具体系统对象，并保存
 * Update/Destroy 函数指针。业务类无需继承、无需 virtual，只需公开 OnUpdate 方法。
 */
class System {
public:
    ~System() { Reset(); }

    System(const System&)            = delete;
    System& operator=(const System&) = delete;
    System(System&& other) noexcept { MoveFrom(other); }

    System& operator=(System&& other) noexcept {
        if (this != std::addressof(other)) {
            Reset();
            MoveFrom(other);
        }
        return *this;
    }

    [[nodiscard]] std::string_view Name() const noexcept { return name_; }
    [[nodiscard]] int Order() const noexcept { return order_; }
    [[nodiscard]] bool Enabled() const noexcept { return enabled_; }
    void SetOrder(int order) noexcept { order_ = order; }
    void SetEnabled(bool enabled) noexcept { enabled_ = enabled; }

    template <typename Type>
    [[nodiscard]] bool Is() const noexcept {
        return type_ == std::type_index(typeid(Type));
    }

    template <typename Type>
    [[nodiscard]] Type* Get() noexcept {
        return Is<Type>() ? static_cast<Type*>(object_) : nullptr;
    }

private:
    friend class SystemScheduler;

    using UpdateFunction = void (*)(void*, World&, CommandBuffer&, float);
    using OnDestroyFunction = void (*)(void*, World&) noexcept;
    using DestroyObjectFunction = void (*)(void*) noexcept;

    System() = default;

    template <typename Type, typename... Arguments>
    static System Create(World& world, SystemDesc desc, Arguments&&... arguments) {
        static_assert(requires(Type& value, World& target, CommandBuffer& commands, float dt) {
            value.OnUpdate(target, commands, dt);
        }, "A composed ECS system must provide public OnUpdate(World&, CommandBuffer&, float)");

        constexpr bool hasOnCreate = requires(Type& value, World& target) {
            value.OnCreate(target);
        };
        constexpr bool hasOnDestroy = requires(Type& value, World& target) {
            value.OnDestroy(target);
        };
        if constexpr (hasOnDestroy) {
            static_assert(
                noexcept(std::declval<Type&>().OnDestroy(std::declval<World&>())),
                "System::OnDestroy must be noexcept");
        }

        void* object     = AllocateRawMemory(sizeof(Type), alignof(Type));
        bool constructed = false;
        try {
            std::construct_at(static_cast<Type*>(object), std::forward<Arguments>(arguments)...);
            constructed = true;
            if constexpr (hasOnCreate) {
                static_cast<Type*>(object)->OnCreate(world);
            }
        } catch (...) {
            if (constructed) {
                std::destroy_at(static_cast<Type*>(object));
            }
            FreeRawMemory(object, alignof(Type));
            throw;
        }

        System result;
        result.world_     = std::addressof(world);
        result.object_    = object;
        result.alignment_ = alignof(Type);
        result.type_      = std::type_index(typeid(Type));
        result.name_      = desc.name.empty() ? typeid(Type).name() : std::move(desc.name);
        result.order_     = desc.order;
        result.enabled_   = desc.enabled;
        result.update_ = [](void* value, World& target, CommandBuffer& commands, float dt) {
            static_cast<Type*>(value)->OnUpdate(target, commands, dt);
        };
        if constexpr (hasOnDestroy) {
            result.onDestroy_ = [](void* value, World& target) noexcept {
                static_cast<Type*>(value)->OnDestroy(target);
            };
        }
        result.destroyObject_ = [](void* value) noexcept {
            std::destroy_at(static_cast<Type*>(value));
        };
        return result;
    }

    void Update(World& world, CommandBuffer& commands, float deltaTime) {
        update_(object_, world, commands, deltaTime);
    }
    ECS_API void Reset() noexcept;
    ECS_API void MoveFrom(System& other) noexcept;

    World* world_                     = nullptr;
    void* object_                     = nullptr;
    std::size_t alignment_            = 1;
    std::type_index type_{typeid(void)};
    std::string name_;
    int order_                         = 0;
    bool enabled_                      = true;
    UpdateFunction update_             = nullptr;
    OnDestroyFunction onDestroy_       = nullptr;
    DestroyObjectFunction destroyObject_ = nullptr;
};

class SystemScheduler {
public:
    explicit SystemScheduler(World& world) noexcept
        : world_(std::addressof(world)) {
    }

    ECS_API ~SystemScheduler();
    SystemScheduler(const SystemScheduler&)            = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;

    template <typename Type, typename... Arguments>
    Type& AddSystem(Arguments&&... arguments) {
        return AddSystem<Type>(SystemDesc{}, std::forward<Arguments>(arguments)...);
    }

    template <typename Type, typename... Arguments>
    Type& AddSystem(SystemDesc desc, Arguments&&... arguments) {
        System system = System::Create<Type>(
            *world_, std::move(desc), std::forward<Arguments>(arguments)...);
        Type* result = system.Get<Type>();
        systems_.push_back(Entry{std::move(system), nextSequence_++});
        sortDirty_ = true;
        return *result;
    }

    template <typename Type>
    [[nodiscard]] Type* GetSystem() noexcept {
        for (Entry& entry : systems_) {
            if (Type* result = entry.system.Get<Type>(); result != nullptr) {
                return result;
            }
        }
        return nullptr;
    }

    template <typename Type>
    bool RemoveSystem() {
        const auto iterator = std::find_if(systems_.begin(), systems_.end(), [](const Entry& entry) {
            return entry.system.Is<Type>();
        });
        if (iterator == systems_.end()) {
            return false;
        }
        systems_.erase(iterator);
        return true;
    }

    template <typename Type>
    bool SetSystemEnabled(bool enabled) noexcept {
        for (Entry& entry : systems_) {
            if (entry.system.Is<Type>()) {
                entry.system.SetEnabled(enabled);
                return true;
            }
        }
        return false;
    }

    template <typename Type>
    bool SetSystemOrder(int order) noexcept {
        for (Entry& entry : systems_) {
            if (entry.system.Is<Type>()) {
                entry.system.SetOrder(order);
                sortDirty_ = true;
                return true;
            }
        }
        return false;
    }

    ECS_API void Update(float deltaTime);
    void Clear() noexcept { systems_.clear(); commandBuffer_.Clear(); }
    [[nodiscard]] std::size_t Size() const noexcept { return systems_.size(); }

private:
    struct Entry {
        System system;
        u64 sequence = 0;
    };

    ECS_API void SortIfNeeded();

    World* world_ = nullptr;
    std::vector<Entry> systems_;
    CommandBuffer commandBuffer_;
    u64 nextSequence_ = 0;
    bool sortDirty_    = false;
};

} // namespace ecs
