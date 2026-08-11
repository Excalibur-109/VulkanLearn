#include "RHI/RHI.hpp"

#include <cstring>
#include <fstream>
#include <iostream>

namespace
{

/** 从构建生成目录读取 SPIR-V 二进制文件。 */
std::vector<std::byte> ReadBinaryFile(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return {};
    const std::streamsize size = input.tellg();
    if (size <= 0) return {};
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    return bytes;
}

/** 为指定后端构造匹配的 PBR 着色器集合。 */
bool CreateShaderSet(const std::string_view backendId, RHI::RHIPBRShaderSet& outShaders)
{
    if (backendId != "vulkan")
    {
        outShaders.Vertex = RHI::CreateRHIPBRHlslShaderDesc("PBR.Vertex", "PBRVertexMain");
        outShaders.Fragment = RHI::CreateRHIPBRHlslShaderDesc("PBR.Fragment", "PBRFragmentMain");
        return true;
    }

    outShaders.Vertex = {"PBR.Vertex.SPIRV", RHI::RHIShaderCodeFormat::IntermediateBinary, ReadBinaryFile(std::string(RHI_PBR_SPIRV_DIRECTORY) + "/RHIPBR.vert.spv"), "main"};
    outShaders.Fragment = {"PBR.Fragment.SPIRV", RHI::RHIShaderCodeFormat::IntermediateBinary, ReadBinaryFile(std::string(RHI_PBR_SPIRV_DIRECTORY) + "/RHIPBR.frag.spv"), "main"};
    return !outShaders.Vertex.Code.empty() && !outShaders.Fragment.Code.empty();
}

/** 返回跨 API 均能解释的单位矩阵。 */
RHI::RHIPBRMatrix4x4 IdentityMatrix()
{
    RHI::RHIPBRMatrix4x4 matrix;
    matrix.Values = {1.0f, 0.0f, 0.0f, 0.0f,
                     0.0f, 1.0f, 0.0f, 0.0f,
                     0.0f, 0.0f, 1.0f, 0.0f,
                     0.0f, 0.0f, 0.0f, 1.0f};
    return matrix;
}

/** 在一个后端上执行完整的离屏 PBR 绘制。 */
bool RunBackend(RHI::RHIBackendRegistry& registry, const std::string_view backendId)
{
    RHI::RHIBackendPtr backend;
    RHI::RHIStatus status = registry.CreateBackend(backendId, backend);
    if (!status.Succeeded()) { std::cerr << backendId << ": " << status.Message << '\n'; return false; }
    RHI::RHIDevicePtr device;
    status = backend->CreateDevice({}, device);
    if (!status.Succeeded()) { std::cerr << backendId << ": " << status.Message << '\n'; return false; }

    RHI::RHITextureHandle target;
    RHI::RHITextureViewHandle targetView;
    RHI::RHITextureDesc textureDesc;
    textureDesc.DebugName = "PBR.Offscreen";
    textureDesc.Extent = {256, 256, 1};
    textureDesc.Format = RHI::RHIFormat::RGBA8_UNorm;
    textureDesc.Usage = RHI::RHITextureUsage::ColorAttachment;
    if (!(status = device->CreateTexture(textureDesc, target)).Succeeded() || !(status = device->CreateTextureView({"PBR.Offscreen.View", target}, targetView)).Succeeded())
    {
        std::cerr << backendId << ": " << status.Message << '\n';
        if (target.IsValid()) device->DestroyTexture(target);
        return false;
    }

    RHI::RHIPBRShaderSet shaders;
    if (!CreateShaderSet(backendId, shaders))
    {
        std::cerr << backendId << ": 无法读取 PBR 着色器。\n";
        device->DestroyTextureView(targetView); device->DestroyTexture(target); return false;
    }
    std::unique_ptr<RHI::RHIPBRRenderer> renderer;
    RHI::RHIPBRRendererDesc rendererDesc;
    rendererDesc.Shaders = std::move(shaders);
    rendererDesc.ColorFormat = RHI::RHIFormat::RGBA8_UNorm;
    rendererDesc.LatitudeSegments = 16;
    rendererDesc.LongitudeSegments = 32;
    status = RHI::RHIPBRRenderer::Create(device, rendererDesc, renderer);
    if (status.Succeeded())
    {
        RHI::RHIPBRDrawConstants constants;
        constants.ViewProjection = IdentityMatrix();
        constants.CameraPosition = {0.0f, 0.0f, 3.0f, 0.0f};
        constants.BaseColorMetallic = {0.72f, 0.19f, 0.08f, 0.65f};
        constants.RoughnessOcclusionIntensity = {0.3f, 1.0f, 4.0f, 0.0f};
        constants.LightDirection = {0.4f, 0.8f, 0.5f, 0.0f};
        status = renderer->Render({targetView, {256, 256}, constants});
    }
    if (status.Succeeded()) status = device->WaitIdle();
    if (!status.Succeeded()) std::cerr << backendId << ": " << status.Message << '\n';
    renderer.reset();
    device->DestroyTextureView(targetView);
    device->DestroyTexture(target);
    return status.Succeeded();
}

} // namespace

int main()
{
    RHI::RHIBackendRegistry registry;
    if (!RHI::RegisterD3D11Backend(registry).Succeeded() || !RHI::RegisterD3D12Backend(registry).Succeeded() || !RHI::RegisterVulkanBackend(registry).Succeeded()) return 1;
    return RunBackend(registry, "d3d11") && RunBackend(registry, "d3d12") && RunBackend(registry, "vulkan") ? 0 : 2;
}
