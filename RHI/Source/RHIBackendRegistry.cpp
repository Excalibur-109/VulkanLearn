#include "RHI/RHIBackend.hpp"

#include <mutex>

namespace RHI
{

RHIStatus RHIBackendRegistry::RegisterFactory(RHIBackendFactoryPtr factory)
{
    if (factory == nullptr)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "A backend factory is required.");
    }

    const RHIBackendInfo& info = factory->GetInfo();
    if (!info.Id.IsValid())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "The backend factory has an empty identifier.");
    }

    std::unique_lock lock(m_mutex);
    const auto [_, inserted] = m_factories.emplace(info.Id.Value, std::move(factory));
    if (!inserted)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "A backend factory is already registered with this identifier.");
    }

    return RHIStatus::Ok();
}

RHIStatus RHIBackendRegistry::UnregisterFactory(const std::string_view backendId)
{
    if (backendId.empty())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "A backend identifier is required.");
    }

    std::unique_lock lock(m_mutex);
    if (m_factories.erase(std::string(backendId)) == 0u)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "No backend factory is registered with this identifier.");
    }

    return RHIStatus::Ok();
}

std::vector<RHIBackendInfo> RHIBackendRegistry::Enumerate() const
{
    std::shared_lock lock(m_mutex);
    std::vector<RHIBackendInfo> result;
    result.reserve(m_factories.size());
    for (const auto& [_, factory] : m_factories)
    {
        result.push_back(factory->GetInfo());
    }
    return result;
}

RHIStatus RHIBackendRegistry::CreateBackend(const std::string_view backendId, RHIBackendPtr& outBackend) const
{
    if (backendId.empty())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "A backend identifier is required.");
    }

    RHIBackendFactoryPtr factory;
    {
        std::shared_lock lock(m_mutex);
        const auto iterator = m_factories.find(std::string(backendId));
        if (iterator == m_factories.end())
        {
            return RHIStatus::Error(RHIResult::Unsupported, "The requested backend is not registered.");
        }
        factory = iterator->second;
    }

    outBackend.reset();
    RHIStatus status = factory->CreateBackend(outBackend);
    if (status.Succeeded() && outBackend == nullptr)
    {
        return RHIStatus::Error(RHIResult::Failure, "The backend factory reported success without producing a backend.");
    }
    return status;
}

bool RHIBackendRegistry::Contains(const std::string_view backendId) const
{
    std::shared_lock lock(m_mutex);
    return m_factories.contains(std::string(backendId));
}

} // namespace RHI
