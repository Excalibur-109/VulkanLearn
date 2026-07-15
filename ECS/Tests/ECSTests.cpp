#include "ECS/ECS.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Position { float x = 0.0F; float y = 0.0F; };
struct Velocity { float x = 0.0F; float y = 0.0F; };
struct Health { int value = 100; };
struct Disabled {};
struct Name { std::string value; };

struct alignas(64) AlignedComponent {
    std::uint64_t values[8]{};
};

struct MoveOnlyComponent {
    explicit MoveOnlyComponent(int input)
        : value(std::make_unique<int>(input)) {}
    MoveOnlyComponent(MoveOnlyComponent&&) noexcept            = default;
    MoveOnlyComponent& operator=(MoveOnlyComponent&&) noexcept = default;
    std::unique_ptr<int> value;
};

struct LifetimeProbe {
    inline static int live = 0;
    LifetimeProbe() { ++live; }
    LifetimeProbe(LifetimeProbe&&) noexcept { ++live; }
    ~LifetimeProbe() { --live; }
};

struct ThrowOnCopy {
    ThrowOnCopy() = default;
    ThrowOnCopy(const ThrowOnCopy&) {
        throw std::runtime_error("Expected component copy failure");
    }
    ThrowOnCopy(ThrowOnCopy&&) noexcept = default;
};

struct ThrowOnConstruction {
    explicit ThrowOnConstruction(int) {
        throw std::runtime_error("Expected component construction failure");
    }
    ThrowOnConstruction(ThrowOnConstruction&&) noexcept = default;
};

struct alignas(64) OverAlignedCommand {
    bool* aligned = nullptr;
    std::byte padding[64U - sizeof(bool*)]{};

    void operator()(ecs::World&) const {
        *aligned = reinterpret_cast<std::uintptr_t>(this) % alignof(OverAlignedCommand) == 0;
    }
};

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestSignatureFilter() {
    constexpr ecs::ComponentTypeId a = 2;
    constexpr ecs::ComponentTypeId b = 70;
    constexpr ecs::ComponentTypeId c = 130;
    constexpr ecs::ComponentTypeId d = 191;
    ecs::Signature signature{a, b};
    ecs::Filter filter;
    filter.Require(a).RequireAny(b).RequireAny(c).Exclude(d);
    Check(filter.Matches(signature), "Dynamic Filter rejected a valid signature");
    signature.Set(d);
    Check(!filter.Matches(signature), "Filter none condition failed");
    signature.Reset(d);
    Check(signature.Count() == 2, "Dynamic Signature reset/count failed");
}

void TestEntityAndMigration() {
    ecs::World world;
    const ecs::Entity first = world.CreateEntity(Position{1, 2}, Velocity{3, 4}, Name{"first"});
    const ecs::Entity second = world.CreateEntity(Position{10, 20}, Velocity{30, 40});
    Check(world.Remove<Velocity>(first), "Remove failed");
    Check(world.Get<Position>(first).x == 1, "Remove migration lost Position");
    Check(world.Get<Velocity>(second).x == 30, "Swap-remove broke another entity record");
    world.Add<Health>(second, Health{75});
    Check(world.Get<Health>(second).value == 75, "Add failed");
    Check(world.Get<Position>(second).y == 20, "Add migration lost Position");

    Check(world.DestroyEntity(first), "Destroy failed");
    const ecs::Entity reused = world.CreateEntity(Position{});
    Check(reused.index == first.index, "Free Entity slot was not reused");
    Check(reused.generation != first.generation, "Entity generation was not advanced");
    Check(!world.IsAlive(first), "Stale Entity became alive");
}

void TestChunksQueryAndAlignment() {
    ecs::World world;
    constexpr int count = 4000;
    std::vector<ecs::Entity> entities;
    entities.reserve(count);
    for (int index = 0; index < count; ++index) {
        entities.push_back(world.CreateEntity(Position{static_cast<float>(index), 0}));
    }

    std::size_t chunkCount = 0;
    world.MakeQuery<Position>().EachChunk(
        [&](std::span<const ecs::Entity> ids, std::span<Position> positions) {
            Check(ids.size() == positions.size(), "SoA column lengths differ");
            ++chunkCount;
        });
    Check(chunkCount > 1, "Test did not create multiple chunks");

    // 大量跨 Archetype 迁移，验证每个 Chunk 的 swap-remove 位置修正。
    for (int index = 0; index < count; index += 2) {
        world.Add<Velocity>(entities[index], Velocity{static_cast<float>(index + 1), 0});
    }
    for (int index = 0; index < count; index += 6) {
        Check(world.Remove<Velocity>(entities[index]), "Bulk Remove failed");
    }
    for (int index = 0; index < count; ++index) {
        Check(world.Get<Position>(entities[index]).x == index, "Bulk migration corrupted data");
    }

    ecs::JobSystem jobs(3);
    world.MakeQuery<Position>().ParallelEachChunk(
        jobs,
        [](std::span<const ecs::Entity>, std::span<Position> positions) {
            for (Position& position : positions) {
                position.y = 7.0F;
            }
        });
    for (const ecs::Entity entity : entities) {
        Check(world.Get<Position>(entity).y == 7.0F, "ParallelEachChunk missed an entity");
    }

    const ecs::Entity alignedEntity = world.CreateEntity(AlignedComponent{});
    const auto* aligned = world.TryGet<AlignedComponent>(alignedEntity);
    Check(
        reinterpret_cast<std::uintptr_t>(aligned) % alignof(AlignedComponent) == 0,
        "C heap over-aligned allocation failed");
}

void TestJobExceptionPropagation() {
    ecs::JobSystem jobs(3);
    bool caught = false;
    try {
        jobs.ParallelFor(100, 1, [](std::size_t index) {
            if (index == 37) {
                throw std::runtime_error("Expected parallel job failure");
            }
        });
    } catch (const std::runtime_error&) {
        caught = true;
    }
    Check(caught, "JobSystem did not propagate a worker exception");

    std::atomic<int> nestedCount{0};
    jobs.ParallelFor(16, 1, [&](std::size_t) {
        jobs.ParallelFor(4, 1, [&](std::size_t) {
            nestedCount.fetch_add(1, std::memory_order_relaxed);
        });
    });
    Check(nestedCount.load(std::memory_order_relaxed) == 64, "Nested ParallelFor failed");
}

void TestChunkCompactionAndRelease() {
    ecs::World world;
    std::vector<ecs::Entity> entities;
    entities.reserve(5000);
    for (int index = 0; index < 5000; ++index) {
        entities.push_back(world.CreateEntity(Position{static_cast<float>(index), 0.0F}));
    }

    std::size_t initialChunks = 0;
    world.MakeQuery<const Position>().EachChunk(
        [&](std::span<const ecs::Entity>, std::span<const Position>) { ++initialChunks; });
    Check(initialChunks > 1, "Compaction test did not allocate multiple chunks");

    for (std::size_t index = 0; index + 1U < entities.size(); ++index) {
        Check(world.DestroyEntity(entities[index]), "Compaction destroy failed");
    }

    std::size_t remainingChunks = 0;
    world.MakeQuery<const Position>().EachChunk(
        [&](std::span<const ecs::Entity>, std::span<const Position> positions) {
            ++remainingChunks;
            Check(positions.size() == 1, "Compacted chunk contains the wrong row count");
        });
    Check(remainingChunks == 1, "Empty trailing chunks were not released");
    Check(
        world.Get<Position>(entities.back()).x == 4999.0F,
        "Cross-chunk compaction corrupted the surviving entity");
}

void TestFilterAndCommands() {
    ecs::World world;
    const ecs::Entity normal = world.CreateEntity(Position{}, Velocity{1, 0});
    (void)world.CreateEntity(Position{}, Velocity{}, Disabled{});
    auto query = world.MakeQuery<Position, const Velocity>();
    query.Without<Disabled>();
    Check(query.Count() == 1, "Query Filter count is wrong");

    bool guard = false;
    query.Each([&](ecs::Entity entity, Position&, const Velocity&) {
        try {
            (void)world.DestroyEntity(entity);
        } catch (const std::logic_error&) {
            guard = true;
        }
    });
    Check(guard, "Structural mutation during Query was not rejected");

    ecs::CommandBuffer commands;
    commands.Set<Position>(normal, Position{5, 6});
    commands.Add<Health>(normal, Health{25});
    commands.Remove<Health>(normal);
    commands.Create(MoveOnlyComponent{42});
    commands.Playback(world);
    Check(world.Get<Position>(normal).x == 5, "Deferred Set failed");
    Check(!world.Has<Health>(normal), "Deferred Remove failed");
    int moveOnlyValue = 0;
    world.MakeQuery<const MoveOnlyComponent>().Each(
        [&](const MoveOnlyComponent& value) { moveOnlyValue = *value.value; });
    Check(moveOnlyValue == 42, "Raw command payload lost move-only data");

    bool alignedCommand = false;
    int deferredValue   = 0;
    commands.Enqueue(OverAlignedCommand{&alignedCommand});
    commands.Enqueue([&](ecs::World&) {
        commands.Enqueue([&](ecs::World&) { deferredValue = 7; });
    });
    commands.Playback(world);
    Check(alignedCommand, "LinearArena did not align an over-aligned command payload");
    Check(commands.Size() == 1 && deferredValue == 0, "Playback did not double-buffer new commands");
    commands.Playback(world);
    Check(deferredValue == 7 && commands.Empty(), "Second command playback failed");
}

class MovementSystem {
public:
    inline static int destroys = 0;

    void OnUpdate(ecs::World& world, ecs::CommandBuffer& commands, float dt) {
        world.MakeQuery<Position, const Velocity>().Without<Disabled>().Each(
            [&](ecs::Entity entity, Position& position, const Velocity& velocity) {
                position.x += velocity.x * dt;
                commands.Add<Disabled>(entity);
            });
    }

    void OnDestroy(ecs::World&) noexcept { ++destroys; }
};

class CountSystem {
public:
    std::size_t count = 0;
    void OnUpdate(ecs::World& world, ecs::CommandBuffer&, float) {
        count = world.MakeQuery<const Position>().Without<Disabled>().Count();
    }
};

class FailingCreateSystem {
public:
    inline static int live = 0;
    FailingCreateSystem() { ++live; }
    ~FailingCreateSystem() { --live; }
    void OnCreate(ecs::World&) { throw std::runtime_error("Expected OnCreate failure"); }
    void OnUpdate(ecs::World&, ecs::CommandBuffer&, float) {}
};

void TestComposedSystems() {
    MovementSystem::destroys = 0;
    ecs::World world;
    const ecs::Entity entity = world.CreateEntity(Position{}, Velocity{2, 0});
    {
        ecs::SystemScheduler scheduler(world);
        scheduler.AddSystem<CountSystem>(ecs::SystemDesc{"Count", 10, true});
        scheduler.AddSystem<MovementSystem>(ecs::SystemDesc{"Movement", -10, true});
        scheduler.Update(0.5F);
        Check(std::abs(world.Get<Position>(entity).x - 1.0F) < 0.0001F, "System update failed");
        Check(scheduler.GetSystem<CountSystem>()->count == 0, "System order/playback failed");

        const ecs::Entity second = world.CreateEntity(Position{}, Velocity{4, 0});
        Check(scheduler.SetSystemEnabled<MovementSystem>(false), "System enable lookup failed");
        scheduler.Update(0.5F);
        Check(world.Get<Position>(second).x == 0, "Disabled composed System still executed");
        Check(scheduler.GetSystem<CountSystem>()->count == 1, "Later System did not observe enable state");
    }
    Check(MovementSystem::destroys == 1, "Composed System OnDestroy was not called");

    ecs::SystemScheduler scheduler(world);
    bool failed = false;
    try {
        scheduler.AddSystem<FailingCreateSystem>();
    } catch (const std::runtime_error&) {
        failed = true;
    }
    Check(failed && FailingCreateSystem::live == 0, "System OnCreate rollback leaked an object");
}

void TestDestructionAndClear() {
    LifetimeProbe::live = 0;
    ecs::World world;
    const ecs::Entity old = world.CreateEntity(LifetimeProbe{});
    Check(LifetimeProbe::live == 1, "Component construction count failed");
    world.Clear();
    Check(LifetimeProbe::live == 0, "World Clear did not destroy raw components");
    (void)world.CreateEntity(Position{});
    Check(!world.IsAlive(old), "Entity from before Clear became valid again");
}

void TestComponentExceptionRollback() {
    ecs::World world;
    ThrowOnCopy source;
    bool createFailed = false;
    try {
        (void)world.CreateEntity(source);
    } catch (const std::runtime_error&) {
        createFailed = true;
    }
    Check(createFailed && world.EntityCount() == 0, "Create failure left a partial Entity row");

    const ecs::Entity entity = world.CreateEntity(Position{9, 8});
    bool addFailed = false;
    try {
        (void)world.Add<ThrowOnConstruction>(entity, 1);
    } catch (const std::runtime_error&) {
        addFailed = true;
    }
    Check(addFailed, "Expected Add constructor failure was not observed");
    Check(world.IsAlive(entity), "Failed Add invalidated source Entity");
    Check(!world.Has<ThrowOnConstruction>(entity), "Failed Add left a partial component");
    Check(world.Get<Position>(entity).x == 9, "Failed Add damaged retained data");
}

} // namespace

int main() {
    try {
        TestSignatureFilter();
        TestEntityAndMigration();
        TestChunksQueryAndAlignment();
        TestJobExceptionPropagation();
        TestChunkCompactionAndRelease();
        TestFilterAndCommands();
        TestComposedSystems();
        TestDestructionAndClear();
        TestComponentExceptionRollback();
        std::cout << "All Archetype ECS tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ECS test failed: " << exception.what() << '\n';
        return 1;
    }
}
