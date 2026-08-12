#include "PbrMath.h"
#include "PlaneMesh.h"
#include "SoftwareRenderer.h"
#include "SphereMesh.h"
#include "Win32Window.h"

#include "rhi/RHI.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

rhi::Backend parseBackend(int argc, char** argv) {
    if (argc < 2) return rhi::Backend::Software;
    const std::string name = argv[1];
    if (name == "vulkan") return rhi::Backend::Vulkan;
    if (name == "d3d11") return rhi::Backend::D3D11;
    if (name == "d3d12") return rhi::Backend::D3D12;
    return rhi::Backend::Software;
}

bool hasNoWaitFlag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-wait") return true;
    }
    return false;
}

// 创建一次索引式 PBR 绘制所需的全部 RHI 对象。
// 应用层只持有抽象句柄，后端资源分配隐藏在 Device::create* 方法之后。
int runDemo(int argc, char** argv) {
    rhi::DeviceCreateInfo createInfo;
    createInfo.backend = parseBackend(argc, argv);
    createInfo.extent = {1280, 720};

    auto device = rhi::createDevice(createInfo);
    std::cout << "RHI PBR demo: " << device->name() << '\n';

    std::vector<pbrdemo::SphereVertex> vertices;
    std::vector<uint32_t> indices;
    pbrdemo::makeSphere(64, 32, vertices, indices);
    std::vector<pbrdemo::SphereVertex> groundVertices;
    std::vector<uint32_t> groundIndices;
    pbrdemo::makeGroundPlane(5.0f, groundVertices, groundIndices);

    rhi::BufferDesc vertexDesc{
        vertices.size() * sizeof(pbrdemo::SphereVertex),
        rhi::ResourceUsage::Vertex, false, "PBR sphere vertices"};
    auto vertexBuffer = device->createBuffer(vertexDesc, vertices.data());

    rhi::BufferDesc indexDesc{
        indices.size() * sizeof(uint32_t),
        rhi::ResourceUsage::Index, false, "PBR sphere indices"};
    auto indexBuffer = device->createBuffer(indexDesc, indices.data());
    rhi::BufferDesc groundVertexDesc{
        groundVertices.size() * sizeof(pbrdemo::SphereVertex),
        rhi::ResourceUsage::Vertex, false, "Ground plane vertices"};
    auto groundVertexBuffer = device->createBuffer(groundVertexDesc, groundVertices.data());
    rhi::BufferDesc groundIndexDesc{
        groundIndices.size() * sizeof(uint32_t), rhi::ResourceUsage::Index,
        false, "Ground plane indices"};
    auto groundIndexBuffer = device->createBuffer(groundIndexDesc, groundIndices.data());

    pbrdemo::PbrMaterial material;
    pbrdemo::Camera camera;
    rhi::BufferDesc uniformDesc{
        sizeof(material) + sizeof(camera), rhi::ResourceUsage::Uniform,
        true, "PBR frame constants"};
    auto uniformBuffer = device->createBuffer(uniformDesc, &material);

    rhi::ShaderDesc vertexShaderDesc{
        rhi::ShaderStage::Vertex, "main", {}, "// PBR 顶点阶段", "PBR vertex"};
    rhi::ShaderDesc fragmentShaderDesc{
        rhi::ShaderStage::Fragment, "main", {}, "// Cook-Torrance GGX 片元阶段", "PBR fragment"};
    auto vertexShader = device->createShader(vertexShaderDesc);
    auto fragmentShader = device->createShader(fragmentShaderDesc);

    rhi::PipelineDesc pipelineDesc;
    pipelineDesc.vertexShader = std::move(vertexShader);
    pipelineDesc.fragmentShader = std::move(fragmentShader);
    pipelineDesc.attributes = {
        {0, 0,  rhi::Format::RGBA32_Float},
        {1, 12, rhi::Format::RGBA32_Float},
        {2, 24, rhi::Format::RGBA32_Float}};
    pipelineDesc.bindings = {
        {0, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Vertex},
        {1, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment},
        {2, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment}};
    pipelineDesc.debugName = "PBR sphere pipeline";
    auto pipeline = device->createPipeline(pipelineDesc);
    auto sampler = device->createSampler({true, true, 8.0f});
    (void)sampler;

    rhi::TextureDesc colorDesc;
    colorDesc.extent = createInfo.extent;
    colorDesc.format = rhi::Format::BGRA8_UNorm;
    colorDesc.usage = rhi::ResourceUsage::RenderTarget | rhi::ResourceUsage::Texture;
    auto colorTarget = device->createTexture(colorDesc);

    rhi::TextureDesc depthDesc = colorDesc;
    depthDesc.format = rhi::Format::D32_Float;
    depthDesc.usage = rhi::ResourceUsage::DepthStencil;
    auto depthTarget = device->createTexture(depthDesc);

    // 独立的深度纹理和深度管线是 RHI 对阴影贴图通道的通用描述。
    // 原生后端会在光照通道中把这张纹理作为采样资源绑定。
    rhi::TextureDesc shadowDesc;
    shadowDesc.extent = {1024, 1024};
    shadowDesc.format = rhi::Format::D32_Float;
    shadowDesc.usage = rhi::ResourceUsage::DepthStencil | rhi::ResourceUsage::Texture;
    shadowDesc.debugName = "Sphere shadow map";
    auto shadowMap = device->createTexture(shadowDesc);
    rhi::PipelineDesc shadowPipelineDesc = pipelineDesc;
    shadowPipelineDesc.fragmentShader.reset();
    shadowPipelineDesc.colorFormat = rhi::Format::Unknown;
    shadowPipelineDesc.depthFormat = rhi::Format::D32_Float;
    shadowPipelineDesc.depthBiasConstant = 1.25f;
    shadowPipelineDesc.depthBiasSlope = 1.5f;
    shadowPipelineDesc.debugName = "Shadow depth pipeline";
    auto shadowPipeline = device->createPipeline(shadowPipelineDesc);

    // 录制索引式球体绘制。原生后端可以在不修改此处代码的情况下，
    // 将同一命令流转换为 Vulkan、D3D11 或 D3D12 命令。
    auto commands = device->createCommandList();
    commands->begin();
    rhi::RenderPassDesc pass;
    pass.clear.color = {0.025f, 0.03f, 0.04f, 1.0f};
    pass.clear.depth = 1.0f;
    // 阴影贴图通道。抽象 barrier/状态调用显式表达资源转换，
    // 便于 Vulkan 和 D3D12 后端实现正确同步。
    commands->transition(shadowMap.get(), rhi::ResourceState::Undefined,
                         rhi::ResourceState::DepthWrite);
    rhi::RenderPassDesc shadowPass;
    shadowPass.colorLoad = rhi::LoadOp::DontCare;
    shadowPass.depthLoad = rhi::LoadOp::Clear;
    shadowPass.depthFormat = rhi::Format::D32_Float;
    shadowPass.debugName = "Shadow map pass";
    commands->beginRenderPass(shadowPass, nullptr, shadowMap.get());
    commands->setPipeline(shadowPipeline.get());
    commands->setViewport(0, 0, 1024.0f, 1024.0f);
    commands->setVertexBuffer(vertexBuffer.get());
    commands->setIndexBuffer(indexBuffer.get());
    commands->drawIndexed(static_cast<uint32_t>(indices.size()));
    commands->endRenderPass();
    commands->transition(shadowMap.get(), rhi::ResourceState::DepthWrite,
                         rhi::ResourceState::ShaderRead);

    // 主通道先绘制地面，再绘制位于地面上方的球体。
    commands->beginRenderPass(pass, colorTarget.get(), depthTarget.get());
    commands->setPipeline(pipeline.get());
    commands->setViewport(0, 0, static_cast<float>(createInfo.extent.width),
                          static_cast<float>(createInfo.extent.height));
    commands->setScissor(0, 0, createInfo.extent.width, createInfo.extent.height);
    commands->setUniformBuffer(uniformBuffer.get());
    commands->setVertexBuffer(groundVertexBuffer.get());
    commands->setIndexBuffer(groundIndexBuffer.get());
    commands->setTexture(shadowMap.get(), 1);
    commands->drawIndexed(static_cast<uint32_t>(groundIndices.size()));
    commands->setVertexBuffer(vertexBuffer.get());
    commands->setIndexBuffer(indexBuffer.get());
    commands->drawIndexed(static_cast<uint32_t>(indices.size()));
    commands->endRenderPass();
    commands->end();
    device->submit(*commands);
    device->present();

    pbrdemo::writeSceneImages(material, "pbr_sphere.ppm", "pbr_sphere.bmp");
    std::cout << "Scene meshes: sphere " << vertices.size() << " vertices / "
              << indices.size() / 3 << " triangles, ground "
              << groundIndices.size() / 3 << " triangles\n";
    std::cout << "Shadow map: 1024 x 1024 D32 texture\n";
    std::cout << "Offline images: pbr_sphere.ppm, pbr_sphere.bmp\n";

#if defined(_WIN32)
    if (!hasNoWaitFlag(argc, argv)) return pbrdemo::runPbrWindow(material);
#else
    if (!hasNoWaitFlag(argc, argv)) {
        std::cout << "Press Enter to exit..." << std::endl;
        std::cin.get();
    }
#endif
    return 0;
}

} // 匿名命名空间

int main(int argc, char** argv) {
    return runDemo(argc, argv);
}
