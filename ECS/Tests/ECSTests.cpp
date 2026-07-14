#include "ECS/ECS.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
};

struct Velocity {
    float x = 0.0F;
    float y = 0.0F;
};

struct Health {
    int value = 100;
};

struct Disabled {};

struct Name {
    std::string value;
};

struct alignas(64) AlignedComponent {
    std::uint64_t values[8]{};
};

struct LifetimeProbe {
    inline static int liveCount = 0;

    LifetimeProbe() {
        ++liveCount;
    }

    LifetimeProbe(LifetimeProbe&&) noexcept {
        ++liveCount;
    }

    ~LifetimeProbe() {
        --liveCount;
    }
};

struct ThrowOnConstruction {
    explicit ThrowOnConstruction(int) {
        throw std::runtime_error("Expected component construction failure");
    }

    ThrowOnConstruction(ThrowOnConstruction&&) noexcept = default;
};

struct MoveOnlyComponent {
    explicit MoveOnlyComponent(int value)
        : value(std::make_unique<int>(value)) {
    }

    MoveOnlyComponent(MoveOnlyComponent&&) noexcept            = default;
    MoveOnlyComponent& operator=(MoveOnlyComponent&&) noexcept = default;

    std::unique_ptr<int> value;
};

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestSignatureAndFilter() {
    constexpr ecs::ComponentTypeId positionType = 2;
    constexpr ecs::ComponentTypeId velocityType = 70;
    constexpr ecs::ComponentTypeId healthType   = 130;
    constexpr ecs::ComponentTypeId disabledType = 191;

    ecs::Signature signature{positionType, velocityType};
    Check(signature.Test(positionType), "Signature lost a low component bit");
    Check(signature.Test(velocityType), "Signature lost a component bit above 64");
    Check(signature.Count() == 2, "Signature returned the wrong bit count");

    ecs::Filter filter;
    filter.Require(positionType).RequireAny(velocityType).RequireAny(healthType).Exclude(disabledType);
    Check(filter.Matches(signature), "Filter rejected a matching dynamic signature");

    signature.Set(disabledType);
    Check(!filter.Matches(signature), "Filter none set did not reject an archetype");
    signature.Reset(disabledType);
    Check(filter.Matches(signature), "Signature reset did not restore filter matching");

    ecs::Signature same{velocityType, positionType};
    Check(signature == same, "Signature equality depends on insertion order");
    Check(
        ecs::SignatureHash{}(signature) == ecs::SignatureHash{}(same),
        "Equal signatures must have equal hashes");
}

void TestEntityGenerationAndComponents() {
    ecs::World world;
    const ecs::ComponentTypeId positionType = world.RegisterComponent<Position>("Position");
    Check(
        world.Components().Get(positionType).name == "Position",
        "Registered component metadata did not preserve its name");
    const ecs::Entity first = world.CreateEntity(Position{1.0F, 2.0F}, Name{"first"});

    Check(world.IsAlive(first), "Created entity must be alive");
    Check(world.Get<Position>(first).x == 1.0F, "Position value was not preserved");
    Check(world.Get<Name>(first).value == "first", "Non-trivial component was not preserved");
    Check(world.DestroyEntity(first), "DestroyEntity should destroy a live entity");
    Check(!world.IsAlive(first), "A destroyed entity handle must become stale");

    const ecs::Entity reused = world.CreateEntity(Position{3.0F, 4.0F});
    Check(reused.index == first.index, "The free entity slot should be reused");
    Check(reused.generation != first.generation, "A reused slot must change generation");
    Check(world.TryGet<Position>(first) == nullptr, "A stale handle must not access a new entity");
}

void TestEmptyEntityAndQuery() {
    ecs::World world;
    const ecs::Entity empty    = world.CreateEntity();
    const ecs::Entity position = world.CreateEntity(Position{});

    std::size_t count = 0;
    world.MakeQuery<>().Each([&](ecs::Entity entity) {
        Check(entity == empty || entity == position, "Zero-component query returned an unknown entity");
        ++count;
    });
    Check(count == 2, "Zero-component query must match every live entity");
}

void TestArchetypeMigrationAndSwapRemove() {
    ecs::World world;
    const ecs::Entity first =
        world.CreateEntity(Position{1.0F, 2.0F}, Velocity{3.0F, 4.0F});
    const ecs::Entity second =
        world.CreateEntity(Position{10.0F, 20.0F}, Velocity{30.0F, 40.0F});

    Check(world.Remove<Velocity>(first), "Remove should migrate an existing component");
    Check(!world.Has<Velocity>(first), "Removed component still exists");
    Check(world.Get<Position>(first).x == 1.0F, "Migration changed a retained component");

    // first 的源行会被 second 填补，这里验证 second 的反向索引也被修正。
    Check(world.Get<Position>(second).x == 10.0F, "Swap-remove broke the moved entity record");
    Check(world.Get<Velocity>(second).x == 30.0F, "Swap-remove broke component data");

    Health& health = world.Add<Health>(second, Health{75});
    Check(health.value == 75, "Add did not construct the new component");
    Check(world.Get<Position>(second).y == 20.0F, "Add migration lost Position");
    Check(world.Get<Velocity>(second).y == 40.0F, "Add migration lost Velocity");
}

void TestQueryFiltersAndChunkIteration() {
    ecs::World world;
    (void)world.CreateEntity(Position{1.0F, 0.0F}, Velocity{1.0F, 0.0F});
    (void)world.CreateEntity(Position{2.0F, 0.0F}, Health{50});
    (void)world.CreateEntity(
        Position{3.0F, 0.0F},
        Velocity{3.0F, 0.0F},
        Disabled{});
    (void)world.CreateEntity(Velocity{4.0F, 0.0F});

    auto moving = world.MakeQuery<Position, const Velocity>();
    moving.Without<Disabled>();
    Check(moving.Count() == 1, "all/none filter returned the wrong entity count");

    moving.Each([](Position& position, const Velocity& velocity) {
        position.x += velocity.x;
    });

    std::size_t chunkRows = 0;
    moving.EachChunk([&](
                         std::span<const ecs::Entity> entities,
                         std::span<Position> positions,
                         std::span<const Velocity> velocities) {
        Check(entities.size() == positions.size(), "Entity and component spans must align");
        Check(positions.size() == velocities.size(), "Component spans must have equal rows");
        chunkRows += entities.size();
    });
    Check(chunkRows == 1, "EachChunk visited the wrong number of rows");

    auto positionWithHealthOrVelocity = world.MakeQuery<const Position>();
    positionWithHealthOrVelocity.WithAny<Health, Velocity>();
    Check(positionWithHealthOrVelocity.Count() == 3, "any filter did not match all alternatives");
}

void TestMultipleChunksAndAlignment() {
    ecs::World world;
    constexpr int entityCount = 5000;
    for (int index = 0; index < entityCount; ++index) {
        (void)world.CreateEntity(Position{static_cast<float>(index), 0.0F});
    }

    auto positions = world.MakeQuery<Position>();
    Check(positions.Count() == entityCount, "Query did not span all chunks");

    std::size_t chunkCount = 0;
    positions.EachChunk([&](std::span<const ecs::Entity>, std::span<Position>) {
        ++chunkCount;
    });
    Check(chunkCount > 1, "The test did not create more than one chunk");

    const ecs::Entity alignedEntity = world.CreateEntity(AlignedComponent{});
    const auto* aligned = world.TryGet<AlignedComponent>(alignedEntity);
    Check(
        reinterpret_cast<std::uintptr_t>(aligned) % alignof(AlignedComponent) == 0,
        "Over-aligned component storage is not correctly aligned");
}

void TestBulkMigrationAcrossChunks() {
    ecs::World world;
    std::vector<ecs::Entity> entities;
    constexpr int entityCount = 3000;
    entities.reserve(entityCount);

    for (int index = 0; index < entityCount; ++index) {
        if ((index % 2) == 0) {
            entities.push_back(world.CreateEntity(
                Position{static_cast<float>(index), 0.0F},
                Velocity{static_cast<float>(index + 1), 0.0F}));
        } else {
            entities.push_back(world.CreateEntity(Position{static_cast<float>(index), 0.0F}));
        }
    }

    // 交错增删使两个 Archetype 的多个 Chunk 都反复执行 swap-remove。
    for (int index = 0; index < entityCount; ++index) {
        if ((index % 3) == 0 && world.Has<Velocity>(entities[index])) {
            Check(world.Remove<Velocity>(entities[index]), "Bulk Remove failed");
        } else if ((index % 3) == 1 && !world.Has<Velocity>(entities[index])) {
            (void)world.Add<Velocity>(
                entities[index],
                Velocity{static_cast<float>(index + 1), 0.0F});
        }
    }

    std::size_t expectedAlive    = entityCount;
    std::size_t expectedVelocity = 0;
    for (int index = 0; index < entityCount; ++index) {
        const ecs::Entity entity = entities[index];
        Check(world.Get<Position>(entity).x == index, "Bulk migration corrupted Position");
        if (world.Has<Velocity>(entity)) {
            ++expectedVelocity;
            Check(
                world.Get<Velocity>(entity).x == index + 1,
                "Bulk migration corrupted Velocity");
        }

        if ((index % 5) == 0) {
            Check(world.DestroyEntity(entity), "Bulk Destroy failed");
            --expectedAlive;
        }
    }

    Check(world.EntityCount() == expectedAlive, "Bulk migration produced the wrong live count");
    Check(
        world.MakeQuery<const Position>().Count() == expectedAlive,
        "Position query count is wrong after bulk migration");

    std::size_t destroyedWithVelocity = 0;
    for (int index = 0; index < entityCount; index += 5) {
        // 已销毁句柄不能再查询，但销毁前的模式可以由迁移规则重算。
        const bool originallyHadVelocity = (index % 2) == 0;
        const bool removed = (index % 3) == 0 && originallyHadVelocity;
        const bool added   = (index % 3) == 1 && !originallyHadVelocity;
        if ((originallyHadVelocity && !removed) || added) {
            ++destroyedWithVelocity;
        }
    }
    expectedVelocity -= destroyedWithVelocity;
    Check(
        world.MakeQuery<const Velocity>().Count() == expectedVelocity,
        "Velocity query count is wrong after bulk destruction");
}

void TestIterationGuardAndCommandBuffer() {
    ecs::World world;
    (void)world.CreateEntity(Position{});
    (void)world.CreateEntity(Position{});

    bool guardTriggered = false;
    world.MakeQuery<Position>().Each([&](ecs::Entity entity, Position&) {
        try {
            (void)world.DestroyEntity(entity);
        } catch (const std::logic_error&) {
            guardTriggered = true;
        }
    });
    Check(guardTriggered, "Direct structural change during iteration was not rejected");

    ecs::CommandBuffer commands;
    world.MakeQuery<Position>().Each([&](ecs::Entity entity, Position&) {
        commands.Destroy(entity);
    });
    Check(world.EntityCount() == 2, "Deferred commands executed too early");
    commands.Playback(world);
    Check(world.EntityCount() == 0, "CommandBuffer did not destroy queued entities");
}

void TestAllCommandBufferOperations() {
    ecs::World world;
    const ecs::Entity entity = world.CreateEntity(Position{1.0F, 2.0F});

    ecs::CommandBuffer commands;
    commands.Set<Position>(entity, Position{5.0F, 6.0F});
    commands.Add<Health>(entity, Health{25});
    commands.Remove<Health>(entity);
    commands.Create(Position{9.0F, 10.0F}, Velocity{11.0F, 12.0F});
    commands.Create(MoveOnlyComponent{42});
    Check(commands.Size() == 5, "CommandBuffer returned the wrong queued command count");

    commands.Playback(world);
    Check(commands.Empty(), "Playback must consume queued commands");
    Check(world.Get<Position>(entity).x == 5.0F, "Deferred Set did not update the component");
    Check(!world.Has<Health>(entity), "Deferred Remove did not remove the component");
    Check(world.MakeQuery<const Position>().Count() == 2, "Deferred Create did not create an entity");

    int moveOnlyValue = 0;
    world.MakeQuery<const MoveOnlyComponent>().Each([&](const MoveOnlyComponent& component) {
        moveOnlyValue = *component.value;
    });
    Check(moveOnlyValue == 42, "CommandBuffer did not preserve a move-only component");
}

void TestLifetimeAndExceptionRollback() {
    LifetimeProbe::liveCount = 0;
    {
        ecs::World world;
        const ecs::Entity first = world.CreateEntity(LifetimeProbe{});
        const ecs::Entity second = world.CreateEntity(LifetimeProbe{}, Position{});
        Check(LifetimeProbe::liveCount == 2, "Unexpected live component count after creation");
        Check(world.DestroyEntity(first), "Failed to destroy LifetimeProbe entity");
        Check(LifetimeProbe::liveCount == 1, "Component destructor did not run on destroy");
        world.Clear();
        Check(LifetimeProbe::liveCount == 0, "Component destructor did not run on World::Clear");
        Check(!world.IsAlive(second), "World::Clear must invalidate existing entities");
        const ecs::Entity afterClear = world.CreateEntity(Position{});
        Check(world.IsAlive(afterClear), "World must remain usable after Clear");
        Check(!world.IsAlive(second), "An entity handle from before Clear must never become alive again");
    }
    Check(LifetimeProbe::liveCount == 0, "World destruction leaked a component");

    ecs::World world;
    const ecs::Entity entity = world.CreateEntity(Position{9.0F, 8.0F});
    bool exceptionObserved = false;
    try {
        (void)world.Add<ThrowOnConstruction>(entity, 1);
    } catch (const std::runtime_error&) {
        exceptionObserved = true;
    }
    Check(exceptionObserved, "The expected component constructor exception was not observed");
    Check(world.IsAlive(entity), "Failed Add invalidated the source entity");
    Check(!world.Has<ThrowOnConstruction>(entity), "Failed Add left a partial component");
    Check(world.Get<Position>(entity).x == 9.0F, "Failed Add damaged retained components");
}

class DisableMovedSystem final : public ecs::System {
public:
    DisableMovedSystem()
        : System("DisableMoved", -10) {
    }

protected:
    void OnUpdate(ecs::World& world, ecs::CommandBuffer& commands, float deltaTime) override {
        world.MakeQuery<Position, const Velocity>().Each(
            [&](ecs::Entity entity, Position& position, const Velocity& velocity) {
                position.x += velocity.x * deltaTime;
                commands.Add<Disabled>(entity);
            });
    }
};

class CountEnabledSystem final : public ecs::System {
public:
    CountEnabledSystem()
        : System("CountEnabled", 10) {
    }

    std::size_t count = 0;

protected:
    void OnUpdate(ecs::World& world, ecs::CommandBuffer&, float) override {
        auto query = world.MakeQuery<const Position>();
        query.Without<Disabled>();
        count = query.Count();
    }
};

void TestSystemScheduler() {
    ecs::World world;
    const ecs::Entity entity = world.CreateEntity(Position{}, Velocity{2.0F, 0.0F});
    ecs::SystemScheduler scheduler(world);
    scheduler.AddSystem<CountEnabledSystem>();
    scheduler.AddSystem<DisableMovedSystem>();

    scheduler.Update(0.5F);
    Check(std::abs(world.Get<Position>(entity).x - 1.0F) < 0.0001F, "System did not update data");
    Check(world.Has<Disabled>(entity), "System command was not played back");
    Check(
        scheduler.GetSystem<CountEnabledSystem>()->count == 0,
        "System order or per-system command playback is incorrect");
}

} // namespace

int main() {
    try {
        TestSignatureAndFilter();
        TestEntityGenerationAndComponents();
        TestEmptyEntityAndQuery();
        TestArchetypeMigrationAndSwapRemove();
        TestQueryFiltersAndChunkIteration();
        TestMultipleChunksAndAlignment();
        TestBulkMigrationAcrossChunks();
        TestIterationGuardAndCommandBuffer();
        TestAllCommandBufferOperations();
        TestLifetimeAndExceptionRollback();
        TestSystemScheduler();
        std::cout << "All Archetype ECS tests passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ECS test failed: " << exception.what() << '\n';
        return 1;
    }
}
