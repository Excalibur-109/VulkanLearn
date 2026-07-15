#include "Component.hpp"

#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace ecs {
namespace {

struct ComponentGuidHash {
    [[nodiscard]] std::size_t operator()(ComponentGuid guid) const noexcept {
        std::size_t seed = std::hash<u64>{}(guid.high);
        seed ^= std::hash<u64>{}(guid.low) + static_cast<std::size_t>(0x9e3779b9U) +
                (seed << 6U) + (seed >> 2U);
        return seed;
    }
};

struct RegisteredType {
    ComponentTypeId id = INVALID_COMPONENT_TYPE;
    std::string debugName;
};

struct CentralComponentRegistry {
    std::mutex mutex;
    std::unordered_map<ComponentGuid, RegisteredType, ComponentGuidHash> types;
    ComponentTypeId nextId = 0;
};

CentralComponentRegistry& Registry() {
    static CentralComponentRegistry registry;
    return registry;
}

} // namespace

ComponentTypeId AcquireComponentTypeId(ComponentGuid guid, const char* debugName) {
    if (!guid.IsValid()) {
        throw std::invalid_argument("ECS component GUID cannot be zero");
    }

    CentralComponentRegistry& registry = Registry();
    std::lock_guard lock(registry.mutex);
    if (const auto iterator = registry.types.find(guid); iterator != registry.types.end()) {
        const std::string_view requestedName = debugName != nullptr ? debugName : "";
        if (!iterator->second.debugName.empty() && !requestedName.empty() &&
            iterator->second.debugName != requestedName) {
            throw std::logic_error("Two ECS component types use the same explicit GUID");
        }
        return iterator->second.id;
    }

    if (registry.nextId == INVALID_COMPONENT_TYPE) {
        throw std::overflow_error("The ECS component type id space is exhausted");
    }
    const ComponentTypeId id = registry.nextId++;
    registry.types.emplace(
        guid,
        RegisteredType{id, debugName != nullptr ? debugName : ""});
    return id;
}

std::size_t RegisteredComponentTypeCount() noexcept {
    CentralComponentRegistry& registry = Registry();
    std::lock_guard lock(registry.mutex);
    return registry.types.size();
}

} // namespace ecs
