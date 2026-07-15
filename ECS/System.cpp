#include "System.hpp"

namespace ecs {

SystemScheduler::~SystemScheduler() = default;

void System::Reset() noexcept {
    if (object_ == nullptr) {
        return;
    }
    if (onDestroy_ != nullptr) {
        onDestroy_(object_, *world_);
    }
    destroyObject_(object_);
    FreeRawMemory(object_, alignment_);
    object_ = nullptr;
}

void System::MoveFrom(System& other) noexcept {
    world_         = std::exchange(other.world_, nullptr);
    object_        = std::exchange(other.object_, nullptr);
    alignment_     = other.alignment_;
    type_          = other.type_;
    name_          = std::move(other.name_);
    order_         = other.order_;
    enabled_       = other.enabled_;
    update_        = other.update_;
    onDestroy_     = other.onDestroy_;
    destroyObject_ = other.destroyObject_;
}

void SystemScheduler::Update(float deltaTime) {
    SortIfNeeded();
    for (Entry& entry : systems_) {
        if (!entry.system.Enabled()) {
            continue;
        }
        commandBuffer_.Clear();
        try {
            entry.system.Update(*world_, commandBuffer_, deltaTime);
            commandBuffer_.Playback(*world_);
        } catch (...) {
            commandBuffer_.Clear();
            throw;
        }
    }
}

void SystemScheduler::SortIfNeeded() {
    const auto less = [](const Entry& lhs, const Entry& rhs) {
        if (lhs.system.Order() != rhs.system.Order()) {
            return lhs.system.Order() < rhs.system.Order();
        }
        return lhs.sequence < rhs.sequence;
    };
    if (sortDirty_ || !std::is_sorted(systems_.begin(), systems_.end(), less)) {
        std::sort(systems_.begin(), systems_.end(), less);
        sortDirty_ = false;
    }
}

} // namespace ecs

