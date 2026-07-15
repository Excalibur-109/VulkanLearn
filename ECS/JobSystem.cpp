#include "JobSystem.hpp"

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <vector>

namespace ecs {
namespace {

thread_local const void* currentJobSystem = nullptr;

class CurrentJobSystemScope {
public:
    explicit CurrentJobSystemScope(const void* jobSystem) noexcept
        : previous_(currentJobSystem) {
        currentJobSystem = jobSystem;
    }

    ~CurrentJobSystemScope() {
        currentJobSystem = previous_;
    }

private:
    const void* previous_ = nullptr;
};

} // namespace

struct JobSystem::Implementation {
    struct Task {
        std::size_t count     = 0;
        std::size_t grainSize = 1;
        void* context         = nullptr;
        TaskFunction function = nullptr;
        std::atomic<std::size_t> next{0};
        std::atomic<std::size_t> workersRemaining{0};
        std::atomic<bool> failed{false};
        std::mutex exceptionMutex;
        std::exception_ptr exception;
    };

    explicit Implementation(std::size_t workerCount) {
        workers.reserve(workerCount);
        try {
            for (std::size_t index = 0; index < workerCount; ++index) {
                workers.emplace_back([this] { WorkerLoop(); });
            }
        } catch (...) {
            {
                std::lock_guard lock(stateMutex);
                stopping = true;
                ++generation;
            }
            workCondition.notify_all();
            for (std::thread& worker : workers) {
                worker.join();
            }
            throw;
        }
    }

    ~Implementation() {
        {
            std::lock_guard lock(stateMutex);
            stopping = true;
            ++generation;
        }
        workCondition.notify_all();
        for (std::thread& worker : workers) {
            worker.join();
        }
    }

    void WorkerLoop() noexcept {
        std::size_t observedGeneration = 0;
        for (;;) {
            Task* task = nullptr;
            {
                std::unique_lock lock(stateMutex);
                workCondition.wait(lock, [&] {
                    return stopping || generation != observedGeneration;
                });
                if (stopping) {
                    return;
                }
                observedGeneration = generation;
                task               = activeTask;
            }

            CurrentJobSystemScope currentScope(this);
            Execute(*task);
            if (task->workersRemaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                completionCondition.notify_one();
            }
        }
    }

    static void Execute(Task& task) noexcept {
        try {
            while (!task.failed.load(std::memory_order_relaxed)) {
                const std::size_t begin =
                    task.next.fetch_add(task.grainSize, std::memory_order_relaxed);
                if (begin >= task.count) {
                    return;
                }
                const std::size_t end = std::min(begin + task.grainSize, task.count);
                for (std::size_t index = begin; index < end; ++index) {
                    task.function(task.context, index);
                }
            }
        } catch (...) {
            task.failed.store(true, std::memory_order_relaxed);
            std::lock_guard lock(task.exceptionMutex);
            if (task.exception == nullptr) {
                task.exception = std::current_exception();
            }
        }
    }

    std::vector<std::thread> workers;
    std::mutex submissionMutex;
    std::mutex stateMutex;
    std::condition_variable workCondition;
    std::condition_variable completionCondition;
    Task* activeTask       = nullptr;
    std::size_t generation = 0;
    bool stopping          = false;
};

JobSystem::JobSystem(std::size_t workerCount)
    : implementation_(std::make_unique<Implementation>(workerCount)) {
}

JobSystem::~JobSystem() = default;

void JobSystem::ParallelForErased(
    std::size_t count,
    std::size_t grainSize,
    void* context,
    TaskFunction function) {
    Implementation& implementation = *implementation_;
    if (implementation.workers.empty() || count <= grainSize ||
        currentJobSystem == std::addressof(implementation)) {
        for (std::size_t index = 0; index < count; ++index) {
            function(context, index);
        }
        return;
    }

    std::unique_lock submissionLock(implementation.submissionMutex);
    Implementation::Task task{};
    task.count            = count;
    task.grainSize        = grainSize;
    task.context          = context;
    task.function         = function;
    task.workersRemaining = implementation.workers.size();

    {
        std::lock_guard stateLock(implementation.stateMutex);
        implementation.activeTask = std::addressof(task);
        ++implementation.generation;
    }
    implementation.workCondition.notify_all();

    // 提交线程也参与工作，避免只等待 worker。
    {
        CurrentJobSystemScope currentScope(std::addressof(implementation));
        Implementation::Execute(task);
    }
    {
        std::unique_lock stateLock(implementation.stateMutex);
        implementation.completionCondition.wait(stateLock, [&] {
            return task.workersRemaining.load(std::memory_order_acquire) == 0;
        });
        implementation.activeTask = nullptr;
    }

    if (task.exception != nullptr) {
        std::rethrow_exception(task.exception);
    }
}

std::size_t JobSystem::WorkerCount() const noexcept {
    return implementation_->workers.size();
}

std::size_t JobSystem::DefaultWorkerCount() noexcept {
    const unsigned int hardwareThreads = std::thread::hardware_concurrency();
    if (hardwareThreads <= 1) {
        return 0;
    }
    return std::min<std::size_t>(hardwareThreads - 1U, 15U);
}

} // namespace ecs
