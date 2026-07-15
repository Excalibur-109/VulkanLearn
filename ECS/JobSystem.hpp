#pragma once

#include "ECSApi.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <thread>
#include <type_traits>

namespace ecs {

/**
 * 固定工作线程池。一次 ParallelFor 只发布一个类型擦除任务，worker 通过原子索引抢批次，
 * 不为每个 job 分配对象。调用同步结束，适合 Query 按 Chunk 并行。
 */
class JobSystem {
public:
    ECS_API explicit JobSystem(std::size_t workerCount = DefaultWorkerCount());
    ECS_API ~JobSystem();

    JobSystem(const JobSystem&)            = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&)                 = delete;
    JobSystem& operator=(JobSystem&&)      = delete;

    template <typename Function>
    void ParallelFor(std::size_t count, std::size_t grainSize, Function&& function) {
        if (count == 0) {
            return;
        }
        grainSize = std::max<std::size_t>(grainSize, 1U);
        using Type = std::remove_reference_t<Function>;
        ParallelForErased(
            count,
            grainSize,
            std::addressof(function),
            [](void* context, std::size_t index) {
                std::invoke(*static_cast<Type*>(context), index);
            });
    }

    [[nodiscard]] ECS_API std::size_t WorkerCount() const noexcept;
    [[nodiscard]] static ECS_API std::size_t DefaultWorkerCount() noexcept;

private:
    using TaskFunction = void (*)(void* context, std::size_t index);
    struct Implementation;

    ECS_API void ParallelForErased(
        std::size_t count,
        std::size_t grainSize,
        void* context,
        TaskFunction function);

    std::unique_ptr<Implementation> implementation_;
};

} // namespace ecs
