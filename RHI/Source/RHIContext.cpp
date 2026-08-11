#include "RHI/RHIContext.hpp"

#include <mutex>

namespace RHI
{

RHIContext::RHIContext(RHIBackendRegistry& registry) noexcept
    : m_registry(registry)
{
}

RHIContext::~RHIContext()
{
    (void)Shutdown();
}

RHIStatus RHIContext::CreateCandidate(const RHIBackendId& backendId, const RHIDeviceDesc& deviceDesc, RHIBackendPtr& outBackend, RHIDevicePtr& outDevice, RHIBackendInfo& outInfo) const
{
    if (!backendId.IsValid())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "A non-empty backend identifier is required.");
    }

    outBackend.reset();
    outDevice.reset();
    RHIStatus status = m_registry.CreateBackend(backendId.Value, outBackend);
    if (!status.Succeeded())
    {
        return status;
    }

    outInfo = outBackend->GetInfo();
    if (outInfo.Id != backendId)
    {
        outBackend.reset();
        return RHIStatus::Error(RHIResult::Failure, "The backend identifier differs from the registered factory identifier.");
    }

    status = outBackend->CreateDevice(deviceDesc, outDevice);
    if (!status.Succeeded())
    {
        outBackend.reset();
        return status;
    }
    if (outDevice == nullptr)
    {
        outBackend.reset();
        return RHIStatus::Error(RHIResult::Failure, "The backend reported successful device creation without producing a device.");
    }

    return RHIStatus::Ok();
}

RHIStatus RHIContext::Initialize(const RHIContextDesc& desc)
{
    std::lock_guard transitionLock(m_transitionMutex);
    {
        std::shared_lock stateLock(m_stateMutex);
        if (m_active != nullptr)
        {
            return RHIStatus::Error(RHIResult::InvalidArgument, "The RHI context is already initialized.");
        }
    }

    RHIBackendPtr candidateBackend;
    RHIDevicePtr candidateDevice;
    RHIBackendInfo candidateInfo;
    RHIStatus status = CreateCandidate(desc.Backend, desc.Device, candidateBackend, candidateDevice, candidateInfo);
    if (!status.Succeeded())
    {
        return status;
    }

    auto active = std::make_shared<ActiveState>();
    active->Backend = std::move(candidateBackend);
    active->Device = std::move(candidateDevice);
    active->BackendInfo = std::move(candidateInfo);
    std::unique_lock stateLock(m_stateMutex);
    active->Generation = ++m_generation;
    m_active = std::move(active);
    return RHIStatus::Ok();
}

RHIStatus RHIContext::SwitchBackend(const RHIBackendSwitchDesc& desc)
{
    std::lock_guard transitionLock(m_transitionMutex);

    if (desc.Policy == RHIBackendSwitchPolicy::RejectIfActive)
    {
        std::shared_lock stateLock(m_stateMutex);
        if (m_active != nullptr)
        {
            return RHIStatus::Error(RHIResult::NotReady, "An active device prevents this backend switch policy.");
        }
    }

    RHIBackendPtr candidateBackend;
    RHIDevicePtr candidateDevice;
    RHIBackendInfo candidateInfo;
    RHIStatus status = CreateCandidate(desc.Backend, desc.Device, candidateBackend, candidateDevice, candidateInfo);
    if (!status.Succeeded())
    {
        return status;
    }

    std::shared_ptr<ActiveState> previousActive;
    {
        std::shared_lock stateLock(m_stateMutex);
        previousActive = m_active;
    }

    if (previousActive != nullptr)
    {
        status = previousActive->Device->WaitIdle();
        if (!status.Succeeded())
        {
            return status;
        }
    }

    auto active = std::make_shared<ActiveState>();
    active->Backend = std::move(candidateBackend);
    active->Device = std::move(candidateDevice);
    active->BackendInfo = std::move(candidateInfo);
    std::unique_lock stateLock(m_stateMutex);
    active->Generation = ++m_generation;
    m_active = std::move(active);
    return RHIStatus::Ok();
}

RHIActiveDevice RHIContext::GetActiveDevice() const
{
    std::shared_lock stateLock(m_stateMutex);
    if (m_active == nullptr)
    {
        return {};
    }
    return {m_active->BackendInfo, RHIDevicePtr(m_active, m_active->Device.get()), m_active->Generation};
}

bool RHIContext::IsInitialized() const
{
    std::shared_lock stateLock(m_stateMutex);
    return m_active != nullptr;
}

RHIStatus RHIContext::Shutdown()
{
    std::lock_guard transitionLock(m_transitionMutex);
    std::shared_ptr<ActiveState> previousActive;
    {
        std::unique_lock stateLock(m_stateMutex);
        previousActive = std::move(m_active);
        if (previousActive != nullptr)
        {
            ++m_generation;
        }
    }

    if (previousActive != nullptr)
    {
        return previousActive->Device->WaitIdle();
    }
    return RHIStatus::Ok();
}

} // namespace RHI
