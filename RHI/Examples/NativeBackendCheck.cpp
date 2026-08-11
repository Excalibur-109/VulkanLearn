#include "RHI/Backends/RHINativeBackends.hpp"

int main()
{
    RHI::RHIBackendRegistry registry;
    if (!RHI::RegisterD3D11Backend(registry).Succeeded() ||
        !RHI::RegisterD3D12Backend(registry).Succeeded() ||
        !RHI::RegisterVulkanBackend(registry).Succeeded())
    {
        return 1;
    }

    for (const RHI::RHIBackendInfo& info : registry.Enumerate())
    {
        RHI::RHIBackendPtr backend;
        if (!registry.CreateBackend(info.Id.Value, backend).Succeeded())
        {
            return 2;
        }
        std::vector<RHI::RHIAdapterInfo> adapters;
        if (!backend->EnumerateAdapters(adapters).Succeeded())
        {
            return 3;
        }
    }
    return 0;
}
