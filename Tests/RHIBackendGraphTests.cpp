#include "RHI.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

rhi::RHIFramePacket MakeClearFrame(rhi::u64 frameIndex) {
    rhi::RHIFramePacket packet{};
    packet.settings.frameIndex = frameIndex;
    packet.settings.drawableSize = {64, 64};
    packet.settings.viewport = {0.0F, 0.0F, 64.0F, 64.0F, 0.0F, 1.0F};
    packet.settings.scissor = {{0, 0}, {64, 64}};

    rhi::RHIRenderGraphTextureDesc color{};
    color.name = "TransientColor";
    color.desc.debugName = "BackendGraphTest.TransientColor";
    color.desc.extent = {64, 64, 1};
    color.desc.format = rhi::RHIFormat::RGBA8_UNorm;
    color.desc.usage = rhi::RHITextureUsage::ColorAttachment |
                       rhi::RHITextureUsage::Sampled;
    color.desc.lifetime = rhi::RHIResourceLifetime::Transient;
    color.flags = rhi::RHIRenderGraphResourceFlags::Transient |
                  rhi::RHIRenderGraphResourceFlags::AllowAliasing;
    packet.graph.textures.push_back(color);

    color.name = "TransientColorAfter";
    color.desc.debugName = "BackendGraphTest.TransientColorAfter";
    packet.graph.textures.push_back(color);

    rhi::RHIRenderGraphPassDesc pass{};
    pass.name = "ClearTransientColor";
    pass.type = rhi::RHIRenderGraphPassType::Raster;
    pass.queue = rhi::RHIQueueType::Graphics;
    pass.hasSideEffect = true;
    rhi::RHIRenderGraphAttachmentDesc attachment{};
    attachment.resourceName = "TransientColor";
    attachment.loadOp = rhi::RHILoadOp::Clear;
    attachment.storeOp = rhi::RHIStoreOp::Store;
    attachment.clearValue.color = {0.1F, 0.2F, 0.3F, 1.0F};
    pass.colorAttachments.push_back(attachment);
    packet.graph.passes.push_back(pass);

    pass.name = "ClearTransientColorAfter";
    pass.colorAttachments[0].resourceName = "TransientColorAfter";
    pass.colorAttachments[0].clearValue.color = {
        frameIndex == 0 ? 0.8F : 0.6F,
        0.1F,
        0.2F,
        1.0F};
    packet.graph.passes.push_back(pass);
    return packet;
}

void RunBackend(rhi::RHIGraphicsAPI api) {
    rhi::RHIDeviceCreateDesc desc{};
    desc.backend.applicationName = "RHIBackendGraphTests";
    desc.backend.preferredApi = api;
    desc.backend.validation = rhi::RHIValidationMode::Disabled;
    desc.backend.framesInFlight = 2;
    desc.allowSoftwareAdapter = true;

    std::string error;
    std::unique_ptr<rhi::RHIDevice> device =
        rhi::CreateInitializedRHIDevice(desc, &error);
    if (device == nullptr) {
        throw std::runtime_error(
            std::string("Failed to initialize ") +
            (api == rhi::RHIGraphicsAPI::Vulkan
                 ? "Vulkan"
                 : (api == rhi::RHIGraphicsAPI::Direct3D11 ? "D3D11" : "D3D12")) +
            ": " + error);
    }

    for (rhi::u64 frameIndex = 0; frameIndex < 2; ++frameIndex) {
        const rhi::RHIFramePacket packet = MakeClearFrame(frameIndex);
        if (!device->SubmitFrame(packet, &error)) {
            throw std::runtime_error(
                std::string(device->BackendName()) +
                " RenderGraph frame failed: " + error);
        }
    }

    rhi::RHIBufferDesc sourceDesc{};
    sourceDesc.debugName = "BackendGraphTest.CopySource";
    sourceDesc.size = 16;
    sourceDesc.usage = rhi::RHIBufferUsage::TransferSource |
                       rhi::RHIBufferUsage::TransferDestination;
    const rhi::RHIBuffer source = device->CreateBuffer(sourceDesc);

    rhi::RHIBufferDesc destinationDesc = sourceDesc;
    destinationDesc.debugName = "BackendGraphTest.CopyDestination";
    const rhi::RHIBuffer destination = device->CreateBuffer(destinationDesc);

    rhi::RHIFramePacket copyPacket{};
    rhi::RHIBufferUploadDesc upload{};
    upload.destination = source;
    upload.data.resize(16, std::byte{0x5A});
    copyPacket.uploads.buffers.push_back(std::move(upload));

    const auto importBuffer = [&](const char* name, rhi::RHIBuffer handle) {
        rhi::RHIRenderGraphBufferDesc resource{};
        resource.name = name;
        resource.desc = sourceDesc;
        resource.imported = true;
        resource.flags = rhi::RHIRenderGraphResourceFlags::Imported;
        resource.externalHandle = handle;
        copyPacket.graph.buffers.push_back(resource);
    };
    importBuffer("CopySource", source);
    importBuffer("CopyDestination", destination);

    rhi::RHIRenderGraphPassDesc copyPass{};
    copyPass.name = "CopyBuffer";
    copyPass.type = rhi::RHIRenderGraphPassType::Copy;
    copyPass.hasSideEffect = true;
    copyPass.reads.push_back({
        "CopySource",
        rhi::RHIRenderGraphResourceType::Buffer,
        rhi::RHIResourceState::CopySource,
        rhi::RHIPipelineStage::Transfer});
    copyPass.writes.push_back({
        "CopyDestination",
        rhi::RHIRenderGraphResourceType::Buffer,
        rhi::RHIResourceState::CopyDestination,
        rhi::RHIPipelineStage::Transfer});
    copyPacket.graph.passes.push_back(copyPass);

    rhi::RHIRenderPassWorkload copyWorkload{};
    copyWorkload.passName = "CopyBuffer";
    copyWorkload.bufferCopies.push_back({source, destination, 0, 0, 16});
    copyPacket.workloads.push_back(copyWorkload);
    if (!device->SubmitFrame(copyPacket, &error)) {
        throw std::runtime_error(
            std::string(device->BackendName()) +
            " RenderGraph buffer copy failed: " + error);
    }

    device->WaitIdle();
    device->Destroy(destination);
    device->Destroy(source);
}

} // namespace

int main() {
    try {
        RunBackend(rhi::RHIGraphicsAPI::Vulkan);
#if defined(_WIN32)
        RunBackend(rhi::RHIGraphicsAPI::Direct3D11);
        RunBackend(rhi::RHIGraphicsAPI::Direct3D12);
#endif
        std::cout << "All available RHI backends executed RenderGraph frames.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "RHI backend RenderGraph test failed: " << exception.what() << '\n';
        return 1;
    }
}
