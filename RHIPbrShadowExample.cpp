#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "RHI/RHI.hpp"

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

#ifndef RHI_PBR_SHADOW_EXAMPLE_SHADER_DIR
#define RHI_PBR_SHADOW_EXAMPLE_SHADER_DIR "Examples/Shaders"
#endif

using namespace rhi;

namespace {

constexpr u32 SHADOW_MAP_SIZE = 2048;
constexpr float PI = 3.14159265358979323846F;

/// PBR 顶点格式，对应 Shader 中的位置、法线和 UV 输入。
struct PbrVertex {
    glm::vec3 position{0.0F};
    glm::vec3 normal{0.0F, 1.0F, 0.0F};
    glm::vec2 uv{0.0F};
};

/// 每帧更新的相机、投影、阴影矩阵和环境参数。
struct SceneUniforms {
    glm::mat4 view{1.0F};
    glm::mat4 projection{1.0F};
    glm::mat4 viewProjection{1.0F};
    glm::mat4 lightViewProjection{1.0F};
    glm::vec4 cameraPosition{0.0F};
    glm::vec4 ambientColorExposure{0.03F, 0.035F, 0.04F, 1.0F};
};

/// 方向光参数：方向、强度、颜色和阴影偏移。
struct LightUniforms {
    glm::vec4 directionIntensity{0.35F, -1.0F, 0.25F, 4.0F};
    glm::vec4 colorShadowBias{1.0F, 0.96F, 0.88F, 0.0015F};
};

/// 每个可渲染物体的变换矩阵和材质索引。
struct ObjectGpuData {
    glm::mat4 localToWorld{1.0F};
    glm::mat4 normalMatrix{1.0F};
    glm::uvec4 materialIndex{0, 0, 0, 0};
};

/// PBR 材质参数：基础色、金属度和粗糙度。
struct MaterialGpuData {
    glm::vec4 baseColor{1.0F};
    glm::vec4 metallicRoughness{0.0F, 0.6F, 0.0F, 0.0F};
};

/// CPU 生成的平面和球体共享一套顶点/索引缓冲，通过 submesh 区分。
struct GeneratedMesh {
    std::vector<PbrVertex> vertices;
    std::vector<u32> indices;
    RHISubmeshDesc planeSubmesh;
    RHISubmeshDesc sphereSubmesh;
};

/// 随窗口尺寸变化而重建的交换链和场景深度资源。
struct FrameTargets {
    RHISwapchain swapchain{};
    std::vector<RHITexture> swapchainImages;
    std::vector<RHITextureView> swapchainImageViews;
    RHITexture depthTexture{};
    RHITextureView depthView{};
    RHIExtent2D extent{};
    RHIFormat colorFormat = RHIFormat::Undefined;
};

/// PBR 示例持有的长期 GPU 资源和对应的 CPU 渲染描述。
struct ExampleResources {
    GeneratedMesh cpuMesh;
    RHIMeshDesc meshDesc;
    std::vector<RHIMaterialDesc> materials;
    std::vector<ObjectGpuData> objectData;
    std::vector<MaterialGpuData> materialData;
    SceneUniforms sceneUniforms{};
    LightUniforms lightUniforms{};

    RHIBuffer vertexBuffer{};
    RHIBuffer indexBuffer{};
    RHIBuffer sceneUniformBuffer{};
    RHIBuffer lightUniformBuffer{};
    RHIBuffer objectBuffer{};
    RHIBuffer materialBuffer{};

    RHITexture shadowMap{};
    RHITextureView shadowMapView{};
    RHISampler shadowSampler{};

    RHIBindSetLayout sceneLayout{};
    RHIBindSetLayout objectLayout{};
    RHIBindSet sceneBindSet{};
    RHIBindSet objectBindSet{};
    RHIPipelineLayout pipelineLayout{};
    RHIPipelineCache pipelineCache{};
    RHIPipeline pbrPipeline{};
    RHIPipeline shadowPipeline{};
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
    return std::string(RHI_PBR_SHADOW_EXAMPLE_SHADER_DIR) + "/" + fileName;
}

RHIExtent2D waitForDrawableSize(GLFWwindow* window) {
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
    mesh.planeSubmesh.boundsBox.min = {-halfSize, 0.0F, -halfSize};
    mesh.planeSubmesh.boundsBox.max = {halfSize, 0.0F, halfSize};
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
    mesh.sphereSubmesh.boundsBox.min = center - glm::vec3(radius);
    mesh.sphereSubmesh.boundsBox.max = center + glm::vec3(radius);
    mesh.sphereSubmesh.boundsSphere = {center, radius};

    return mesh;
}

RHIBuffer CreateBuffer(RHIDevice& renderer, const char* name, u64 size, RHIBufferUsage usage, RHIMemoryUsage memoryUsage) {
    RHIBufferDesc desc{};
    desc.debugName = name;
    desc.size = size;
    desc.usage = usage;
    desc.memoryUsage = memoryUsage;
    return renderer.CreateBuffer(desc);
}

RHIShaderDesc makeShader(RHIShaderStage stage, const char* fileName, const char* debugName) {
    RHIShaderDesc shader{};
    shader.debugName = debugName;
    shader.stage = stage;
    shader.language = RHIShaderLanguage::SPIRV;
    shader.entryPoint = "main";
    shader.filePath = shaderPath(fileName);
    return shader;
}

RHIVertexBufferLayoutDesc makePbrVertexLayout() {
    RHIVertexBufferLayoutDesc layout{};
    layout.binding = 0;
    layout.stride = sizeof(PbrVertex);
    layout.inputRate = RHIVertexInputRate::PerVertex;
    layout.attributes = {
        {"POSITION", 0, 0, 0, RHIVertexFormat::Float32x3, offsetof(PbrVertex, position)},
        {"NORMAL", 0, 1, 0, RHIVertexFormat::Float32x3, offsetof(PbrVertex, normal)},
        {"TEXCOORD", 0, 2, 0, RHIVertexFormat::Float32x2, offsetof(PbrVertex, uv)}
    };
    return layout;
}

RHIGraphicsPipelineDesc makePbrPipelineDesc(RHIPipelineLayout layout, RHIPipelineCache cache, RHIFormat colorFormat) {
    RHIGraphicsPipelineDesc desc{};
    desc.debugName = "Example.PBRPipeline";
    desc.cache = cache;
    desc.layout = layout;
    desc.shaders = {
        makeShader(RHIShaderStage::Vertex, "pbr_lit.vert.spv", "PBR.Vertex"),
        makeShader(RHIShaderStage::Fragment, "pbr_lit.frag.spv", "PBR.Fragment")
    };
    desc.vertexBuffers = {makePbrVertexLayout()};
    desc.colorFormats = {colorFormat == RHIFormat::Undefined ? RHIFormat::BGRA8_SRGB : colorFormat};
    desc.depthStencilFormat = RHIFormat::D32_Float;
    desc.raster.cullMode = RHICullMode::None;
    desc.depthStencil.depthTestEnable = true;
    desc.depthStencil.depthWriteEnable = true;
    desc.depthStencil.depthCompareOp = RHICompareOp::LessOrEqual;
    desc.blend.attachments.push_back(RHIColorBlendAttachmentState{});
    return desc;
}

RHIGraphicsPipelineDesc makeShadowPipelineDesc(RHIPipelineLayout layout, RHIPipelineCache cache) {
    RHIGraphicsPipelineDesc desc{};
    desc.debugName = "Example.ShadowPipeline";
    desc.cache = cache;
    desc.layout = layout;
    desc.shaders = {
        makeShader(RHIShaderStage::Vertex, "shadow_depth.vert.spv", "Shadow.Vertex"),
        makeShader(RHIShaderStage::Fragment, "shadow_depth.frag.spv", "Shadow.Fragment")
    };
    desc.vertexBuffers = {makePbrVertexLayout()};
    desc.colorFormats = {};
    desc.depthStencilFormat = RHIFormat::D32_Float;
    desc.raster.cullMode = RHICullMode::None;
    desc.raster.depthBiasEnable = true;
    desc.raster.depthBiasConstantFactor = 1.25F;
    desc.raster.depthBiasSlopeFactor = 1.75F;
    desc.depthStencil.depthTestEnable = true;
    desc.depthStencil.depthWriteEnable = true;
    desc.depthStencil.depthCompareOp = RHICompareOp::LessOrEqual;
    desc.dynamicStates = {RHIDynamicState::RHIViewport, RHIDynamicState::Scissor};
    return desc;
}

FrameTargets createFrameTargets(RHIDevice& renderer, RHIExtent2D requestedExtent) {
    FrameTargets targets{};

    RHISwapchainDesc swapchainDesc{};
    swapchainDesc.debugName = "Example.Swapchain";
    swapchainDesc.extent = requestedExtent;
    swapchainDesc.preferredFormat = RHIFormat::BGRA8_SRGB;
    swapchainDesc.colorSpace = RHIColorSpace::SRGBNonlinear;
    swapchainDesc.presentMode = RHIPresentMode::FIFO;
    swapchainDesc.imageCount = 2;
    targets.swapchain = renderer.CreateSwapchain(swapchainDesc);
    targets.extent = renderer.GetSwapchainExtent(targets.swapchain);
    targets.colorFormat = renderer.GetSwapchainFormat(targets.swapchain);
    targets.swapchainImages = renderer.GetSwapchainImages(targets.swapchain);
    targets.swapchainImageViews = renderer.GetSwapchainImageViews(targets.swapchain);

    RHITextureDesc depthDesc{};
    depthDesc.debugName = "Example.SceneDepth";
    depthDesc.dimension = RHITextureDimension::Texture2D;
    depthDesc.extent = {targets.extent.width, targets.extent.height, 1};
    depthDesc.format = RHIFormat::D32_Float;
    depthDesc.usage = RHITextureUsage::DepthStencilAttachment;
    targets.depthTexture = renderer.CreateTexture(depthDesc);

    RHITextureViewDesc depthViewDesc{};
    depthViewDesc.debugName = "Example.SceneDepthView";
    depthViewDesc.texture = targets.depthTexture;
    depthViewDesc.dimension = RHITextureViewDimension::View2D;
    depthViewDesc.aspect = RHITextureAspect::Depth;
    targets.depthView = renderer.CreateTextureView(depthViewDesc);

    return targets;
}

void DestroyFrameTargets(RHIDevice& renderer, FrameTargets& targets) {
    renderer.Destroy(targets.depthView);
    renderer.Destroy(targets.depthTexture);
    renderer.Destroy(targets.swapchain);
    targets = {};
}

ExampleResources createExampleResources(RHIDevice& renderer, RHIFormat colorFormat) {
    ExampleResources resources{};
    resources.cpuMesh = generatePlaneAndSphere();

    resources.vertexBuffer = CreateBuffer(
        renderer,
        "Example.GeometryVertices",
        sizeof(PbrVertex) * resources.cpuMesh.vertices.size(),
        RHIBufferUsage::Vertex | RHIBufferUsage::TransferDestination,
        RHIMemoryUsage::GpuOnly);
    resources.indexBuffer = CreateBuffer(
        renderer,
        "Example.GeometryIndices",
        sizeof(u32) * resources.cpuMesh.indices.size(),
        RHIBufferUsage::Index | RHIBufferUsage::TransferDestination,
        RHIMemoryUsage::GpuOnly);
    resources.sceneUniformBuffer = CreateBuffer(
        renderer,
        "Example.SceneUniforms",
        sizeof(SceneUniforms),
        RHIBufferUsage::Uniform | RHIBufferUsage::TransferDestination,
        RHIMemoryUsage::CpuToGpu);
    resources.lightUniformBuffer = CreateBuffer(
        renderer,
        "Example.LightUniforms",
        sizeof(LightUniforms),
        RHIBufferUsage::Uniform | RHIBufferUsage::TransferDestination,
        RHIMemoryUsage::CpuToGpu);

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

    resources.objectBuffer = CreateBuffer(
        renderer,
        "Example.ObjectBuffer",
        sizeof(ObjectGpuData) * resources.objectData.size(),
        RHIBufferUsage::Storage | RHIBufferUsage::TransferDestination,
        RHIMemoryUsage::GpuOnly);
    resources.materialBuffer = CreateBuffer(
        renderer,
        "Example.MaterialBuffer",
        sizeof(MaterialGpuData) * resources.materialData.size(),
        RHIBufferUsage::Storage | RHIBufferUsage::TransferDestination,
        RHIMemoryUsage::GpuOnly);

    RHITextureDesc shadowDesc{};
    shadowDesc.debugName = "Example.ShadowMap";
    shadowDesc.dimension = RHITextureDimension::Texture2D;
    shadowDesc.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
    shadowDesc.format = RHIFormat::D32_Float;
    shadowDesc.usage = RHITextureUsage::DepthStencilAttachment | RHITextureUsage::Sampled;
    resources.shadowMap = renderer.CreateTexture(shadowDesc);

    RHITextureViewDesc shadowViewDesc{};
    shadowViewDesc.debugName = "Example.ShadowMapView";
    shadowViewDesc.texture = resources.shadowMap;
    shadowViewDesc.dimension = RHITextureViewDimension::View2D;
    shadowViewDesc.aspect = RHITextureAspect::Depth;
    resources.shadowMapView = renderer.CreateTextureView(shadowViewDesc);

    RHISamplerDesc shadowSamplerDesc{};
    shadowSamplerDesc.debugName = "Example.ShadowCompareSampler";
    shadowSamplerDesc.minFilter = RHIFilterMode::Linear;
    shadowSamplerDesc.magFilter = RHIFilterMode::Linear;
    shadowSamplerDesc.mipmapMode = RHIMipmapMode::Nearest;
    shadowSamplerDesc.addressU = RHIAddressMode::ClampToBorder;
    shadowSamplerDesc.addressV = RHIAddressMode::ClampToBorder;
    shadowSamplerDesc.addressW = RHIAddressMode::ClampToBorder;
    shadowSamplerDesc.enableCompare = true;
    shadowSamplerDesc.compareOp = RHICompareOp::LessOrEqual;
    shadowSamplerDesc.borderColor = RHIBorderColor::OpaqueWhite;
    resources.shadowSampler = renderer.CreateSampler(shadowSamplerDesc);

    RHIBindSetLayoutDesc sceneLayout{};
    sceneLayout.debugName = "Example.SceneLayout";
    sceneLayout.set = 0;
    sceneLayout.entries = {
        {0, RHIBindingType::UniformBuffer, RHIShaderStage::Vertex | RHIShaderStage::Fragment, 1},
        {1, RHIBindingType::UniformBuffer, RHIShaderStage::Fragment, 1},
        {2, RHIBindingType::CombinedTextureSampler, RHIShaderStage::Fragment, 1, false, RHITextureViewDimension::View2D, RHITextureSampleType::Depth}
    };
    resources.sceneLayout = renderer.CreateBindSetLayout(sceneLayout);

    RHIBindSetLayoutDesc objectLayout{};
    objectLayout.debugName = "Example.ObjectLayout";
    objectLayout.set = 1;
    objectLayout.entries = {
        {0, RHIBindingType::StorageBuffer, RHIShaderStage::Vertex, 1, false},
        {1, RHIBindingType::StorageBuffer, RHIShaderStage::Fragment, 1, false}
    };
    resources.objectLayout = renderer.CreateBindSetLayout(objectLayout);

    RHIPipelineLayoutDesc pipelineLayoutDesc{};
    pipelineLayoutDesc.debugName = "Example.PipelineLayout";
    pipelineLayoutDesc.bindSetLayouts = {resources.sceneLayout, resources.objectLayout};
    resources.pipelineLayout = renderer.CreatePipelineLayout(pipelineLayoutDesc);

    RHIPipelineCacheDesc cacheDesc{};
    cacheDesc.debugName = "Example.PipelineCache";
    resources.pipelineCache = renderer.CreatePipelineCache(cacheDesc);

    RHIBindSetDesc sceneBindSet{};
    sceneBindSet.debugName = "Example.SceneBindSet";
    sceneBindSet.layout = resources.sceneLayout;
    sceneBindSet.bindings = {
        {0, 0, RHIBindingType::UniformBuffer, {resources.sceneUniformBuffer, 0, sizeof(SceneUniforms)}},
        {1, 0, RHIBindingType::UniformBuffer, {resources.lightUniformBuffer, 0, sizeof(LightUniforms)}},
        {2, 0, RHIBindingType::CombinedTextureSampler, {}, {resources.shadowMapView, resources.shadowMap}, resources.shadowSampler}
    };
    resources.sceneBindSet = renderer.CreateBindSet(sceneBindSet);

    RHIBindSetDesc objectBindSet{};
    objectBindSet.debugName = "Example.ObjectBindSet";
    objectBindSet.layout = resources.objectLayout;
    objectBindSet.bindings = {
        {0, 0, RHIBindingType::StorageBuffer, {resources.objectBuffer, 0, sizeof(ObjectGpuData) * resources.objectData.size()}},
        {1, 0, RHIBindingType::StorageBuffer, {resources.materialBuffer, 0, sizeof(MaterialGpuData) * resources.materialData.size()}}
    };
    resources.objectBindSet = renderer.CreateBindSet(objectBindSet);

    resources.pbrPipeline = renderer.CreateGraphicsPipeline(makePbrPipelineDesc(resources.pipelineLayout, resources.pipelineCache, colorFormat));
    resources.shadowPipeline = renderer.CreateGraphicsPipeline(makeShadowPipelineDesc(resources.pipelineLayout, resources.pipelineCache));

    resources.meshDesc.debugName = "Example.GeneratedPlaneAndSphere";
    resources.meshDesc.vertexStreams = {{resources.vertexBuffer, 0, 0, sizeof(PbrVertex)}};
    resources.meshDesc.indexStream = RHIIndexStream{resources.indexBuffer, RHIIndexType::UInt32, 0, static_cast<u32>(resources.cpuMesh.indices.size())};
    resources.meshDesc.submeshes = {resources.cpuMesh.planeSubmesh, resources.cpuMesh.sphereSubmesh};

    RHIMaterialDesc planeMaterial{};
    planeMaterial.debugName = "Example.PlaneMaterial";
    planeMaterial.pipeline = resources.pbrPipeline;
    planeMaterial.bindSets = {resources.sceneBindSet, resources.objectBindSet};
    planeMaterial.parameters = {
        {"baseColorFactor", RHIMaterialParameterType::Float4, resources.materialData[0].baseColor},
        {"metallic", RHIMaterialParameterType::Float, {resources.materialData[0].metallicRoughness.x, 0.0F, 0.0F, 0.0F}},
        {"roughness", RHIMaterialParameterType::Float, {resources.materialData[0].metallicRoughness.y, 0.0F, 0.0F, 0.0F}}
    };

    RHIMaterialDesc sphereMaterial{};
    sphereMaterial.debugName = "Example.SphereMaterial";
    sphereMaterial.pipeline = resources.pbrPipeline;
    sphereMaterial.bindSets = {resources.sceneBindSet, resources.objectBindSet};
    sphereMaterial.parameters = {
        {"baseColorFactor", RHIMaterialParameterType::Float4, resources.materialData[1].baseColor},
        {"metallic", RHIMaterialParameterType::Float, {resources.materialData[1].metallicRoughness.x, 0.0F, 0.0F, 0.0F}},
        {"roughness", RHIMaterialParameterType::Float, {resources.materialData[1].metallicRoughness.y, 0.0F, 0.0F, 0.0F}}
    };
    resources.materials = {planeMaterial, sphereMaterial};

    return resources;
}

void updateSceneUniforms(ExampleResources& resources, RHIExtent2D extent) {
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

RHIBufferUploadDesc makeUpload(RHIBuffer destination, std::vector<std::byte> data) {
    RHIBufferUploadDesc upload{};
    upload.destination = destination;
    upload.data = std::move(data);
    return upload;
}

RHIUploadBatchDesc makeUploadBatch(const ExampleResources& resources) {
    RHIUploadBatchDesc uploads{};
    uploads.buffers.push_back(makeUpload(resources.vertexBuffer, bytesFromVector(resources.cpuMesh.vertices)));
    uploads.buffers.push_back(makeUpload(resources.indexBuffer, bytesFromVector(resources.cpuMesh.indices)));
    uploads.buffers.push_back(makeUpload(resources.sceneUniformBuffer, bytesFromObject(resources.sceneUniforms)));
    uploads.buffers.push_back(makeUpload(resources.lightUniformBuffer, bytesFromObject(resources.lightUniforms)));
    uploads.buffers.push_back(makeUpload(resources.objectBuffer, bytesFromVector(resources.objectData)));
    uploads.buffers.push_back(makeUpload(resources.materialBuffer, bytesFromVector(resources.materialData)));
    return uploads;
}

RHIDrawIndexedCommand makeDraw(const ExampleResources& resources, const RHISubmeshDesc& submesh, RHIPipeline pipeline, u32 objectIndex) {
    RHIDrawIndexedCommand draw{};
    draw.pipeline = pipeline;
    draw.bindSets = {resources.sceneBindSet, resources.objectBindSet};
    draw.vertexStreams = resources.meshDesc.vertexStreams;
    draw.indexStream = *resources.meshDesc.indexStream;
    draw.indexCount = submesh.indexCount;
    draw.instanceCount = 1;
    draw.firstIndex = submesh.firstIndex;
    draw.vertexOffsetElements = 0;
    draw.firstInstance = objectIndex;
    return draw;
}

RHIRenderCameraSetDesc makeCameraSetDesc(const ExampleResources& resources) {
    RHIRenderCameraSetDesc cameras{};
    cameras.main.view = resources.sceneUniforms.view;
    cameras.main.projection = resources.sceneUniforms.projection;
    cameras.main.viewProjection = resources.sceneUniforms.viewProjection;
    cameras.main.position = glm::vec3(resources.sceneUniforms.cameraPosition);
    return cameras;
}

RHISceneEnvironmentDesc makeEnvironmentDesc(const ExampleResources& resources) {
    RHISceneEnvironmentDesc environment{};
    environment.ambientColor = glm::vec3(resources.sceneUniforms.ambientColorExposure);
    environment.exposure = resources.sceneUniforms.ambientColorExposure.w;
    return environment;
}

RHIRenderLightSetDesc makeLightSetDesc(const ExampleResources& resources) {
    RHIRenderLightSetDesc lights{};
    RHILightData light{};
    light.type = RHILightType::Directional;
    light.direction = glm::vec3(resources.lightUniforms.directionIntensity);
    light.color = glm::vec3(resources.lightUniforms.colorShadowBias);
    light.intensity = resources.lightUniforms.directionIntensity.w;
    lights.items.push_back(light);
    return lights;
}

RHIRenderObjectSetDesc makeObjectSetDesc(const ExampleResources& resources) {
    RHIRenderObjectSetDesc objects{};
    RHIRenderObjectDesc plane{};
    plane.debugName = "Plane";
    plane.mesh = RHIMesh(1);
    plane.material = RHIMaterial(1);
    plane.submeshIndex = 0;
    plane.transform.localToWorld = resources.objectData[0].localToWorld;
    plane.transform.previousLocalToWorld = resources.objectData[0].localToWorld;
    plane.worldBounds = resources.cpuMesh.planeSubmesh.boundsBox;
    plane.worldBoundsSphere = resources.cpuMesh.planeSubmesh.boundsSphere;

    RHIRenderObjectDesc sphere{};
    sphere.debugName = "Sphere";
    sphere.mesh = RHIMesh(1);
    sphere.material = RHIMaterial(2);
    sphere.submeshIndex = 1;
    sphere.transform.localToWorld = resources.objectData[1].localToWorld;
    sphere.transform.previousLocalToWorld = resources.objectData[1].localToWorld;
    sphere.worldBounds = resources.cpuMesh.sphereSubmesh.boundsBox;
    sphere.worldBoundsSphere = resources.cpuMesh.sphereSubmesh.boundsSphere;

    objects.items = {plane, sphere};
    return objects;
}

RHIFramePacket buildFramePacket(
    const ExampleResources& resources,
    const FrameTargets& targets,
    u32 imageIndex,
    RHIGPUWaitGPUSignal imageAvailable,
    RHIGPUWaitGPUSignal renderFinished,
    u64 frameIndex) {
    RHIFramePacket packet{};
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

    RHIRenderGraphTextureDesc shadowMap{};
    shadowMap.name = "ShadowMap";
    shadowMap.imported = true;
    shadowMap.externalHandle = resources.shadowMap;
    shadowMap.desc.debugName = "ShadowMap";
    shadowMap.desc.extent = {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 1};
    shadowMap.desc.format = RHIFormat::D32_Float;
    shadowMap.desc.usage = RHITextureUsage::DepthStencilAttachment | RHITextureUsage::Sampled;

    RHIRenderGraphTextureDesc sceneDepth{};
    sceneDepth.name = "SceneDepth";
    sceneDepth.imported = true;
    sceneDepth.externalHandle = targets.depthTexture;
    sceneDepth.desc.debugName = "SceneDepth";
    sceneDepth.desc.extent = {targets.extent.width, targets.extent.height, 1};
    sceneDepth.desc.format = RHIFormat::D32_Float;
    sceneDepth.desc.usage = RHITextureUsage::DepthStencilAttachment;

    RHIRenderGraphTextureDesc backBuffer{};
    backBuffer.name = "BackBuffer";
    backBuffer.imported = true;
    backBuffer.externalHandle = imageIndex < targets.swapchainImages.size() ? targets.swapchainImages[imageIndex] : RHITexture{};
    backBuffer.desc.debugName = "BackBuffer";
    backBuffer.desc.extent = {targets.extent.width, targets.extent.height, 1};
    backBuffer.desc.format = targets.colorFormat;
    backBuffer.desc.usage = RHITextureUsage::ColorAttachment | RHITextureUsage::Present;

    packet.graph.textures = {shadowMap, sceneDepth, backBuffer};
    packet.graph.passes = {
        {"ShadowPass", RHIRenderGraphPassType::Raster, RHIQueueType::Graphics, {}, {{"ShadowMap", RHIRenderGraphResourceType::Texture, RHIResourceState::DepthWrite}}, {}, RHIRenderGraphAttachmentDesc{"ShadowMap", RHITextureAspect::Depth}},
        {"PBRLightingPass", RHIRenderGraphPassType::Raster, RHIQueueType::Graphics, {{"ShadowMap", RHIRenderGraphResourceType::Texture, RHIResourceState::ShaderRead}}, {{"BackBuffer", RHIRenderGraphResourceType::SwapchainImage, RHIResourceState::RenderTarget}, {"SceneDepth", RHIRenderGraphResourceType::Texture, RHIResourceState::DepthWrite}}, {RHIRenderGraphAttachmentDesc{"BackBuffer", RHITextureAspect::Color}}, RHIRenderGraphAttachmentDesc{"SceneDepth", RHITextureAspect::Depth}},
        {"Present", RHIRenderGraphPassType::Present, RHIQueueType::Present, {{"BackBuffer", RHIRenderGraphResourceType::SwapchainImage, RHIResourceState::Present}}, {}, {}, std::nullopt, false, true}
    };

    RHIRenderPassWorkload shadowWorkload{};
    shadowWorkload.passName = "ShadowPass";
    shadowWorkload.viewport = {0.0F, 0.0F, static_cast<float>(SHADOW_MAP_SIZE), static_cast<float>(SHADOW_MAP_SIZE), 0.0F, 1.0F};
    shadowWorkload.scissor = {{0, 0}, {SHADOW_MAP_SIZE, SHADOW_MAP_SIZE}};
    shadowWorkload.indexedDraws = {
        makeDraw(resources, resources.cpuMesh.planeSubmesh, resources.shadowPipeline, 0),
        makeDraw(resources, resources.cpuMesh.sphereSubmesh, resources.shadowPipeline, 1)
    };

    RHIRenderPassWorkload pbrWorkload{};
    pbrWorkload.passName = "PBRLightingPass";
    pbrWorkload.viewport = packet.settings.viewport;
    pbrWorkload.scissor = packet.settings.scissor;
    pbrWorkload.indexedDraws = {
        makeDraw(resources, resources.cpuMesh.planeSubmesh, resources.pbrPipeline, 0),
        makeDraw(resources, resources.cpuMesh.sphereSubmesh, resources.pbrPipeline, 1)
    };
    packet.workloads = {shadowWorkload, pbrWorkload};

    RHIQueueSubmitDesc submit{};
    submit.debugName = "Example.FrameSubmit";
    submit.queue = RHIQueueType::Graphics;
    submit.passNames = {"ShadowPass", "PBRLightingPass"};
    submit.waits = {{imageAvailable, 0, RHIPipelineStage::ColorAttachmentOutput}};
    submit.signals = {{renderFinished, 0}};
    packet.submissions = {submit};

    RHIPresentDesc present{};
    present.swapchain = targets.swapchain;
    present.imageIndex = imageIndex;
    present.waitSignals = {renderFinished};
    present.presentMode = RHIPresentMode::FIFO;
    packet.present = present;
    return packet;
}

void DestroyExampleResources(RHIDevice& renderer, ExampleResources& resources) {
    renderer.Destroy(resources.pbrPipeline);
    renderer.Destroy(resources.shadowPipeline);
    renderer.Destroy(resources.pipelineCache);
    renderer.Destroy(resources.pipelineLayout);
    renderer.Destroy(resources.sceneBindSet);
    renderer.Destroy(resources.objectBindSet);
    renderer.Destroy(resources.sceneLayout);
    renderer.Destroy(resources.objectLayout);
    renderer.Destroy(resources.shadowSampler);
    renderer.Destroy(resources.shadowMapView);
    renderer.Destroy(resources.shadowMap);
    renderer.Destroy(resources.materialBuffer);
    renderer.Destroy(resources.objectBuffer);
    renderer.Destroy(resources.lightUniformBuffer);
    renderer.Destroy(resources.sceneUniformBuffer);
    renderer.Destroy(resources.indexBuffer);
    renderer.Destroy(resources.vertexBuffer);
    resources = {};
}

class GlfwScope {
public:
    GlfwScope() {
        if (glfwInit() != GLFW_TRUE) {
            throw std::runtime_error("glfwInit failed");
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
            throw std::runtime_error("Vulkan is not supported by the current system or GLFW");
        }

        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        GLFWwindow* window = glfwCreateWindow(1280, 720, "RHI PBR Shadow Example", nullptr, nullptr);
        if (window == nullptr) {
            throw std::runtime_error("glfwCreateWindow failed");
        }

        u32 extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (extensions == nullptr || extensionCount == 0) {
            throw std::runtime_error("GLFW did not return any required Vulkan instance extensions");
        }

        RHIDevice renderer(RHIGraphicsAPI::Vulkan);
        RHIDeviceCreateDesc rendererDesc{};
        rendererDesc.backend.applicationName = "RHI PBR Shadow Example";
        rendererDesc.backend.engineName = "VulkanLearn";
        rendererDesc.backend.validation = RHIValidationMode::Enabled;
        rendererDesc.backend.requiredFeatures = RHIRenderFeature::DynamicRendering;
        rendererDesc.backend.optionalFeatures = RHIRenderFeature::DebugMarkers | RHIRenderFeature::SamplerAnisotropy;
        rendererDesc.requiredVulkanInstanceExtensions.assign(extensions, extensions + extensionCount);
        rendererDesc.ownsVulkanSurface = true;
        rendererDesc.createVulkanSurface = [window](std::uintptr_t instanceValue) -> std::uintptr_t {
            VkSurfaceKHR surface = VK_NULL_HANDLE;
            if (glfwCreateWindowSurface(reinterpret_cast<VkInstance>(instanceValue), window, nullptr, &surface) != VK_SUCCESS) {
                return 0;
            }
            return reinterpret_cast<std::uintptr_t>(surface);
        };

        std::string errorMessage;
        if (!renderer.Initialize(rendererDesc, &errorMessage)) {
            throw std::runtime_error("RHIDevice initialization failed: " + errorMessage);
        }

        FrameTargets targets = createFrameTargets(renderer, waitForDrawableSize(window));
        ExampleResources resources = createExampleResources(renderer, targets.colorFormat);
        updateSceneUniforms(resources, targets.extent);

        // frames-in-flight:每个 slot 独立一对 imageAvailable / renderFinished semaphore,
        // 避免上一帧 GPU 还没用完就拿同一对 semaphore 去 acquire/present 导致 validation 报错。
        constexpr u32 kFrameSlotCount = 2;
        std::vector<RHIGPUWaitGPUSignal> imageAvailable(kFrameSlotCount);
        std::vector<RHIGPUWaitGPUSignal> renderFinished(kFrameSlotCount);
        std::vector<RHICPUWaitGPUSignal> inFlightFences(kFrameSlotCount);
        for (u32 slot = 0; slot < kFrameSlotCount; ++slot) {
            imageAvailable[slot] = renderer.CreateGPUWaitGPUSignal({
                "Example.ImageAvailable[" + std::to_string(slot) + "]", RHIGPUWaitGPUSignalType::Binary});
            renderFinished[slot] = renderer.CreateGPUWaitGPUSignal({
                "Example.RenderFinished[" + std::to_string(slot) + "]", RHIGPUWaitGPUSignalType::Binary});
            inFlightFences[slot] = renderer.CreateCPUWaitGPUSignal({
                "Example.InFlight[" + std::to_string(slot) + "]", true /*signaled*/});
        }

        u64 frameIndex = 0;
        while (!glfwWindowShouldClose(window) && (maxFrames == 0 || frameIndex < maxFrames)) {
            glfwPollEvents();

            const RHIExtent2D drawableSize = waitForDrawableSize(window);
            if (drawableSize.width != targets.extent.width || drawableSize.height != targets.extent.height) {
                renderer.WaitIdle();
                DestroyFrameTargets(renderer, targets);
                targets = createFrameTargets(renderer, drawableSize);
            }

            updateSceneUniforms(resources, targets.extent);

            const u32 slot = static_cast<u32>(frameIndex % kFrameSlotCount);

            // 等当前 slot 的 GPU 工作完成,确保 imageAvailable 上一帧已经被 GPU 消费。
            renderer.WaitForCPUSignal(inFlightFences[slot]);

            u32 imageIndex = 0;
            if (!renderer.AcquireNextImage(targets.swapchain, imageAvailable[slot], RHICPUWaitGPUSignal{}, &imageIndex, &errorMessage)) {
                std::cerr << "AcquireNextImage 失败: " << errorMessage << '\n';
                break;
            }

            RHIFramePacket packet = buildFramePacket(resources, targets, imageIndex, imageAvailable[slot], renderFinished[slot], frameIndex);
            packet.submissions[0].cpuWaitGPUSignal = inFlightFences[slot];
            ++frameIndex;
            // RHIFramePacket 将上传、阴影 Pass、PBR Pass、提交和呈现组织成一帧。
            if (!renderer.SubmitFrame(packet, &errorMessage)) {
                std::cerr << "SubmitFrame 失败: " << errorMessage << '\n';
                break;
            }
        }

        renderer.WaitIdle();
        for (u32 slot = 0; slot < kFrameSlotCount; ++slot) {
            renderer.Destroy(inFlightFences[slot]);
            renderer.Destroy(renderFinished[slot]);
            renderer.Destroy(imageAvailable[slot]);
        }
        DestroyExampleResources(renderer, resources);
        DestroyFrameTargets(renderer, targets);
        renderer.Shutdown();
        glfwDestroyWindow(window);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}


















