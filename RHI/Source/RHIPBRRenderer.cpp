#include "RHI/PBR/RHIPBRRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>

namespace RHI
{
namespace
{

/**
 * 将 CPU 容器中的数据创建为可直接写入的 GPU 缓冲区。
 *
 * 当前教学后端把 CpuToGpu 缓冲区映射为上传内存，因此网格创建阶段可以直接
 * 写入顶点和索引数据。真实引擎通常会把这一步封装为资源上传器，并在上传后
 * 转换到设备本地内存；这里保留直接路径，便于看清资源生命周期。
 */
template <typename T>
RHIStatus CreateUploadBuffer(IRHIDevice& device, std::string name, const RHIBufferUsage usage, const std::vector<T>& values, RHIBufferHandle& outBuffer)
{
    // 第 1 步：描述并创建缓冲区。
    RHIBufferDesc desc;
    desc.DebugName = std::move(name);
    desc.SizeInBytes = sizeof(T) * values.size();
    desc.Usage = usage;
    desc.MemoryUsage = RHIMemoryUsage::CpuToGpu;
    desc.InitialState = usage == RHIBufferUsage::Vertex ? RHIResourceState::VertexBuffer : RHIResourceState::IndexBuffer;
    RHIStatus status = device.CreateBuffer(desc, outBuffer);
    if (!status.Succeeded())
    {
        return status;
    }

    // 第 2 步：映射上传内存，将 CPU 数组复制到缓冲区。
    void* mapped = nullptr;
    status = device.MapBuffer(outBuffer, 0, desc.SizeInBytes, mapped);
    if (!status.Succeeded())
    {
        device.DestroyBuffer(outBuffer);
        outBuffer = {};
        return status;
    }
    std::memcpy(mapped, values.data(), static_cast<std::size_t>(desc.SizeInBytes));
    bool isMapped = true;

    // 第 3 步：让后端提交可见性，再解除映射。
    status = device.FlushMappedBufferRange(outBuffer, 0, desc.SizeInBytes);
    if (status.Succeeded())
    {
        status = device.UnmapBuffer(outBuffer);
        isMapped = false;
    }
    if (!status.Succeeded())
    {
        // 即使刷新失败，也先结束映射再释放资源，满足所有原生后端的生命周期要求。
        if (isMapped)
        {
            (void)device.UnmapBuffer(outBuffer);
        }
        device.DestroyBuffer(outBuffer);
        outBuffer = {};
    }
    return status;
}

} // namespace

RHIPBRRenderer::RHIPBRRenderer(RHIDevicePtr device) noexcept
    : m_device(std::move(device))
{
}

RHIPBRRenderer::~RHIPBRRenderer()
{
    Release();
}

RHIStatus RHIPBRRenderer::Create(RHIDevicePtr device, const RHIPBRRendererDesc& desc, std::unique_ptr<RHIPBRRenderer>& outRenderer)
{
    if (device == nullptr || desc.ColorFormat == RHIFormat::Unknown)
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "创建 PBR 渲染器需要有效设备和颜色格式。");
    }
    if (desc.Shaders.Vertex.Code.empty() || desc.Shaders.Fragment.Code.empty())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "创建 PBR 渲染器需要顶点和片段着色器代码。");
    }

    outRenderer = std::unique_ptr<RHIPBRRenderer>(new RHIPBRRenderer(std::move(device)));

    // 初始化顺序很重要：管线会在之后的绘制中引用这里创建的网格布局。
    RHIStatus status = outRenderer->CreateSphereMesh(std::max(3u, desc.LatitudeSegments), std::max(3u, desc.LongitudeSegments));
    if (status.Succeeded())
    {
        status = outRenderer->CreatePipeline(desc);
    }
    if (!status.Succeeded())
    {
        outRenderer.reset();
    }
    return status;
}

RHIStatus RHIPBRRenderer::CreateSphereMesh(const std::uint32_t latitudeSegments, const std::uint32_t longitudeSegments)
{
    // 一个纬线环上的点数比经线分段多 1，用于在纹理接缝处重复 U=0 和 U=1 的顶点。
    std::vector<RHIPBRVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(static_cast<std::size_t>(latitudeSegments + 1) * (longitudeSegments + 1));
    indices.reserve(static_cast<std::size_t>(latitudeSegments) * longitudeSegments * 6);

    // 第 1 步：生成球面顶点。单位球的位置可以直接作为光照所需的法线。
    for (std::uint32_t latitude = 0; latitude <= latitudeSegments; ++latitude)
    {
        const float v = static_cast<float>(latitude) / static_cast<float>(latitudeSegments);
        const float theta = v * std::numbers::pi_v<float>;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        for (std::uint32_t longitude = 0; longitude <= longitudeSegments; ++longitude)
        {
            const float u = static_cast<float>(longitude) / static_cast<float>(longitudeSegments);
            const float phi = u * std::numbers::pi_v<float> * 2.0f;
            const float x = sinTheta * std::cos(phi);
            const float y = cosTheta;
            const float z = sinTheta * std::sin(phi);
            vertices.push_back({{x, y, z}, {x, y, z}, {u, v}});
        }
    }
    // 第 2 步：每个经纬四边形拆成两个三角形，索引按逆时针顺序排列。
    for (std::uint32_t latitude = 0; latitude < latitudeSegments; ++latitude)
    {
        for (std::uint32_t longitude = 0; longitude < longitudeSegments; ++longitude)
        {
            const std::uint32_t row = longitudeSegments + 1;
            const std::uint32_t topLeft = latitude * row + longitude;
            const std::uint32_t bottomLeft = (latitude + 1) * row + longitude;
            indices.insert(indices.end(), {topLeft, bottomLeft, topLeft + 1, topLeft + 1, bottomLeft, bottomLeft + 1});
        }
    }

    // 第 3 步：分别上传顶点与索引。两个资源的失败处理互不隐藏。
    RHIStatus status = CreateUploadBuffer(*m_device, "PBR.UVSphere.Vertices", RHIBufferUsage::Vertex, vertices, m_vertexBuffer);
    if (!status.Succeeded())
    {
        return status;
    }
    status = CreateUploadBuffer(*m_device, "PBR.UVSphere.Indices", RHIBufferUsage::Index, indices, m_indexBuffer);
    if (!status.Succeeded())
    {
        m_device->DestroyBuffer(m_vertexBuffer);
        m_vertexBuffer = {};
        return status;
    }
    m_indexCount = static_cast<std::uint32_t>(indices.size());
    return RHIStatus::Ok();
}

RHIStatus RHIPBRRenderer::CreatePipeline(const RHIPBRRendererDesc& desc)
{
    // 着色器模块由 RHI 创建；具体后端决定输入是 HLSL、SPIR-V 还是其他代码格式。
    RHIStatus status = m_device->CreateShaderModule(desc.Shaders.Vertex, m_vertexShader);
    if (!status.Succeeded())
    {
        return status;
    }
    status = m_device->CreateShaderModule(desc.Shaders.Fragment, m_fragmentShader);
    if (!status.Succeeded())
    {
        m_device->DestroyShaderModule(m_vertexShader);
        m_vertexShader = {};
        return status;
    }

    // 图形管线把“顶点内存长什么样”和“要写入哪种颜色格式”固定下来。
    RHIGraphicsPipelineDesc pipelineDesc;
    pipelineDesc.DebugName = "PBR.MetallicRoughness";
    pipelineDesc.ShaderStages = {
        {m_vertexShader, RHIShaderStage::Vertex, desc.Shaders.Vertex.EntryPoint},
        {m_fragmentShader, RHIShaderStage::Pixel, desc.Shaders.Fragment.EntryPoint},
    };
    pipelineDesc.VertexBuffers = {{
        static_cast<std::uint32_t>(sizeof(RHIPBRVertex)),
        RHIVertexStepMode::PerVertex,
        {{0, RHIVertexFormat::Float3, 0}, {1, RHIVertexFormat::Float3, 12}, {2, RHIVertexFormat::Float2, 24}},
    }};
    pipelineDesc.Topology = RHIPrimitiveTopology::TriangleList;
    pipelineDesc.Rasterizer.CullMode = RHICullMode::Back;
    pipelineDesc.Rasterizer.FrontFace = RHIFrontFace::CounterClockwise;
    pipelineDesc.DepthStencil.DepthTestEnable = false;
    pipelineDesc.DepthStencil.DepthWriteEnable = false;
    pipelineDesc.Blend.Attachments = {RHIColorBlendAttachmentState{}};
    pipelineDesc.RenderTargets.ColorFormats = {desc.ColorFormat};
    pipelineDesc.DynamicStates = {RHIDynamicState::Viewport, RHIDynamicState::Scissor};
    return m_device->CreateGraphicsPipeline(pipelineDesc, m_pipeline);
}

RHIStatus RHIPBRRenderer::Render(const RHIPBRRenderDesc& desc)
{
    if (!desc.ColorTarget.IsValid() || !desc.Extent.IsValid())
    {
        return RHIStatus::Error(RHIResult::InvalidArgument, "PBR 绘制需要有效颜色目标和非零输出尺寸。");
    }

    // 1. 创建并开始录制图形命令列表。
    RHICommandListPtr commandList;
    RHIStatus status = m_device->CreateCommandList({"PBR.Forward", RHIQueueType::Graphics, RHICommandListLevel::Primary}, commandList);
    if (!status.Succeeded())
    {
        return status;
    }
    status = commandList->Begin();
    if (!status.Succeeded())
    {
        return status;
    }

    // 2. 指定本帧的颜色输出，并清除上一帧可能留下的内容。
    RHIRenderingDesc rendering;
    rendering.RenderArea = {0, 0, desc.Extent.Width, desc.Extent.Height};
    rendering.ColorAttachments = {{desc.ColorTarget, {}, RHIResolveMode::Average, RHILoadOp::Clear, RHIStoreOp::Store, desc.ClearColor}};
    status = commandList->BeginRendering(rendering);
    if (!status.Succeeded())
    {
        return status;
    }

    // 3. 绑定管线和动态状态。管线定义布局，动态状态定义这次绘制的输出区域。
    status = commandList->BindGraphicsPipeline(m_pipeline);
    if (!status.Succeeded())
    {
        return status;
    }

    const RHIViewport viewport{0.0f, 0.0f, static_cast<float>(desc.Extent.Width), static_cast<float>(desc.Extent.Height), 0.0f, 1.0f};
    const RHIScissor scissor{0, 0, desc.Extent.Width, desc.Extent.Height};
    const RHIVertexBufferBinding vertexBinding{m_vertexBuffer, 0};
    const RHIIndexBufferBinding indexBinding{m_indexBuffer, 0, RHIIndexType::UInt32};
    const auto constants = std::span<const std::byte>(reinterpret_cast<const std::byte*>(&desc.Constants), sizeof(desc.Constants));

    status = commandList->SetViewports(0, std::span(&viewport, 1));
    if (!status.Succeeded())
    {
        return status;
    }
    status = commandList->SetScissors(0, std::span(&scissor, 1));
    if (!status.Succeeded())
    {
        return status;
    }

    // 4. 绑定本次 PBR 绘制的材质/相机常量和球体网格，然后发出索引绘制。
    status = commandList->PushConstants(RHIShaderStage::AllGraphics, 0, constants);
    if (!status.Succeeded())
    {
        return status;
    }
    status = commandList->BindVertexBuffers(0, std::span(&vertexBinding, 1));
    if (!status.Succeeded())
    {
        return status;
    }
    status = commandList->BindIndexBuffer(indexBinding);
    if (!status.Succeeded())
    {
        return status;
    }
    status = commandList->DrawIndexed(m_indexCount);
    if (!status.Succeeded())
    {
        return status;
    }

    // 5. 关闭渲染区域和命令列表，令它进入可提交状态。
    status = commandList->EndRendering();
    if (!status.Succeeded())
    {
        return status;
    }
    status = commandList->End();
    if (!status.Succeeded())
    {
        return status;
    }

    // 6. 提交到图形队列。调用方可按自身帧调度策略决定何时等待。
    return m_device->Submit(RHIQueueType::Graphics, {{commandList}, {}, {}, {}});
}

void RHIPBRRenderer::Release() noexcept
{
    if (m_device == nullptr) return;
    (void)m_device->WaitIdle();
    if (m_pipeline.IsValid()) m_device->DestroyGraphicsPipeline(m_pipeline);
    if (m_vertexShader.IsValid()) m_device->DestroyShaderModule(m_vertexShader);
    if (m_fragmentShader.IsValid()) m_device->DestroyShaderModule(m_fragmentShader);
    if (m_vertexBuffer.IsValid()) m_device->DestroyBuffer(m_vertexBuffer);
    if (m_indexBuffer.IsValid()) m_device->DestroyBuffer(m_indexBuffer);
    m_pipeline = {};
    m_vertexShader = {};
    m_fragmentShader = {};
    m_vertexBuffer = {};
    m_indexBuffer = {};
}

std::string_view GetRHIPBRHlslSource() noexcept
{
    return R"HLSL(
cbuffer PBRConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 CameraPosition;
    float4 BaseColorMetallic;
    float4 RoughnessOcclusionIntensity;
    float4 LightDirection;
};

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 Position : SV_Position;
    float3 WorldPosition : TEXCOORD0;
    float3 Normal : TEXCOORD1;
};

VertexOutput PBRVertexMain(VertexInput input)
{
    VertexOutput output;
    output.Position = mul(float4(input.Position, 1.0), ViewProjection);
    output.WorldPosition = input.Position;
    output.Normal = normalize(input.Normal);
    return output;
}

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float nDotH = max(dot(normal, halfVector), 0.0);
    float nDotH2 = nDotH * nDotH;
    float denominator = nDotH2 * (a2 - 1.0) + 1.0;
    return a2 / max(3.14159265 * denominator * denominator, 0.0001);
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nDotV / max(nDotV * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    return GeometrySchlickGGX(max(dot(normal, viewDirection), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(normal, lightDirection), 0.0), roughness);
}

float3 FresnelSchlick(float cosine, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - cosine, 5.0);
}

float4 PBRFragmentMain(VertexOutput input) : SV_Target0
{
    float3 normal = normalize(input.Normal);
    float3 viewDirection = normalize(CameraPosition.xyz - input.WorldPosition);
    float3 lightDirection = normalize(LightDirection.xyz);
    float3 halfVector = normalize(viewDirection + lightDirection);
    float roughness = clamp(RoughnessOcclusionIntensity.x, 0.045, 1.0);
    float metallic = saturate(BaseColorMetallic.w);
    float3 baseColor = max(BaseColorMetallic.rgb, 0.0);
    float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float ndf = DistributionGGX(normal, halfVector, roughness);
    float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    float3 fresnel = FresnelSchlick(max(dot(halfVector, viewDirection), 0.0), f0);
    float3 specular = (ndf * geometry * fresnel) /
        max(4.0 * max(dot(normal, viewDirection), 0.0) * max(dot(normal, lightDirection), 0.0), 0.0001);
    float3 kd = (1.0 - fresnel) * (1.0 - metallic);
    float nDotL = max(dot(normal, lightDirection), 0.0);
    float3 radiance = float3(1.0, 0.98, 0.92) * RoughnessOcclusionIntensity.z;
    float3 direct = (kd * baseColor / 3.14159265 + specular) * radiance * nDotL;
    float3 ambient = baseColor * (0.025 + 0.12 * max(normal.y, 0.0)) * RoughnessOcclusionIntensity.y;
    float3 color = ambient + direct;
    color = color / (color + 1.0);
    color = pow(color, 1.0 / 2.2);
    return float4(color, 1.0);
}
)HLSL";
}

RHIShaderModuleDesc CreateRHIPBRHlslShaderDesc(std::string debugName, std::string entryPoint)
{
    const std::string_view source = GetRHIPBRHlslSource();
    RHIShaderModuleDesc desc;
    desc.DebugName = std::move(debugName);
    desc.CodeFormat = RHIShaderCodeFormat::SourceText;
    desc.EntryPoint = std::move(entryPoint);
    desc.Code.resize(source.size());
    std::memcpy(desc.Code.data(), source.data(), source.size());
    return desc;
}

} // namespace RHI
