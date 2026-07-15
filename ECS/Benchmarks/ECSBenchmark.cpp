#include "ECS/ECS.hpp"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

struct Position {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float padding = 0.0F;
};

struct Velocity {
    float x = 1.0F;
    float y = 2.0F;
    float z = 3.0F;
    float padding = 0.0F;
};

struct Active {};

using Clock = std::chrono::steady_clock;

template <typename Function>
double MeasureMilliseconds(Function&& function) {
    const Clock::time_point begin = Clock::now();
    function();
    const Clock::time_point end = Clock::now();
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

void PrintThroughput(std::string_view name, std::size_t operations, double milliseconds) {
    const double millionPerSecond = static_cast<double>(operations) / milliseconds / 1000.0;
    const double nanosecondsEach  = milliseconds * 1'000'000.0 / static_cast<double>(operations);
    std::cout << std::left << std::setw(24) << name
              << std::right << std::fixed << std::setprecision(2)
              << std::setw(10) << milliseconds << " ms  "
              << std::setw(10) << millionPerSecond << " M/s  "
              << std::setw(8) << nanosecondsEach << " ns/op\n";
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t entityCount = argc > 1
                                        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
                                        : 250'000U;
    const std::size_t iterations = argc > 2
                                       ? static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10))
                                       : 100U;

    ecs::World world;
    world.ReserveEntities(entityCount);
    std::vector<ecs::Entity> entities;
    entities.reserve(entityCount);
    const double createMs = MeasureMilliseconds([&] {
        for (std::size_t index = 0; index < entityCount; ++index) {
            entities.push_back(world.CreateEntity(
                Position{static_cast<float>(index), 0.0F, 0.0F, 0.0F},
                Velocity{}));
        }
    });
    PrintThroughput("Create P+V", entityCount, createMs);

    auto query = world.MakeQuery<Position, const Velocity>();
    query.Each([](Position& position, const Velocity& velocity) {
        position.x += velocity.x;
    });

    const std::size_t updateOperations = entityCount * iterations;
    const double eachMs = MeasureMilliseconds([&] {
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            query.Each([](Position& position, const Velocity& velocity) {
                position.x += velocity.x;
                position.y += velocity.y;
                position.z += velocity.z;
            });
        }
    });
    PrintThroughput("Query::Each", updateOperations, eachMs);

    const double chunkMs = MeasureMilliseconds([&] {
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            query.EachChunk([](
                                std::span<const ecs::Entity>,
                                std::span<Position> positions,
                                std::span<const Velocity> velocities) {
                for (std::size_t row = 0; row < positions.size(); ++row) {
                    positions[row].x += velocities[row].x;
                    positions[row].y += velocities[row].y;
                    positions[row].z += velocities[row].z;
                }
            });
        }
    });
    PrintThroughput("Query::EachChunk", updateOperations, chunkMs);

    ecs::JobSystem jobs;
    const double parallelMs = MeasureMilliseconds([&] {
        for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
            query.ParallelEachChunk(
                jobs,
                [](std::span<const ecs::Entity>,
                   std::span<Position> positions,
                   std::span<const Velocity> velocities) {
                    for (std::size_t row = 0; row < positions.size(); ++row) {
                        positions[row].x += velocities[row].x;
                        positions[row].y += velocities[row].y;
                        positions[row].z += velocities[row].z;
                    }
                });
        }
    });
    PrintThroughput("ParallelEachChunk", updateOperations, parallelMs);
    std::cout << "workers: " << jobs.WorkerCount() << '\n';

    const double addMs = MeasureMilliseconds([&] {
        for (const ecs::Entity entity : entities) {
            (void)world.Add<Active>(entity);
        }
    });
    PrintThroughput("Add tag", entityCount, addMs);

    const double removeMs = MeasureMilliseconds([&] {
        for (const ecs::Entity entity : entities) {
            (void)world.Remove<Active>(entity);
        }
    });
    PrintThroughput("Remove tag", entityCount, removeMs);

    ecs::CommandBuffer commands;
    commands.Reserve(entityCount);
    std::uint64_t commandChecksum = 0;
    const double queueMs = MeasureMilliseconds([&] {
        for (std::size_t index = 0; index < entityCount; ++index) {
            commands.Enqueue([index, &commandChecksum](ecs::World&) {
                commandChecksum += index;
            });
        }
    });
    PrintThroughput("Command queue", entityCount, queueMs);

    const double playbackMs = MeasureMilliseconds([&] {
        commands.Playback(world);
    });
    PrintThroughput("Command playback", entityCount, playbackMs);

    double checksum = 0.0;
    world.MakeQuery<const Position>().Each([&](const Position& position) {
        checksum += position.x + position.y + position.z;
    });
    std::cout << "checksum: " << checksum + static_cast<double>(commandChecksum) << '\n';
    return 0;
}
