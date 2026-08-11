#pragma once

#include "RHI/RHIBackend.hpp"

#include <mutex>

namespace RHI
{

/** Defines behavior applied to the currently active device during a backend switch. */
enum class RHIBackendSwitchPolicy : std::uint8_t
{
    /** Reject switching while an active device exists. */
    RejectIfActive,
    /** Wait for the active device to finish all submitted work before replacing it. */
    WaitForIdle,
};

/** Describes initial RHI context creation. */
struct RHIContextDesc final
{
    /** Backend identifier selected at startup. */
    RHIBackendId Backend;
    /** Device selection and feature requirements. */
    RHIDeviceDesc Device{};
};

/** Describes one runtime backend replacement request. */
struct RHIBackendSwitchDesc final
{
    /** Backend identifier selected after the switch succeeds. */
    RHIBackendId Backend;
    /** Device selection and feature requirements for the replacement device. */
    RHIDeviceDesc Device{};
    /** Required handling of the currently active device. */
    RHIBackendSwitchPolicy Policy = RHIBackendSwitchPolicy::WaitForIdle;
};

/** Snapshots the currently active backend, device, and invalidation generation. */
struct RHIActiveDevice final
{
    /** Backend selected for this device. */
    RHIBackendInfo Backend{};
    /** Shared ownership of the active device; retaining this pointer also retains its backend implementation. */
    RHIDevicePtr Device;
    /** Monotonically increasing generation assigned after every successful replacement. */
    std::uint64_t Generation = 0;

    /** Returns true when this snapshot owns an initialized device. */
    [[nodiscard]] bool IsValid() const noexcept { return Device != nullptr; }
};

/** Coordinates a registered backend and makes runtime backend replacement transactional. */
class RHI_API RHIContext final
{
public:
    /** Constructs an uninitialized context backed by a non-owning registry reference. */
    explicit RHIContext(RHIBackendRegistry& registry) noexcept;

    /** Waits for active work where possible, then releases the active backend and device. */
    ~RHIContext();

    /** Prevents accidental copying of context-owned backend state. */
    RHIContext(const RHIContext&) = delete;

    /** Prevents accidental copying of context-owned backend state. */
    RHIContext& operator=(const RHIContext&) = delete;

    /** Creates the initial backend and device for this context. */
    RHIStatus Initialize(const RHIContextDesc& desc);

    /** Creates a replacement backend/device and atomically exposes it after successful initialization. */
    RHIStatus SwitchBackend(const RHIBackendSwitchDesc& desc);

    /** Returns a thread-safe snapshot of the active device. */
    [[nodiscard]] RHIActiveDevice GetActiveDevice() const;

    /** Returns true when this context currently owns an initialized device. */
    [[nodiscard]] bool IsInitialized() const;

    /** Waits for submitted work and releases the active backend and device. */
    RHIStatus Shutdown();

private:
    /** Creates a backend and device without mutating the currently active state. */
    RHIStatus CreateCandidate(const RHIBackendId& backendId, const RHIDeviceDesc& deviceDesc, RHIBackendPtr& outBackend, RHIDevicePtr& outDevice, RHIBackendInfo& outInfo) const;

    /** Keeps a device and the backend that implements it alive as one shared lifetime. */
    struct ActiveState final
    {
        /** Loaded backend implementing Device. */
        RHIBackendPtr Backend;
        /** Device created by Backend. */
        RHIDevicePtr Device;
        /** Immutable description of Backend. */
        RHIBackendInfo BackendInfo;
        /** Generation associated with this active device. */
        std::uint64_t Generation = 0;
    };

    /** Registry used to look up external backend providers. */
    RHIBackendRegistry& m_registry;
    /** Serializes initialization, switching, and shutdown. */
    mutable std::mutex m_transitionMutex;
    /** Protects active state snapshots. */
    mutable std::shared_mutex m_stateMutex;
    /** Active device/backend lifetime, or null before initialization and after shutdown. */
    std::shared_ptr<ActiveState> m_active;
    /** Current invalidation generation. */
    std::uint64_t m_generation = 0;
};

} // namespace RHI
