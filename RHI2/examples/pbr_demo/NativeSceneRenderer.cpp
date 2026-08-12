#include "NativeSceneRenderer.h"

#include "PlaneMesh.h"
#include "SphereMesh.h"

#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace pbrdemo {
namespace {

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

// 与 HLSL 中 row_major float4x4 配套的行主序矩阵。
struct Mat4 {
    float value[16]{};
};

Vec3 cross(Vec3 lhs, Vec3 rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x};
}

Mat4 identity() {
    Mat4 result{};
    result.value[0] = result.value[5] = result.value[10] = result.value[15] = 1.0f;
    return result;
}

Mat4 translation(Vec3 offset) {
    Mat4 result = identity();
    result.value[12] = offset.x;
    result.value[13] = offset.y;
    result.value[14] = offset.z;
    return result;
}

Mat4 multiply(const Mat4& lhs, const Mat4& rhs) {
    Mat4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int i = 0; i < 4; ++i) {
                result.value[row * 4 + column] +=
                    lhs.value[row * 4 + i] * rhs.value[i * 4 + column];
            }
        }
    }
    return result;
}

Mat4 lookAtRh(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 forward = normalize(eye - target);
    const Vec3 right = normalize(cross(up, forward));
    const Vec3 correctedUp = cross(forward, right);
    Mat4 result{};
    result.value[0] = right.x; result.value[1] = correctedUp.x; result.value[2] = forward.x;
    result.value[4] = right.y; result.value[5] = correctedUp.y; result.value[6] = forward.y;
    result.value[8] = right.z; result.value[9] = correctedUp.z; result.value[10] = forward.z;
    result.value[12] = -dot(right, eye);
    result.value[13] = -dot(correctedUp, eye);
    result.value[14] = -dot(forward, eye);
    result.value[15] = 1.0f;
    return result;
}

Mat4 perspectiveRh(float verticalFovRadians, float aspect, float nearPlane, float farPlane) {
    const float yScale = 1.0f / std::tan(verticalFovRadians * 0.5f);
    Mat4 result{};
    result.value[0] = yScale / aspect;
    result.value[5] = yScale;
    result.value[10] = farPlane / (nearPlane - farPlane);
    result.value[11] = -1.0f;
    result.value[14] = nearPlane * farPlane / (nearPlane - farPlane);
    return result;
}

Mat4 orthographicRh(float width, float height, float nearPlane, float farPlane) {
    Mat4 result{};
    result.value[0] = 2.0f / width;
    result.value[5] = 2.0f / height;
    result.value[10] = 1.0f / (nearPlane - farPlane);
    result.value[14] = nearPlane / (nearPlane - farPlane);
    result.value[15] = 1.0f;
    return result;
}

constexpr const char* shadowVertexShaderSource = R"(
cbuffer FrameConstants : register(b0) {
    row_major float4x4 world;
    row_major float4x4 viewProjection;
    row_major float4x4 lightViewProjection;
    float4 cameraPosition;
    float4 lightPosition;
    float4 albedoMetallic;
    float4 roughnessAmbient;
};
struct VSInput {
    float3 position : ATTRIBUTE0;
    float3 normal : ATTRIBUTE1;
    float2 uv : ATTRIBUTE2;
};
float4 main(VSInput input) : SV_POSITION {
    return mul(mul(float4(input.position, 1.0f), world), lightViewProjection);
}
)";

constexpr const char* lightingVertexShaderSource = R"(
cbuffer FrameConstants : register(b0) {
    row_major float4x4 world;
    row_major float4x4 viewProjection;
    row_major float4x4 lightViewProjection;
    float4 cameraPosition;
    float4 lightPosition;
    float4 albedoMetallic;
    float4 roughnessAmbient;
};
struct VSInput {
    float3 position : ATTRIBUTE0;
    float3 normal : ATTRIBUTE1;
    float2 uv : ATTRIBUTE2;
};
struct VSOutput {
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 lightClipPosition : TEXCOORD2;
};
VSOutput main(VSInput input) {
    VSOutput output;
    float4 worldPosition = mul(float4(input.position, 1.0f), world);
    output.position = mul(worldPosition, viewProjection);
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(mul(float4(input.normal, 0.0f), world).xyz);
    output.lightClipPosition = mul(worldPosition, lightViewProjection);
    return output;
}
)";

constexpr const char* lightingFragmentShaderSource = R"(
cbuffer FrameConstants : register(b0) {
    row_major float4x4 world;
    row_major float4x4 viewProjection;
    row_major float4x4 lightViewProjection;
    float4 cameraPosition;
    float4 lightPosition;
    float4 albedoMetallic;
    float4 roughnessAmbient;
};
Texture2D shadowTexture : register(t1);
SamplerState shadowSampler : register(s2);
struct PSInput {
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float4 lightClipPosition : TEXCOORD2;
};
float DistributionGGX(float nDotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float denominator = nDotH * nDotH * (a2 - 1.0f) + 1.0f;
    return a2 / (3.14159265f * denominator * denominator);
}
float GeometrySchlickGGX(float nDotV, float roughness) {
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    return nDotV / (nDotV * (1.0f - k) + k);
}
float GeometrySmith(float nDotV, float nDotL, float roughness) {
    return GeometrySchlickGGX(nDotV, roughness) * GeometrySchlickGGX(nDotL, roughness);
}
float3 FresnelSchlick(float cosine, float3 f0) {
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosine), 5.0f);
}
float4 main(PSInput input) : SV_TARGET {
    float3 normal = normalize(input.normal);
    float3 view = normalize(cameraPosition.xyz - input.worldPosition);
    float3 toLight = lightPosition.xyz - input.worldPosition;
    float lightDistance = length(toLight);
    float3 light = toLight / lightDistance;
    float3 halfVector = normalize(view + light);
    float nDotV = saturate(dot(normal, view));
    float nDotL = saturate(dot(normal, light));
    float nDotH = saturate(dot(normal, halfVector));
    float vDotH = saturate(dot(view, halfVector));
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedoMetallic.rgb, albedoMetallic.a);
    float3 fresnel = FresnelSchlick(vDotH, f0);
    float3 specular = fresnel * DistributionGGX(nDotH, roughnessAmbient.x) *
                      GeometrySmith(nDotV, nDotL, roughnessAmbient.x) /
                      max(4.0f * nDotV * nDotL, 0.001f);
    float3 diffuse = (1.0f - fresnel) * (1.0f - albedoMetallic.a) * albedoMetallic.rgb / 3.14159265f;
    float3 shadowCoordinate = input.lightClipPosition.xyz / input.lightClipPosition.w;
    float2 shadowUv = float2(shadowCoordinate.x * 0.5f + 0.5f,
                              -shadowCoordinate.y * 0.5f + 0.5f);
    float shadow = 1.0f;
    if (all(shadowUv >= 0.0f) && all(shadowUv <= 1.0f) && shadowCoordinate.z <= 1.0f) {
        float storedDepth = shadowTexture.Sample(shadowSampler, shadowUv).r;
        shadow = shadowCoordinate.z > storedDepth + 0.0025f ? 0.14f : 1.0f;
    }
    float attenuation = 20.0f / (lightDistance * lightDistance);
    float3 color = float3(0.025f, 0.03f, 0.04f) +
                   (diffuse + specular) * nDotL * attenuation * shadow;
    color = color / (color + 1.0f);
    color = pow(color, 1.0f / 2.2f);
    return float4(color, 1.0f);
}
)";

} // 匿名命名空间

struct NativeSceneRenderer::FrameConstants {
    Mat4 world;
    Mat4 viewProjection;
    Mat4 lightViewProjection;
    Vec4 cameraPosition;
    Vec4 lightPosition;
    Vec4 albedoMetallic;
    Vec4 roughnessAmbient;
};

static_assert(sizeof(NativeSceneRenderer::FrameConstants) % 16 == 0,
              "常量缓冲区大小必须是 16 字节的倍数");

NativeSceneRenderer::NativeSceneRenderer(rhi::Backend backend)
    : selectedBackend(backend) {}

bool NativeSceneRenderer::initialize(void* nativeWindow, rhi::Extent2D extent) {
    try {
        rhi::DeviceCreateInfo createInfo;
        createInfo.backend = selectedBackend;
        createInfo.extent = extent;
        createInfo.nativeWindow = nativeWindow;
        createInfo.enableDebugLayer = true;
        createInfo.enableVsync = true;
        createInfo.applicationName = "RHI PBR Native Scene";
        device = rhi::createDevice(createInfo);
        drawableExtent = extent;
        createResources();
        createPipelines();
        return device->currentColorTarget() != nullptr && device->currentDepthTarget() != nullptr;
    } catch (const std::exception& error) {
        // 此处不吞掉真实后端错误，窗口层会根据 false 结束消息循环。
        std::cerr << "初始化原生场景失败: " << error.what() << '\n';
        return false;
    }
}

void NativeSceneRenderer::resize(rhi::Extent2D extent) {
    if (device == nullptr || extent.width == 0 || extent.height == 0) return;
    if (device->resizeDrawable(extent)) drawableExtent = extent;
}

void NativeSceneRenderer::createResources() {
    std::vector<SphereVertex> sphereVertices;
    std::vector<uint32_t> sphereIndices;
    makeSphere(64, 32, sphereVertices, sphereIndices);
    sphereIndexCount = static_cast<uint32_t>(sphereIndices.size());

    std::vector<SphereVertex> groundVertices;
    std::vector<uint32_t> groundIndices;
    makeGroundPlane(7.0f, groundVertices, groundIndices);
    groundIndexCount = static_cast<uint32_t>(groundIndices.size());

    rhi::BufferDesc vertexDesc;
    vertexDesc.usage = rhi::ResourceUsage::Vertex;
    vertexDesc.stride = sizeof(SphereVertex);
    vertexDesc.size = sphereVertices.size() * sizeof(SphereVertex);
    vertexDesc.debugName = "PBR 球体顶点缓冲";
    sphereVertexBuffer = device->createBuffer(vertexDesc, sphereVertices.data());
    vertexDesc.size = groundVertices.size() * sizeof(SphereVertex);
    vertexDesc.debugName = "地面顶点缓冲";
    groundVertexBuffer = device->createBuffer(vertexDesc, groundVertices.data());

    rhi::BufferDesc indexDesc;
    indexDesc.usage = rhi::ResourceUsage::Index;
    indexDesc.indexFormat = rhi::IndexFormat::UInt32;
    indexDesc.size = sphereIndices.size() * sizeof(uint32_t);
    indexDesc.debugName = "PBR 球体索引缓冲";
    sphereIndexBuffer = device->createBuffer(indexDesc, sphereIndices.data());
    indexDesc.size = groundIndices.size() * sizeof(uint32_t);
    indexDesc.debugName = "地面索引缓冲";
    groundIndexBuffer = device->createBuffer(indexDesc, groundIndices.data());

    rhi::BufferDesc constantsDesc;
    constantsDesc.size = sizeof(FrameConstants);
    constantsDesc.usage = rhi::ResourceUsage::Uniform;
    constantsDesc.cpuVisible = true;
    constantsDesc.debugName = "每物体 PBR 常量";
    frameConstantBuffer = device->createBuffer(constantsDesc);

    rhi::TextureDesc shadowDesc;
    shadowDesc.extent = {2048, 2048};
    shadowDesc.format = rhi::Format::D32_Float;
    shadowDesc.usage = rhi::ResourceUsage::DepthStencil | rhi::ResourceUsage::Texture;
    shadowDesc.debugName = "球体阴影贴图";
    shadowMap = device->createTexture(shadowDesc);
    shadowSampler = device->createSampler({true, false, 1.0f});
}

void NativeSceneRenderer::createPipelines() {
    auto shadowVertex = device->createShader({
        rhi::ShaderStage::Vertex, "main", {}, shadowVertexShaderSource, "阴影顶点着色器"});
    auto lightingVertex = device->createShader({
        rhi::ShaderStage::Vertex, "main", {}, lightingVertexShaderSource, "PBR 顶点着色器"});
    auto lightingFragment = device->createShader({
        rhi::ShaderStage::Fragment, "main", {}, lightingFragmentShaderSource, "PBR 片元着色器"});

    const std::vector<rhi::VertexAttribute> attributes = {
        {0, 0, rhi::Format::RGB32_Float},
        {1, 12, rhi::Format::RGB32_Float},
        {2, 24, rhi::Format::RG32_Float}};

    rhi::PipelineDesc shadowDesc;
    shadowDesc.vertexShader = std::shared_ptr<rhi::Shader>(std::move(shadowVertex));
    shadowDesc.attributes = attributes;
    shadowDesc.colorFormat = rhi::Format::Unknown;
    shadowDesc.depthFormat = rhi::Format::D32_Float;
    shadowDesc.cullMode = rhi::CullMode::Back;
    shadowDesc.depthBiasConstant = 2.0f;
    shadowDesc.depthBiasSlope = 2.0f;
    shadowDesc.debugName = "阴影深度管线";
    shadowPipeline = device->createPipeline(shadowDesc);

    rhi::PipelineDesc lightingDesc;
    lightingDesc.vertexShader = std::shared_ptr<rhi::Shader>(std::move(lightingVertex));
    lightingDesc.fragmentShader = std::shared_ptr<rhi::Shader>(std::move(lightingFragment));
    lightingDesc.attributes = attributes;
    lightingDesc.bindings = {
        {0, rhi::BindingType::UniformBuffer, rhi::ShaderStage::Vertex},
        {1, rhi::BindingType::SampledTexture, rhi::ShaderStage::Fragment},
        {2, rhi::BindingType::Sampler, rhi::ShaderStage::Fragment}};
    lightingDesc.colorFormat = rhi::Format::BGRA8_UNorm;
    lightingDesc.depthFormat = rhi::Format::D32_Float;
    lightingDesc.cullMode = rhi::CullMode::Back;
    lightingDesc.debugName = "PBR 光照管线";
    lightingPipeline = device->createPipeline(lightingDesc);
}

void NativeSceneRenderer::updateConstants(bool sphere) {
    const float aspect = static_cast<float>(drawableExtent.width) /
                         static_cast<float>(drawableExtent.height);
    const Vec3 camera{0.0f, 2.8f, 7.0f};
    const Vec3 light{4.0f * std::cos(animationTime), 5.5f,
                     4.0f * std::sin(animationTime)};

    FrameConstants constants{};
    constants.world = sphere ? translation({0.0f, 1.0f, 0.0f}) : identity();
    constants.viewProjection = multiply(
        lookAtRh(camera, {0.0f, 0.8f, 0.0f}, {0.0f, 1.0f, 0.0f}),
        perspectiveRh(60.0f * 3.14159265f / 180.0f, aspect, 0.1f, 100.0f));
    constants.lightViewProjection = multiply(
        lookAtRh(light, {0.0f, 0.7f, 0.0f}, {0.0f, 1.0f, 0.0f}),
        orthographicRh(12.0f, 12.0f, 0.1f, 20.0f));
    constants.cameraPosition = {camera.x, camera.y, camera.z, 1.0f};
    constants.lightPosition = {light.x, light.y, light.z, 1.0f};
    if (sphere) {
        constants.albedoMetallic = {sphereMaterial.albedo.x, sphereMaterial.albedo.y,
                                    sphereMaterial.albedo.z, sphereMaterial.metallic};
        constants.roughnessAmbient = {sphereMaterial.roughness,
                                      sphereMaterial.ambientOcclusion, 0.0f, 0.0f};
    } else {
        constants.albedoMetallic = {0.42f, 0.45f, 0.5f, 0.0f};
        constants.roughnessAmbient = {0.82f, 1.0f, 0.0f, 0.0f};
    }
    if (!device->updateBuffer(*frameConstantBuffer, &constants, sizeof(constants))) {
        throw std::runtime_error("更新 PBR 常量缓冲失败");
    }
}

void NativeSceneRenderer::renderFrame() {
    if (device == nullptr || drawableExtent.width == 0 || drawableExtent.height == 0) return;
    animationTime += 0.01f;
    auto commands = device->createCommandList();
    commands->begin();

    // 第一通道只写入球体的光源空间深度。
    rhi::RenderPassDesc shadowPass;
    shadowPass.colorLoad = rhi::LoadOp::DontCare;
    shadowPass.depthLoad = rhi::LoadOp::Clear;
    shadowPass.depthFormat = rhi::Format::D32_Float;
    shadowPass.clear.depth = 1.0f;
    shadowPass.debugName = "阴影深度通道";
    commands->transition(shadowMap.get(), rhi::ResourceState::ShaderRead,
                         rhi::ResourceState::DepthWrite);
    commands->beginRenderPass(shadowPass, nullptr, shadowMap.get());
    commands->setPipeline(shadowPipeline.get());
    commands->setViewport(0.0f, 0.0f, 2048.0f, 2048.0f);
    commands->setScissor(0, 0, 2048, 2048);
    updateConstants(true);
    commands->setUniformBuffer(frameConstantBuffer.get(), 0);
    commands->setVertexBuffer(sphereVertexBuffer.get());
    commands->setIndexBuffer(sphereIndexBuffer.get());
    commands->drawIndexed(sphereIndexCount);
    commands->endRenderPass();
    commands->transition(shadowMap.get(), rhi::ResourceState::DepthWrite,
                         rhi::ResourceState::ShaderRead);

    // 第二通道绘制地面和球体，并在片元着色器中读取阴影贴图。
    rhi::RenderPassDesc mainPass;
    mainPass.colorLoad = rhi::LoadOp::Clear;
    mainPass.depthLoad = rhi::LoadOp::Clear;
    mainPass.clear.color = {0.025f, 0.03f, 0.04f, 1.0f};
    mainPass.clear.depth = 1.0f;
    mainPass.debugName = "PBR 主通道";
    commands->beginRenderPass(mainPass, device->currentColorTarget(), device->currentDepthTarget());
    commands->setPipeline(lightingPipeline.get());
    commands->setViewport(0.0f, 0.0f, static_cast<float>(drawableExtent.width),
                          static_cast<float>(drawableExtent.height));
    commands->setScissor(0, 0, drawableExtent.width, drawableExtent.height);
    commands->setTexture(shadowMap.get(), 1);
    commands->setSampler(shadowSampler.get(), 2);

    updateConstants(false);
    commands->setUniformBuffer(frameConstantBuffer.get(), 0);
    commands->setVertexBuffer(groundVertexBuffer.get());
    commands->setIndexBuffer(groundIndexBuffer.get());
    commands->drawIndexed(groundIndexCount);

    updateConstants(true);
    commands->setUniformBuffer(frameConstantBuffer.get(), 0);
    commands->setVertexBuffer(sphereVertexBuffer.get());
    commands->setIndexBuffer(sphereIndexBuffer.get());
    commands->drawIndexed(sphereIndexCount);
    commands->endRenderPass();
    commands->end();
    device->submit(*commands);
    device->present();
}

} // 命名空间 pbrdemo
