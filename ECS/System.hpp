#pragma once

#include "CommandBuffer.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ecs {

/**
 * @brief 处理一类游戏规则的长期对象。
 *
 * System 不拥有 Entity 或 Component。它在 Update 中创建/复用 Query，读取匹配组件，
 * 并把结构变化写入 CommandBuffer。order 越小越早执行。
 */
class System {
public:
    explicit System(std::string name, int order = 0)
        : name_(std::move(name)), order_(order) {
    }

    virtual ~System() = default;

    [[nodiscard]] std::string_view Name() const noexcept {
        return name_;
    }

    [[nodiscard]] int Order() const noexcept {
        return order_;
    }

    void SetOrder(int order) noexcept {
        order_ = order;
    }

    [[nodiscard]] bool Enabled() const noexcept {
        return enabled_;
    }

    void SetEnabled(bool enabled) noexcept {
        enabled_ = enabled;
    }

protected:
    virtual void OnCreate(World&) {}
    virtual void OnUpdate(World& world, CommandBuffer& commands, float deltaTime) = 0;
    virtual void OnDestroy(World&) noexcept {}

private:
    friend class SystemScheduler;

    std::string name_;
    int order_   = 0;
    bool enabled_ = true;
};

/**
 * @brief 拥有并顺序调度 System。
 *
 * 同 order 时保持注册顺序。每个 System 返回后立即回放它的 CommandBuffer，因此后面的
 * System 可以看见前一个 System 创建、删除或迁移的实体。
 */
class SystemScheduler {
public:
    explicit SystemScheduler(World& world) noexcept
        : world_(std::addressof(world)) {
    }

    ~SystemScheduler();

    SystemScheduler(const SystemScheduler&)            = delete;
    SystemScheduler& operator=(const SystemScheduler&) = delete;

    template <typename SystemType, typename... Arguments>
    SystemType& AddSystem(Arguments&&... arguments) {
        static_assert(std::is_base_of_v<System, SystemType>);
        auto system = std::make_unique<SystemType>(std::forward<Arguments>(arguments)...);
        SystemType* result = system.get();

        // OnCreate 成功后才移交所有权，异常时不会留下半注册 System。
        system->OnCreate(*world_);
        systems_.push_back(Entry{std::move(system), nextSequence_++});
        sortDirty_ = true;
        return *result;
    }

    template <typename SystemType>
    [[nodiscard]] SystemType* GetSystem() noexcept {
        static_assert(std::is_base_of_v<System, SystemType>);
        for (Entry& entry : systems_) {
            if (auto* result = dynamic_cast<SystemType*>(entry.system.get()); result != nullptr) {
                return result;
            }
        }
        return nullptr;
    }

    template <typename SystemType>
    bool RemoveSystem() {
        static_assert(std::is_base_of_v<System, SystemType>);
        const auto iterator = std::find_if(systems_.begin(), systems_.end(), [](const Entry& entry) {
            return dynamic_cast<SystemType*>(entry.system.get()) != nullptr;
        });
        if (iterator == systems_.end()) {
            return false;
        }

        iterator->system->OnDestroy(*world_);
        systems_.erase(iterator);
        return true;
    }

    void Update(float deltaTime);
    void Clear() noexcept;

    [[nodiscard]] std::size_t Size() const noexcept {
        return systems_.size();
    }

private:
    struct Entry {
        std::unique_ptr<System> system;
        u64 sequence = 0;
    };

    void SortIfNeeded();

    World* world_ = nullptr;
    std::vector<Entry> systems_;
    CommandBuffer commandBuffer_;
    u64 nextSequence_ = 0;
    bool sortDirty_    = false;
};

} // namespace ecs

