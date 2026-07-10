#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "RenderVulkan.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <exception>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef PBR_SHADOW_EXAMPLE_SHADER_DIR
#define PBR_SHADOW_EXAMPLE_SHADER_DIR "Examples/Shaders"
#endif

namespace {

constexpr u32 SHADOW_MAP_SIZE = 2048;
constexpr float PI = 3.14159265358979323846F;

/// PBR 示例顶点格式：位置、法线和 UV，和 shader location 0/1/2 对应。
struct PbrVertex {
    glm::vec3 position{0.0F};
    glm::vec3 normal{0.0F, 1.0F, 0.0F};
    glm::vec2 uv{0.0F};
};

/// set=0,binding=0：每帧相机、投影和阴影矩阵。
struct SceneUniforms {
    glm::mat4 view{1.0F};
    glm::mat4 projection{1.0F};
    glm::mat4 viewProjection{1.0F};
    glm::mat4 lightViewProjection{1.0F};
    glm::vec4 cameraPosition{0.0F};
    glm::vec4 ambientColorExposure{0.03F, 0.035F, 0.04F, 1.0F};
};

/// set=0,binding=1：方向光参数。w 分别存放强度和 shadow bias。
struct LightUniforms {
    glm::vec4 directionIntensity{0.35F, -1.0F, 0.25F, 4.0F};
    glm::vec4 colorShadowBias{1.0F, 0.96F, 0.88F, 0.0015F};
};

/// set=1,binding=0：每个物体的矩阵和材质索引。
struct ObjectGpuData {
    glm::mat4 localToWorld{1.0F};
    glm::mat4 normalMatrix{1.0F};
    glm::uvec4 materialIndex{0, 0, 0, 0};
};

/// set=1,binding=1：PBR 材质参数，x=metallic，y=roughness。
struct MaterialGpuData {
    glm::vec4 baseColor{1.0F};
    glm::vec4 metallicRoughness{0.0F, 0.6F, 0.0F, 0.0F};
};

/// CPU 侧生成的合并网格，plane 和 sphere 用 submesh 区分。
struct GeneratedMesh {
    std::vector<PbrVertex> vertices;
    std::vector<u32> indices;
    SubmeshDesc planeSubmesh;
    SubmeshDesc sphereSubmesh;
};

/// swapchain 相关后备缓冲和 scene depth。窗口 resize 时重建这组资源。
struct FrameTargets {
    SwapchainHandle swapchain{};
    std::vector<TextureHandle> swapchainImages;
    std::vector<TextureViewHandle> swapchainImageViews;
    TextureHandle depthTexture{};
    TextureViewHandle depthView{};
    Extent2D extent{};
    Format colorFormat = Format::Undefined;
};

/// 示例中 Vulkan 后端创建出的 GPU 资源和高层渲染描述。
struct ExampleResources {
    GeneratedMesh cpuMesh;
    MeshDesc meshDesc;
    std::vector<MaterialDesc> materials;
    std::vector<ObjectGpuData> objectData;
    std::vector<MaterialGpuData> materialData;
    SceneUniforms sceneUniforms{};
    LightUniforms lightUniforms{};

    BufferHandle vertexBuffer{};
    BufferHandle indexBuffer{};
    BufferHandle sceneUniformBuffer{};
    BufferHandle lightUniformBuffer{};
    BufferHandle objectBuffer{};
    BufferHandle materialBuffer{};

    TextureHandle shadowMap{};
    TextureViewHandle shadowMapView{};
    SamplerHandle shadowSampler{};

    BindGroupLayoutHandle sceneLayout{};
    BindGroupLayoutHandle objectLayout{};
    BindGroupHandle sceneBindGroup{};
    BindGroupHandle objectBindGroup{};
    PipelineLayoutHandle pipelineLayout{};
    PipelineCacheHandle pipelineCache{};
    PipelineHandle pbrPipeline{};
    PipelineHandle shadowPipeline{};
};

template <typename T>
std::vector<std::byte> bytesFromObject(const T& value) {
    std::vector<std::byte> bytes(sizeof(T));
    std::memcpy(bytes.data(), &value, sizeof(T));
    return bytes;
}

template <typename T>
std::vector<std::byte> bytesFromVector(const std::vector<T>& values) {
    std::vector<std::byte> bytes(sizeof(T) * values.size());
    if (!values.empty()) {
        std::memcpy(bytes.data(), values.data(), bytes.size());
    }
    return bytes;
}

std::string shaderPath(const char* fileName) {
    return std::string(PBR_SHADOW_EXAMPLE_SHADER_DIR) + "/" + fileName;
}

Extent2D waitForDrawableSize(GLFWwindow* window) {
    int width = 0;
    int height = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwGetFramebufferSize(window, &width, &height);
        if (width > 0 && height > 0) {
            return {static_cast<u32>(width), static_cast<u32>(height)};
        }
        glfwWaitEvents();
    }
    return {};
}

glm::mat4 makeVulkanPerspective(float fovRadians, float aspect, float nearPlane, float farPlane) {
    glm::mat4 projection = glm::perspectiveRH_ZO(fovRadians, aspect, nearPlane, farPlane);
    projection[1][1] *= -1.0F;
    return projection;
}

GeneratedMesh generatePlaneAndSphere() {
    GeneratedMesh mesh;

    const float halfSize = 6.0F;
    mesh.planeSubmesh.name = "Plane";
    mesh.planeSubmesh.firstVertex = 0;
    mesh.planeSubmesh.vertexCount = 4;
    mesh.planeSubmesh.firstIndex = 0;
    mesh.planeSubmesh.indexCount = 6;
    mesh.planeSubmesh.materialIndex = 0;
    mesh.planeSubmesh.boundsMin = {-halfSize, 0.0F, -halfSize};
    mesh.planeSubmesh.boundsMax = {halfSize, 0.0F, halfSize};
    mesh.planeSubmesh.boundsSphere = {{0.0F, 0.0F, 0.0F}, halfSize * 1.41421356F};

    mesh.vertices.push_back({{-halfSize, 0.0F, -halfSize}, {0.0F, 1.0F, 0.0F}, {0.0F, 0.0F}});
    mesh.vertices.push_back({{halfSize, 0.0F, -halfSize}, {0.0F, 1.0F, 0.0F}, {1.0F, 0.0F}});
    mesh.vertices.push_back({{halfSize, 0.0F, halfSize}, {0.0F, 1.0F, 0.0F}, {1.0F, 1.0F}});
    mesh.vertices.push_back({{-halfSize, 0.0F, halfSize}, {0.0F, 1.0F, 0.0F}, {0.0F, 1.0F}});
    mesh.indices.insert(mesh.indices.end(), {0, 2, 1, 0, 3, 2});

    const u32 sphereFirstVertex = static_cast<u32>(mesh.vertices.size());
    const u32 sphereFirstIndex = static_cast<u32>(mesh.indices.size());
    const u32 slices = 64;
    const u32 stacks = 32;
    const float radius = 1.0F;
    const glm::vec3 center{0.0F, radius, 0.0F};

    for (u32 stack = 0; stack <= stacks; ++stack) {
        const float v = static_cast<float>(stack) / static_cast<float>(stacks);
        const float phi = v * PI;
        for (u32 slice = 0; slice <= slices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = u * PI * 2.0F;
            glm::vec3 normal{
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta)
            };
            mesh.vertices.push_back({center + normal * radius, normal, {u, v}});
        }
    }

    for (u32 stack = 0; stack < stacks; ++stack) {
        for (u32 slice = 0; slice < slices; ++slice) {
            const u32 row0 = sphereFirstVertex + stack * (slices + 1);
            const u32 row1 = sphereFirstVertex + (stack + 1) * (slices + 1);
            const u32 a = row0 + slice;
            const u32 b = row1 + slice;
            const u32 c = row1 + slice + 1;
            const u32 d = row0 + slice + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, a, d, c});
        }
    }

    mesh.sphereSubmesh.name = "Sphere";
    mesh.sphereSubmesh.firstVertex = sphereFirstVertex;
    mesh.sphereSubmesh.vertexCount = static_cast<u32>(mesh.vertices.size()) - sphereFirstVertex;
    mesh.sphereSubmesh.firstIndex = sphereFirstIndex;
    mesh.sphereSubmesh.indexCount = static_cast<u32>(mesh.indices.size()) - sphereFirstIndex;
    mesh.sphereSubmesh.materialIndex = 1;
    mesh.sphereSubmesh.boundsMin = center - glm::vec3(radius);
    mesh.sphereSubmesh.boundsMax = center + glm::vec3(radius);
    mesh.sphereSubmesh.boundsSphere = {center, radius};

    return mesh;
}

BufferHandle createBuffer(VulkanRenderer& renderer, const char* name, u64 size, BufferUsage usage, MemoryUsage memoryUsage) {
    BufferDesc desc{};
    desc.debugName = name;
    desc.size = size;
    desc.usage = usage;
    desc.memoryUsage = memoryUsage;
    return renderer.createBuffer(desc);
}

ShaderDesc makeShader(ShaderStage stage, const char* fileName, const char* debugName) {
    ShaderDesc shader{};
    shader.debugName = debugName;
    shader.stage = stage;
    shader.language = ShaderLanguage::SPIRV;
    shader.entryPoint = "main";
    shader.filePath = shaderPath(fileName);
    return shader;
}

VertexBufferLayoutDesc makePbrVertexLayout() {
    VertexBufferLayoutDesc layout{};
    layout.binding = 0;
    layout.stride = sizeof(PbrVertex);
    layout.inputRate = VertexInputRate::PerVertex;
    layout.attributes = {
        {"POSITION", 0, 0, 0, VertexFormat::Float32x3, offsetof(PbrVertex, position)},
        {"NORMAL", 0, 1, 0, VertexFormat::Float32x3, offsetof(PbrVertex, normal)},
        {"TEXCOORD", 0, 2, 0, VertexFormat::Float32x2, offsetof(PbrVertex, uv)}
    };
    return layout;
}

GraphicsPipelineDesc makePbrPipelineDesc(PipelineLayoutHandle layout, PipelineCacheHandle cache, Format colorFormat) {
    GraphicsPipelineDesc desc{};
    desc.debugName = "Example.PBRPipeline";
    desc.cache = cache;
    desc.layout = layout;
    desc.shaders = {
        makeShader(ShaderStage::Vertex, "pbr_lit.vert.spv", "PBR.Vertex"),
        makeShader(ShaderStage::Fragment, "pbr_lit.frag.spv", "PBR.Fragment")
    };
    desc.vertexBuffers = {makePbrVertexLayout()};
    desc.colorFormats = {colorFormat == Format::Undefined ? Format::BGRA8_SRGB : colorFormat};
    desc.depthStencilFormat = Format::D32_Float;
    desc.raster.cullMode = CullMode::None;
    desc.depthStencil.depthTestEnable = true;
    desc.depthStencil.depthWriteEnable = true;
    desc.depthStencil.depthCompareOp = CompareOp::LessOrEqual;
    desc.blend.attachments.push_back(ColorBlendAttachmentState{});
    return desc;
}

GraphicsPipelineDesc makeShadowPipelineDesc(PipelineLayoutHandle layout, PipelineCacheHandle cache) {
    GraphicsPipelineDesc desc{};
    desc.debugName = "Example.ShadowPipeline";
    desc.cache = cache;
    desc.layout = layout;
    desc.shaders = {
        makeShader(ShaderStage::Vertex, "shadow_depth.vert.spv", "Shadow.Vertex"),
        makeShader(ShaderStage::Fragment, "shadow_depth.frag.spv", "Shadow.Fragment")
    };
    desc.vertexBuffers = {makePbrVertexLayout()};
    desc.colorFormats = {};
    desc.depthStencilFormat = Format::D32_Float;
    desc.raster.cullMode = CullMode::None;
    desc.raster.depthBiasEnable = true;
    desc.raster.depthBiasConstantFactor = 1.25F;
    desc.raster.depthBiasSlopeFactor = 1.75F;
    desc.depthStencil.depthTestEnable = true;
    desc.depthStencil.depthWriteEnable = true;
    desc.depthStencil.depthCompareOp = CompareOp::LessOrEqual;
    desc.dynamicStates = {DynamicState::Viewport, DynamicState::Scissor};
    return desc;
}

FrameTargets createFrameTargets(VulkanRenderer& renderer, Extent2D requestedExtent) {
    FrameTargets targets{};

    SwapchainDesc swapchainDesc{};
    swapchainDesc.debugName = "Example.Swapchain";
    swapchainDesc.extent = requestedExtent;
    swapchainDesc.preferredFormat = Format::BGRA8_SRGB;
    swapchainDesc.colorSpace = ColorSpace::SRGBNonlinear;
    swapchainDesc.presentMode = PresentMode::FIFO;
    swapchainDesc.imageCount = 2;
    targets.swapchain = renderer.createSwapchain(swapchainDesc);
    targets.extent = renderer.getSwapchainExtent(targets.swapchain);
    targets.colorFormat = renderer.getSwapchainFormat(targets.swapchain);
    targets.swapchainImages = renderer.getSwapchainImages(targets.swapchain);
    targets.swapchainImageViews = renderer.getSwapchainImageViews(targets.swapchain);

    TextureDesc depthDesc{};
    depthDesc.debugName = "Example.SceneDepth";
    depthDesc.dimension = TextureDimension::Texture2D;
    depthDesc.extent = {targets.extent.width, targets.extent.height, 1};
    depthDesc.format = Format::D32_Float;
    depthDesc.usage = TextureUsage::DepthStencilAttachment;
    targets.depthTexture = renderer.createTexture(depthDesc);

    TextureViewDesc depthViewDesc{};
    depthViewDesc.debugName = "Example.SceneDepthView";
    depthViewDesc.texture = targets.depthTexture;
    depthViewDesc.dimension = TextureViewDimension::View2D;
    depthViewDesc.aspect = TextureAspect::Depth;
    targets.depthView = renderer.createTextureView(depthViewDesc);

    return targets;
}

void destroyFrameTargets(VulkanRenderer& renderer, FrameTargets& targets) {
    renderer.destroy(targets.depthView);
    renderer.destroy(targets.depthTexture);
    renderer.destroy(targets.swapchain);
    targets = {};
}

ExampleResources createExampleResources(VulkanRenderer& renderer, Format colorFormat) {
    ExampleResources resources{};
    resources.cpuMesh = generatePlaneAndSphere();

    resources.vertexBuffer = createBuffer(
        renderer,
        "Example.GeometryVertices",
        sizeof(PbrVertex) * resources.cpuMesh.vertices.size(),
        BufferUsage::Vertex | BufferUsage::TransferDestination,
        MemoryUsage::GpuOnly);
    resources.indexBuffer = createBuffer(
        renderer,
        "Example.GeometryIndices",
        sizeof(u32) * resources.cpuMesh.indices.size(),
        BufferUsage::Index | BufferUsage::TransferDestination,
        MemoryUsage::GpuOnly);
    resources.sceneUniformBuffer = createBuffer(
        renderer,
        "Example.SceneUniforms",
        sizeof(SceneUniforms),
        BufferUsage::Uniform | BufferUsage::TransferDestination,
        MemoryUsage::CpuToGpu);
    resources.lightUniformBuffer = createBuffer(
        renderer,
        "Example.LightUniforms",
        sizeof(LightUniforms),
        BufferUsage::Uniform | BufferUsage::TransferDestination,
        MemoryUsage::CpuToGpu);

    resources.objectData.resize(2);
    resources.objectData[0].localToWorld = glm::mat4(1.0F);
    resources.objectData[0].normalMatrix = glm::transpose(glm::inverse(resources.objectData[0].localToWorld));
    resources.objectData[0].materialIndex = {0, 0, 0, 0};
    resources.objectData[1].localToWorld = glm::translate(glm::mat4(1.0F), glm::vec3(0.0F, 0.0F, 0.0F));
    resources.objectData[1].normalMatrix = glm::transpose(glm::inverse(resources.objectData[1].localToWorld));
    resources.objectData[1].materialIndex = {1, 0, 0, 0};

    resources.materialData = {
        {{0.62F, 0.58F, 0.52F, 1.0F}, {0.0F, 0.72F, 0.0F, 0.0F}},
        {{0.95F, 0.64F, 0.31F, 1.0F}, {0.85F, 0.28F, 0.0F, 0.0F}}
    };

    resources.objectBuffer = createBuffer(
        renderer,
        "Example.ObjectBuffer",
        sizeof(ObjectGpuData) * resources.objectData.size(),
        BufferUsage::Storage | BufferUsage::TransferDestination,
        MemoryUsage::GpuOnly);
    resources.materialBuffer = createBuffer(
        renderer,
        "Example.MaterialBuffer",
        sizeof(MaterialGpuData) * resources.materialData.size(),
        BufferUsage::Storage | BufferUsage::TransferDestination,
        MemoryUsage::GpuOnly);

    TextureDesc shadowDesc{};
    shadowDesc.debugName = "Example.ShadowMap";
    shadowDesc.dimension = TextureDimension::Texture2D;
    shadowDesc.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
    shadowDesc.format = Format::D32_Float;
    shadowDesc.usage = TextureUsage::DepthStencilAttachment | TextureUsage::Sampled;
    resources.shadowMap = renderer.createTexture(shadowDesc);

    TextureViewDesc shadowViewDesc{};
    shadowViewDesc.debugName = "Example.ShadowMapView";
    shadowViewDesc.texture = resources.shadowMap;
    shadowViewDesc.dimension = TextureViewDimension::View2D;
    shadowViewDesc.aspect = TextureAspect::Depth;
    resources.shadowMapView = renderer.createTextureView(shadowViewDesc);

    SamplerDesc shadowSamplerDesc{};
    shadowSamplerDesc.debugName = "Example.ShadowCompareSampler";
    shadowSamplerDesc.minFilter = FilterMode::Linear;
    shadowSamplerDesc.magFilter = FilterMode::Linear;
    shadowSamplerDesc.mipmapMode = MipmapMode::Nearest;
    shadowSamplerDesc.addressU = AddressMode::ClampToBorder;
    shadowSamplerDesc.addressV = AddressMode::ClampToBorder;
    shadowSamplerDesc.addressW = AddressMode::ClampToBorder;
    shadowSamplerDesc.enableCompare = true;
    shadowSamplerDesc.compareOp = CompareOp::LessOrEqual;
    shadowSamplerDesc.borderColor = BorderColor::OpaqueWhite;
    resources.shadowSampler = renderer.createSampler(shadowSamplerDesc);

    BindGroupLayoutDesc sceneLayout{};
    sceneLayout.debugName = "Example.SceneLayout";
    sceneLayout.set = 0;
    sceneLayout.entries = {
        {0, BindingType::UniformBuffer, ShaderStage::Vertex | ShaderStage::Fragment, 1},
        {1, BindingType::UniformBuffer, ShaderStage::Fragment, 1},
        {2, BindingType::CombinedTextureSampler, ShaderStage::Fragment, 1, false, TextureViewDimension::View2D, TextureSampleType::Depth}
    };
    resources.sceneLayout = renderer.createBindGroupLayout(sceneLayout);

    BindGroupLayoutDesc objectLayout{};
    objectLayout.debugName = "Example.ObjectLayout";
    objectLayout.set = 1;
    objectLayout.entries = {
        {0, BindingType::StorageBuffer, ShaderStage::Vertex, 1, false},
        {1, BindingType::StorageBuffer, ShaderStage::Fragment, 1, false}
    };
    resources.objectLayout = renderer.createBindGroupLayout(objectLayout);

    PipelineLayoutDesc pipelineLayoutDesc{};
    pipelineLayoutDesc.debugName = "Example.PipelineLayout";
    pipelineLayoutDesc.bindGroupLayouts = {resources.sceneLayout, resources.objectLayout};
    resources.pipelineLayout = renderer.createPipelineLayout(pipelineLayoutDesc);

    PipelineCacheDesc cacheDesc{};
    cacheDesc.debugName = "Example.PipelineCache";
    resources.pipelineCache = renderer.createPipelineCache(cacheDesc);

    BindGroupDesc sceneBindGroup{};
    sceneBindGroup.debugName = "Example.SceneBindGroup";
    sceneBindGroup.layout = resources.sceneLayout;
    sceneBindGroup.bindings = {
        {0, 0, BindingType::UniformBuffer, {resources.sceneUniformBuffer, 0, sizeof(SceneUniforms)}},
        {1, 0, BindingType::UniformBuffer, {resources.lightUniformBuffer, 0, sizeof(LightUniforms)}},
        {2, 0, BindingType::CombinedTextureSampler, {}, {resources.shadowMapView, resources.shadowMap}, resources.shadowSampler}
    };
    resources.sceneBindGroup = renderer.createBindGroup(sceneBindGroup);

    BindGroupDesc objectBindGroup{};
    objectBindGroup.debugName = "Example.ObjectBindGroup";
    objectBindGroup.layout = resources.objectLayout;
    objectBindGroup.bindings = {
        {0, 0, BindingType::StorageBuffer, {resources.objectBuffer, 0, sizeof(ObjectGpuData) * resources.objectData.size()}},
        {1, 0, BindingType::StorageBuffer, {resources.materialBuffer, 0, sizeof(MaterialGpuData) * resources.materialData.size()}}
    };
    resources.objectBindGroup = renderer.createBindGroup(objectBindGroup);

    resources.pbrPipeline = renderer.createGraphicsPipeline(makePbrPipelineDesc(resources.pipelineLayout, resources.pipelineCache, colorFormat));
    resources.shadowPipeline = renderer.createGraphicsPipeline(makeShadowPipelineDesc(resources.pipelineLayout, resources.pipelineCache));

    resources.meshDesc.debugName = "Example.GeneratedPlaneAndSphere";
    resources.meshDesc.vertexStreams = {{resources.vertexBuffer, 0, 0, sizeof(PbrVertex)}};
    resources.meshDesc.indexStream = IndexStream{resources.indexBuffer, IndexType::UInt32, 0, static_cast<u32>(resources.cpuMesh.indices.size())};
    resources.meshDesc.submeshes = {resources.cpuMesh.planeSubmesh, resources.cpuMesh.sphereSubmesh};

    MaterialDesc planeMaterial{};
    planeMaterial.debugName = "Example.PlaneMaterial";
    planeMaterial.pipeline = resources.pbrPipeline;
    planeMaterial.bindGroups = {resources.sceneBindGroup, resources.objectBindGroup};
    planeMaterial.parameters = {
        {"baseColorFactor", MaterialParameterType::Float4, resources.materialData[0].baseColor},
        {"metallic", MaterialParameterType::Float, {resources.materialData[0].metallicRoughness.x, 0.0F, 0.0F, 0.0F}},
        {"roughness", MaterialParameterType::Float, {resources.materialData[0].metallicRoughness.y, 0.0F, 0.0F, 0.0F}}
    };

    MaterialDesc sphereMaterial{};
    sphereMaterial.debugName = "Example.SphereMaterial";
    sphereMaterial.pipeline = resources.pbrPipeline;
    sphereMaterial.bindGroups = {resources.sceneBindGroup, resources.objectBindGroup};
    sphereMaterial.parameters = {
        {"baseColorFactor", MaterialParameterType::Float4, resources.materialData[1].baseColor},
        {"metallic", MaterialParameterType::Float, {resources.materialData[1].metallicRoughness.x, 0.0F, 0.0F, 0.0F}},
        {"roughness", MaterialParameterType::Float, {resources.materialData[1].metallicRoughness.y, 0.0F, 0.0F, 0.0F}}
    };
    resources.materials = {planeMaterial, sphereMaterial};

    return resources;
}

void updateSceneUniforms(ExampleResources& resources, Extent2D extent) {
    const float aspect = extent.height == 0 ? 1.0F : static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const glm::vec3 cameraPosition{4.0F, 3.0F, 5.0F};
    const glm::vec3 cameraTarget{0.0F, 0.8F, 0.0F};
    const glm::vec3 lightDirection = glm::normalize(glm::vec3(0.35F, -1.0F, 0.25F));
    const glm::vec3 lightPosition = -lightDirection * 7.0F + glm::vec3(0.0F, 1.0F, 0.0F);

    resources.sceneUniforms.view = glm::lookAtRH(cameraPosition, cameraTarget, glm::vec3(0.0F, 1.0F, 0.0F));
    resources.sceneUniforms.projection = makeVulkanPerspective(60.0F * PI / 180.0F, aspect, 0.1F, 100.0F);
    resources.sceneUniforms.viewProjection = resources.sceneUniforms.projection * resources.sceneUniforms.view;
    const glm::mat4 lightView = glm::lookAtRH(lightPosition, glm::vec3(0.0F, 0.6F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::mat4 lightProjection = glm::orthoRH_ZO(-7.0F, 7.0F, -7.0F, 7.0F, 0.1F, 20.0F);
    resources.sceneUniforms.lightViewProjection = lightProjection * lightView;
    resources.sceneUniforms.cameraPosition = glm::vec4(cameraPosition, 1.0F);
    resources.sceneUniforms.ambientColorExposure = glm::vec4(0.035F, 0.04F, 0.045F, 1.15F);

    resources.lightUniforms.directionIntensity = glm::vec4(lightDirection, 4.0F);
    resources.lightUniforms.colorShadowBias = glm::vec4(1.0F, 0.96F, 0.88F, 0.0015F);
}

BufferUploadDesc makeUpload(BufferHandle destination, std::vector<std::byte> data) {
    BufferUploadDesc upload{};
    upload.destination = destination;
    upload.data = std::move(data);
    return upload;
}

UploadBatchDesc makeUploadBatch(const ExampleResources& resources) {
    UploadBatchDesc uploads{};
    uploads.buffers.push_back(makeUpload(resources.vertexBuffer, bytesFromVector(resources.cpuMesh.vertices)));
    uploads.buffers.push_back(makeUpload(resources.indexBuffer, bytesFromVector(resources.cpuMesh.indices)));
    uploads.buffers.push_back(makeUpload(resources.sceneUniformBuffer, bytesFromObject(resources.sceneUniforms)));
    uploads.buffers.push_back(makeUpload(resources.lightUniformBuffer, bytesFromObject(resources.lightUniforms)));
    uploads.buffers.push_back(makeUpload(resources.objectBuffer, bytesFromVector(resources.objectData)));
    uploads.buffers.push_back(makeUpload(resources.materialBuffer, bytesFromVector(resources.materialData)));
    return uploads;
}

DrawIndexedCommand makeDraw(const ExampleResources& resources, const SubmeshDesc& submesh, PipelineHandle pipeline, u32 objectIndex) {
    DrawIndexedCommand draw{};
    draw.pipeline = pipeline;
    draw.bindGroups = {resources.sceneBindGroup, resources.objectBindGroup};
    draw.vertexStreams = resources.meshDesc.vertexStreams;
    draw.indexStream = *resources.meshDesc.indexStream;
    draw.indexCount = submesh.indexCount;
    draw.instanceCount = 1;
    draw.firstIndex = submesh.firstIndex;
    draw.vertexOffsetElements = 0;
    draw.firstInstance = objectIndex;
    return draw;
}

RenderCameraSetDesc makeCameraSetDesc(const ExampleResources& resources) {
    RenderCameraSetDesc cameras{};
    cameras.main.view = resources.sceneUniforms.view;
    cameras.main.projection = resources.sceneUniforms.projection;
    cameras.main.viewProjection = resources.sceneUniforms.viewProjection;
    cameras.main.position = glm::vec3(resources.sceneUniforms.cameraPosition);
    return cameras;
}

SceneEnvironmentDesc makeEnvironmentDesc(const ExampleResources& resources) {
    SceneEnvironmentDesc environment{};
    environment.ambientColor = glm::vec3(resources.sceneUniforms.ambientColorExposure);
    environment.exposure = resources.sceneUniforms.ambientColorExposure.w;
    return environment;
}

RenderLightSetDesc makeLightSetDesc(const ExampleResources& resources) {
    RenderLightSetDesc lights{};
    LightData light{};
    light.type = LightType::Directional;
    light.direction = glm::vec3(resources.lightUniforms.directionIntensity);
    light.color = glm::vec3(resources.lightUniforms.colorShadowBias);
    light.intensity = resources.lightUniforms.directionIntensity.w;
    lights.items.push_back(light);
    return lights;
}

RenderObjectSetDesc makeObjectSetDesc(const ExampleResources& resources) {
    RenderObjectSetDesc objects{};
    RenderObjectDesc plane{};
    plane.debugName = "Plane";
    plane.mesh = MeshHandle(1);
    plane.material = MaterialHandle(1);
    plane.submeshIndex = 0;
    plane.transform.localToWorld = resources.objectData[0].localToWorld;
    plane.transform.previousLocalToWorld = resources.objectData[0].localToWorld;
    plane.worldBounds = {resources.cpuMesh.planeSubmesh.boundsMin, resources.cpuMesh.planeSubmesh.boundsMax};
    plane.worldBoundsSphere = resources.cpuMesh.planeSubmesh.boundsSphere;

    RenderObjectDesc sphere{};
    sphere.debugName = "Sphere";
    sphere.mesh = MeshHandle(1);
    sphere.material = MaterialHandle(2);
    sphere.submeshIndex = 1;
    sphere.transform.localToWorld = resources.objectData[1].localToWorld;
    sphere.transform.previousLocalToWorld = resources.objectData[1].localToWorld;
    sphere.worldBounds = {resources.cpuMesh.sphereSubmesh.boundsMin, resources.cpuMesh.sphereSubmesh.boundsMax};
    sphere.worldBoundsSphere = resources.cpuMesh.sphereSubmesh.boundsSphere;

    objects.items = {plane, sphere};
    return objects;
}

FramePacket buildFramePacket(
    const ExampleResources& resources,
    const FrameTargets& targets,
    u32 imageIndex,
    SemaphoreHandle imageAvailable,
    SemaphoreHandle renderFinished,
    u64 frameIndex) {
    FramePacket packet{};
    packet.settings.drawableSize = targets.extent;
    packet.settings.viewport = {0.0F, 0.0F, static_cast<float>(targets.extent.width), static_cast<float>(targets.extent.height), 0.0F, 1.0F};
    packet.settings.scissor = {{0, 0}, targets.extent};
    packet.settings.frameIndex = frameIndex;
    packet.settings.enableVsync = true;
    packet.swapchain.extent = targets.extent;
    packet.swapchain.preferredFormat = targets.colorFormat;
    packet.uploads = makeUploadBatch(resources);
    packet.cameras = makeCameraSetDesc(resources);
    packet.environment = makeEnvironmentDesc(resources);
    packet.lights = makeLightSetDesc(resources);
    packet.objects = makeObjectSetDesc(resources);

    RenderGraphTextureDesc shadowMap{};
    shadowMap.name = "ShadowMap";
    shadowMap.imported = true;
    shadowMap.externalHandle = resources.shadowMap;
    shadowMap.desc.debugName = "ShadowMap";
    shadowMap.desc.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
    shadowMap.desc.format = Format::D32_Float;
    shadowMap.desc.usage = TextureUsage::DepthStencilAttachment | TextureUsage::Sampled;

    RenderGraphTextureDesc sceneDepth{};
    sceneDepth.name = "SceneDepth";
    sceneDepth.imported = true;
    sceneDepth.externalHandle = targets.depthTexture;
    sceneDepth.desc.debugName = "SceneDepth";
    sceneDepth.desc.extent = {targets.extent.width, targets.extent.height, 1};
    sceneDepth.desc.format = Format::D32_Float;
    sceneDepth.desc.usage = TextureUsage::DepthStencilAttachment;

    RenderGraphTextureDesc backBuffer{};
    backBuffer.name = "BackBuffer";
    backBuffer.imported = true;
    backBuffer.externalHandle = imageIndex < targets.swapchainImages.size() ? targets.swapchainImages[imageIndex] : TextureHandle{};
    backBuffer.desc.debugName = "BackBuffer";
    backBuffer.desc.extent = {targets.extent.width, targets.extent.height, 1};
    backBuffer.desc.format = targets.colorFormat;
    backBuffer.desc.usage = TextureUsage::ColorAttachment | TextureUsage::Present;

    packet.graph.textures = {shadowMap, sceneDepth, backBuffer};
    packet.graph.passes = {
        {"ShadowPass", RenderGraphPassType::Raster, QueueType::Graphics, {}, {{"ShadowMap", RenderGraphResourceType::Texture, ResourceState::DepthWrite}}, {}, RenderGraphAttachmentDesc{"ShadowMap", TextureAspect::Depth}},
        {"PBRLightingPass", RenderGraphPassType::Raster, QueueType::Graphics, {{"ShadowMap", RenderGraphResourceType::Texture, ResourceState::ShaderRead}}, {{"BackBuffer", RenderGraphResourceType::SwapchainImage, ResourceState::RenderTarget}, {"SceneDepth", RenderGraphResourceType::Texture, ResourceState::DepthWrite}}, {RenderGraphAttachmentDesc{"BackBuffer", TextureAspect::Color}}, RenderGraphAttachmentDesc{"SceneDepth", TextureAspect::Depth}},
        {"Present", RenderGraphPassType::Present, QueueType::Present, {{"BackBuffer", RenderGraphResourceType::SwapchainImage, ResourceState::Present}}, {}, {}, std::nullopt, false, true}
    };

    RenderPassWorkload shadowWorkload{};
    shadowWorkload.passName = "ShadowPass";
    shadowWorkload.viewport = {0.0F, 0.0F, static_cast<float>(SHADOW_MAP_SIZE), static_cast<float>(SHADOW_MAP_SIZE), 0.0F, 1.0F};
    shadowWorkload.scissor = {{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
    shadowWorkload.indexedDraws = {
        makeDraw(resources, resources.cpuMesh.planeSubmesh, resources.shadowPipeline, 0),
        makeDraw(resources, resources.cpuMesh.sphereSubmesh, resources.shadowPipeline, 1)
    };

    RenderPassWorkload pbrWorkload{};
    pbrWorkload.passName = "PBRLightingPass";
    pbrWorkload.viewport = packet.settings.viewport;
    pbrWorkload.scissor = packet.settings.scissor;
    pbrWorkload.indexedDraws = {
        makeDraw(resources, resources.cpuMesh.planeSubmesh, resources.pbrPipeline, 0),
        makeDraw(resources, resources.cpuMesh.sphereSubmesh, resources.pbrPipeline, 1)
    };
    packet.workloads = {shadowWorkload, pbrWorkload};

    QueueSubmitDesc submit{};
    submit.debugName = "Example.FrameSubmit";
    submit.queue = QueueType::Graphics;
    submit.passNames = {"ShadowPass", "PBRLightingPass"};
    submit.waits = {{imageAvailable, 0, PipelineStage::ColorAttachmentOutput}};
    submit.signals = {{renderFinished, 0}};
    packet.submissions = {submit};

    PresentDesc present{};
    present.swapchain = targets.swapchain;
    present.imageIndex = imageIndex;
    present.waitSemaphores = {renderFinished};
    present.presentMode = PresentMode::FIFO;
    packet.present = present;
    return packet;
}

void destroyExampleResources(VulkanRenderer& renderer, ExampleResources& resources) {
    renderer.destroy(resources.pbrPipeline);
    renderer.destroy(resources.shadowPipeline);
    renderer.destroy(resources.pipelineCache);
    renderer.destroy(resources.pipelineLayout);
    renderer.destroy(resources.sceneBindGroup);
    renderer.destroy(resources.objectBindGroup);
    renderer.destroy(resources.sceneLayout);
    renderer.destroy(resources.objectLayout);
    renderer.destroy(resources.shadowSampler);
    renderer.destroy(resources.shadowMapView);
    renderer.destroy(resources.shadowMap);
    renderer.destroy(resources.materialBuffer);
    renderer.destroy(resources.objectBuffer);
    renderer.destroy(resources.lightUniformBuffer);
    renderer.destroy(resources.sceneUniformBuffer);
    renderer.destroy(resources.indexBuffer);
    renderer.destroy(resources.vertexBuffer);
    resources = {};
}

class GlfwScope {
public:
    GlfwScope() {
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("glfwInit 失败");
        }
    }

    ~GlfwScope() {
        glfwTerminate();
    }

    GlfwScope(const GlfwScope&) = delete;
    GlfwScope& operator=(const GlfwScope&) = delete;
};

} // namespace

int main(int argc, char** argv) {
    try {
        u64 maxFrames = 0;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--smoke-test") {
                maxFrames = 1;
            } else if (argument.rfind("--frames=", 0) == 0) {
                maxFrames = static_cast<u64>(std::stoull(argument.substr(9)));
            }
        }

        GlfwScope glfw;
        if (glfwVulkanSupported() != GLFW_TRUE) {
            throw std::runtime_error("当前系统 GLFW 未检测到 Vulkan 支持");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow* window = glfwCreateWindow(1280, 720, "RenderVulkan PBR Shadow Example", nullptr, nullptr);
        if (window == nullptr) {
            throw std::runtime_error("glfwCreateWindow 失败");
        }

        u32 extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (extensions == nullptr || extensionCount == 0) {
            throw std::runtime_error("GLFW 未返回 Vulkan instance extensions");
        }

        VulkanRenderer renderer;
        VulkanRendererDesc rendererDesc{};
        rendererDesc.backend.applicationName = "RenderVulkan PBR Shadow Example";
        rendererDesc.backend.engineName = "VulkanLearn";
        rendererDesc.backend.validation = ValidationMode::Enabled;
        rendererDesc.backend.requiredFeatures = RenderFeature::DynamicRendering;
        rendererDesc.backend.optionalFeatures = RenderFeature::DebugMarkers | RenderFeature::SamplerAnisotropy;
        rendererDesc.requiredInstanceExtensions.assign(extensions, extensions + extensionCount);
        rendererDesc.surface.ownsSurface = true;
        rendererDesc.surface.createSurface = [window](VkInstance instance) -> VkSurfaceKHR {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
                return VK_NULL_HANDLE;
            }
            return surface;
        };

        std::string errorMessage;
        if (!renderer.initialize(rendererDesc, &errorMessage)) {
            throw std::runtime_error("VulkanRenderer 初始化失败: " + errorMessage);
        }

        FrameTargets targets = createFrameTargets(renderer, waitForDrawableSize(window));
        ExampleResources resources = createExampleResources(renderer, targets.colorFormat);
        updateSceneUniforms(resources, targets.extent);

        SemaphoreHandle imageAvailable = renderer.createSemaphore({"Example.ImageAvailable", SemaphoreType::Binary});
        SemaphoreHandle renderFinished = renderer.createSemaphore({"Example.RenderFinished", SemaphoreType::Binary});

        u64 frameIndex = 0;
        while (!glfwWindowShouldClose(window) && (maxFrames == 0 || frameIndex < maxFrames)) {
            glfwPollEvents();

            const Extent2D drawableSize = waitForDrawableSize(window);
            if (drawableSize.width != targets.extent.width || drawableSize.height != targets.extent.height) {
                renderer.waitIdle();
                destroyFrameTargets(renderer, targets);
                targets = createFrameTargets(renderer, drawableSize);
            }

            updateSceneUniforms(resources, targets.extent);

            u32 imageIndex = 0;
            if (!renderer.acquireNextImage(targets.swapchain, imageAvailable, FenceHandle{}, &imageIndex, &errorMessage)) {
                std::cerr << "acquireNextImage 失败: " << errorMessage << '\n';
                break;
            }

            FramePacket packet = buildFramePacket(resources, targets, imageIndex, imageAvailable, renderFinished, frameIndex++);

            // 当前 RenderVulkan 后端还没有实现 RenderGraph 到 Vulkan command buffer 的自动录制。
            // 这里提交的是完整 FramePacket：后续补齐录制器后，ShadowPass/PBRLightingPass 会按 workloads 真正绘制。
            if (!renderer.submitFrame(packet, &errorMessage)) {
                std::cerr << "submitFrame 失败: " << errorMessage << '\n';
                break;
            }
        }

        renderer.waitIdle();
        renderer.destroy(renderFinished);
        renderer.destroy(imageAvailable);
        destroyExampleResources(renderer, resources);
        destroyFrameTargets(renderer, targets);
        renderer.shutdown();
        glfwDestroyWindow(window);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
