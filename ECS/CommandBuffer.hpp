#pragma once

#include "World.hpp"

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs {

/**
 * @brief 延迟执行 ECS 结构变化。
 *
 * Query 遍历期间，Add/Remove/Destroy 可能迁移当前行并破坏遍历。CommandBuffer 先保存
 * 操作，SystemScheduler 在查询结束后 Playback。命令采用类型擦除对象而非 std::function，
 * 因此也能捕获 move-only 组件。
 */
class CommandBuffer {
public:
    CommandBuffer() = default;

    CommandBuffer(const CommandBuffer&)            = delete;
    CommandBuffer& operator=(const CommandBuffer&) = delete;
    CommandBuffer(CommandBuffer&&)                 = default;
    CommandBuffer& operator=(CommandBuffer&&)      = default;

    template <typename... Components>
    void Create(Components&&... components) {
        auto values = std::make_tuple(std::forward<Components>(components)...);
        Enqueue([values = std::move(values)](World& world) mutable {
            std::apply(
                [&world](auto&&... unpacked) {
                    (void)world.CreateEntity(std::move(unpacked)...);
                },
                values);
        });
    }

    void Destroy(Entity entity) {
        Enqueue([entity](World& world) {
            (void)world.DestroyEntity(entity);
        });
    }

    template <typename Component, typename... Arguments>
    void Add(Entity entity, Arguments&&... arguments) {
        auto values = std::make_tuple(std::forward<Arguments>(arguments)...);
        Enqueue([entity, values = std::move(values)](World& world) mutable {
            std::apply(
                [&world, entity](auto&&... unpacked) {
                    (void)world.Add<Component>(entity, std::move(unpacked)...);
                },
                values);
        });
    }

    template <typename Component>
    void Remove(Entity entity) {
        Enqueue([entity](World& world) {
            (void)world.Remove<Component>(entity);
        });
    }

    template <typename Component, typename Value>
    void Set(Entity entity, Value&& value) {
        using StoredValue = std::decay_t<Value>;
        Enqueue([entity, value = StoredValue(std::forward<Value>(value))](World& world) mutable {
            (void)world.Set<Component>(entity, std::move(value));
        });
    }

    template <typename Function>
    void Enqueue(Function&& function) {
        using Type = std::decay_t<Function>;
        commands_.push_back(std::make_unique<Command<Type>>(std::forward<Function>(function)));
    }

    void Playback(World& world);

    void Clear() noexcept {
        commands_.clear();
    }

    [[nodiscard]] bool Empty() const noexcept {
        return commands_.empty();
    }

    [[nodiscard]] std::size_t Size() const noexcept {
        return commands_.size();
    }

private:
    struct CommandBase {
        virtual ~CommandBase() = default;
        virtual void Execute(World& world) = 0;
    };

    template <typename Function>
    struct Command final : CommandBase {
        template <typename Value>
        explicit Command(Value&& value)
            : function(std::forward<Value>(value)) {
        }

        void Execute(World& world) override {
            std::invoke(function, world);
        }

        Function function;
    };

    std::vector<std::unique_ptr<CommandBase>> commands_;
};

} // namespace ecs

