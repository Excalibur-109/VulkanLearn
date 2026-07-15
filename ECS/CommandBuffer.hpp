#pragma once

#include "LinearArena.hpp"
#include "World.hpp"

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs {

/**
 * 查询期间不能迁移当前实体，CommandBuffer 把结构变化保存到遍历结束后执行。
 * 命令没有基类：每条记录只有 payload void*、Execute 函数指针和 Destroy 函数指针。
 */
class CommandBuffer {
public:
    CommandBuffer() = default;
    ECS_API ~CommandBuffer();

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
        Enqueue([entity](World& world) { (void)world.DestroyEntity(entity); });
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
        Enqueue([entity](World& world) { (void)world.Remove<Component>(entity); });
    }

    template <typename Component, typename Value>
    void Set(Entity entity, Value&& value) {
        using Stored = std::decay_t<Value>;
        Enqueue([entity, value = Stored(std::forward<Value>(value))](World& world) mutable {
            (void)world.Set<Component>(entity, std::move(value));
        });
    }

    template <typename Function>
    void Enqueue(Function&& function) {
        using Type = std::decay_t<Function>;
        void* payload = recording_.arena.Allocate(sizeof(Type), alignof(Type));
        std::construct_at(static_cast<Type*>(payload), std::forward<Function>(function));

        Command command{
            payload,
            [](void* object, World& world) {
                std::invoke(*static_cast<Type*>(object), world);
            },
            [](void* object) noexcept {
                std::destroy_at(static_cast<Type*>(object));
            }};
        recording_.commands.push_back(std::move(command));
    }

    ECS_API void Playback(World& world);
    ECS_API void Clear() noexcept;
    void Reserve(std::size_t commandCount) { recording_.commands.reserve(commandCount); }
    [[nodiscard]] bool Empty() const noexcept { return recording_.commands.empty(); }
    [[nodiscard]] std::size_t Size() const noexcept { return recording_.commands.size(); }
    [[nodiscard]] ECS_API std::size_t ReservedBytes() const noexcept;

private:
    class Command {
    public:
        using ExecuteFunction = void (*)(void* payload, World& world);
        using DestroyFunction = void (*)(void* payload) noexcept;

        Command(
            void* payload,
            ExecuteFunction execute,
            DestroyFunction destroy) noexcept
            : payload_(payload),
              execute_(execute),
              destroy_(destroy) {
        }

        ~Command() { Reset(); }
        Command(const Command&)            = delete;
        Command& operator=(const Command&) = delete;

        Command(Command&& other) noexcept { MoveFrom(other); }

        Command& operator=(Command&& other) noexcept {
            if (this != std::addressof(other)) {
                Reset();
                MoveFrom(other);
            }
            return *this;
        }

        void Execute(World& world) { execute_(payload_, world); }

    private:
        void Reset() noexcept {
            if (payload_ != nullptr) {
                destroy_(payload_);
                payload_ = nullptr;
            }
        }

        void MoveFrom(Command& other) noexcept {
            payload_   = std::exchange(other.payload_, nullptr);
            execute_   = other.execute_;
            destroy_   = other.destroy_;
        }

        void* payload_          = nullptr;
        ExecuteFunction execute_ = nullptr;
        DestroyFunction destroy_ = nullptr;
    };

    struct CommandStorage {
        LinearArena arena;
        std::vector<Command> commands;

        void Clear() noexcept {
            commands.clear();
            arena.Reset();
        }

        void Swap(CommandStorage& other) noexcept {
            arena.Swap(other.arena);
            commands.swap(other.commands);
        }
    };

    CommandStorage recording_;
    CommandStorage playback_;
};

} // namespace ecs

