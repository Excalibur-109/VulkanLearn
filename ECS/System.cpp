#include "System.hpp"

namespace ecs {

SystemScheduler::~SystemScheduler() {
    Clear();
}

void SystemScheduler::Update(float deltaTime) {
    SortIfNeeded();
    for (Entry& entry : systems_) {
        if (!entry.system->Enabled()) {
            continue;
        }

        commandBuffer_.Clear();
        try {
            entry.system->OnUpdate(*world_, commandBuffer_, deltaTime);
            commandBuffer_.Playback(*world_);
        } catch (...) {
            commandBuffer_.Clear();
            throw;
        }
    }
}

void SystemScheduler::Clear() noexcept {
    commandBuffer_.Clear();
    for (auto iterator = systems_.rbegin(); iterator != systems_.rend(); ++iterator) {
        iterator->system->OnDestroy(*world_);
    }
    systems_.clear();
}

void SystemScheduler::SortIfNeeded() {
    // order 可能由外部通过 SetOrder 修改，因此每帧都验证是否已经有序。
    const auto less = [](const Entry& lhs, const Entry& rhs) {
        if (lhs.system->Order() != rhs.system->Order()) {
            return lhs.system->Order() < rhs.system->Order();
        }
        return lhs.sequence < rhs.sequence;
    };

    if (sortDirty_ || !std::is_sorted(systems_.begin(), systems_.end(), less)) {
        std::sort(systems_.begin(), systems_.end(), less);
        sortDirty_ = false;
    }
}

} // namespace ecs

