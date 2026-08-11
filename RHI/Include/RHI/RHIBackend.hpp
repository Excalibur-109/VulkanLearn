#pragma once

#include "RHI/RHIDevice.hpp"

#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace RHI
{

/** Describes one registered RHI backend implementation. */
struct RHIBackendInfo final
{
    /** Stable identifier used for lookup and runtime switching. */
    RHIBackendId Id;
    /** Human-readable backend display name. */
    std::string DisplayName;
    /** Backend implementation version. */
    RHIVersion Version{};
};

/** Represents a loaded backend capable of selecting adapters and creating devices. */
class IRHIBackend
{
public:
    /** Releases the loaded backend implementation. */
    virtual ~IRHIBackend() = default;

    /** Returns immutable information about this backend implementation. */
    [[nodiscard]] virtual const RHIBackendInfo& GetInfo() const noexcept = 0;

    /** Enumerates adapters usable by this backend. */
    virtual RHIStatus EnumerateAdapters(std::vector<RHIAdapterInfo>& outAdapters) = 0;

    /** Creates a device using the requested adapter and features. */
    virtual RHIStatus CreateDevice(const RHIDeviceDesc& desc, RHIDevicePtr& outDevice) = 0;
};

/** Owns a loaded backend implementation and may keep it alive across a runtime switch. */
using RHIBackendPtr = std::shared_ptr<IRHIBackend>;

/** Creates independent backend instances for the backend registry. */
class IRHIBackendFactory
{
public:
    /** Releases the backend factory implementation. */
    virtual ~IRHIBackendFactory() = default;

    /** Returns immutable information about backend instances created by this factory. */
    [[nodiscard]] virtual const RHIBackendInfo& GetInfo() const noexcept = 0;

    /** Creates one independent backend instance. */
    virtual RHIStatus CreateBackend(RHIBackendPtr& outBackend) = 0;
};

/** Owns a backend factory implementation. */
using RHIBackendFactoryPtr = std::shared_ptr<IRHIBackendFactory>;

/** Thread-safe registry of application-provided backend factories. */
class RHI_API RHIBackendRegistry final
{
public:
    /** Registers a backend factory under its stable backend identifier. */
    RHIStatus RegisterFactory(RHIBackendFactoryPtr factory);

    /** Removes a backend factory; existing backend and device instances remain valid. */
    RHIStatus UnregisterFactory(std::string_view backendId);

    /** Returns descriptions of all currently registered backend factories. */
    [[nodiscard]] std::vector<RHIBackendInfo> Enumerate() const;

    /** Creates a loaded backend from a registered factory. */
    RHIStatus CreateBackend(std::string_view backendId, RHIBackendPtr& outBackend) const;

    /** Returns true when a backend identifier is currently registered. */
    [[nodiscard]] bool Contains(std::string_view backendId) const;

private:
    /** Protects the factory table against concurrent reads and writes. */
    mutable std::shared_mutex m_mutex;
    /** Factories indexed by their stable identifier. */
    std::unordered_map<std::string, RHIBackendFactoryPtr> m_factories;
};

} // namespace RHI
