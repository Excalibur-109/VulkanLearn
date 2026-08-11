#pragma once

#include "RHI/RHIDevice.hpp"

#include <array>

namespace RHI
{

/** 定义金属度-粗糙度 PBR 网格顶点的固定输入布局。 */
struct RHIPBRVertex final
{
    /** 物体空间位置，对应顶点属性位置 0。 */
    std::array<float, 3> Position{};
    /** 物体空间单位法线，对应顶点属性位置 1。 */
    std::array<float, 3> Normal{};
    /** 纹理坐标，对应顶点属性位置 2。 */
    std::array<float, 2> TexCoord{};
};

/** 定义以行主序存储的 4x4 变换矩阵。 */
struct RHIPBRMatrix4x4 final
{
    /** 矩阵元素，索引为 row * 4 + column。 */
    std::array<float, 16> Values{};
};

/** 定义每次 PBR 绘制所需的紧凑推送常量。 */
struct alignas(16) RHIPBRDrawConstants final
{
    /** 从物体空间到齐次裁剪空间的行主序矩阵。 */
    RHIPBRMatrix4x4 ViewProjection{};
    /** 世界空间相机位置，第四个分量保留。 */
    std::array<float, 4> CameraPosition{};
    /** 线性基础色 RGB 与金属度。 */
    std::array<float, 4> BaseColorMetallic{1.0f, 1.0f, 1.0f, 0.0f};
    /** 粗糙度、环境遮蔽、直射光强度与保留分量。 */
    std::array<float, 4> RoughnessOcclusionIntensity{0.5f, 1.0f, 5.0f, 0.0f};
    /** 从表面指向光源的世界空间方向，第四个分量保留。 */
    std::array<float, 4> LightDirection{0.4f, 0.8f, 0.3f, 0.0f};
};

static_assert(sizeof(RHIPBRDrawConstants) == 128, "PBR 推送常量必须保持在所有目标后端保证的 128 字节范围内。");

/** 描述由调用方提供的、与具体后端匹配的 PBR 着色器代码。 */
struct RHIPBRShaderSet final
{
    /** 顶点阶段着色器模块创建参数。 */
    RHIShaderModuleDesc Vertex;
    /** 像素或片段阶段着色器模块创建参数。 */
    RHIShaderModuleDesc Fragment;
};

/** 描述共享 PBR 渲染器的不可变创建参数。 */
struct RHIPBRRendererDesc final
{
    /** 后端匹配的顶点和片段着色器。 */
    RHIPBRShaderSet Shaders;
    /** 输出颜色附件的格式。 */
    RHIFormat ColorFormat = RHIFormat::RGBA8_UNorm;
    /** 生成球体网格时的纵向分段数，最小值为 3。 */
    std::uint32_t LatitudeSegments = 32;
    /** 生成球体网格时的横向分段数，最小值为 3。 */
    std::uint32_t LongitudeSegments = 64;
};

/** 描述一帧离屏 PBR 绘制请求。 */
struct RHIPBRRenderDesc final
{
    /** 将接收 PBR 输出的颜色纹理视图。 */
    RHITextureViewHandle ColorTarget{};
    /** 输出区域的像素尺寸。 */
    RHIExtent2D Extent{};
    /** 本次绘制使用的相机、材质和光源数据。 */
    RHIPBRDrawConstants Constants{};
    /** 清除颜色，默认值为深蓝灰色。 */
    RHIClearColorValue ClearColor{0.015f, 0.02f, 0.035f, 1.0f};
};

/**
 * 使用统一 RHI 命令构建的金属度-粗糙度前向 PBR 渲染器。
 *
 * 渲染器生成一个带法线和纹理坐标的 UV 球，并执行包含 GGX 正态分布、
 * Smith 几何遮蔽和 Schlick 菲涅耳项的 Cook-Torrance BRDF。它不持有任何
 * 原生 API 对象，因此同一份前端代码可由 Vulkan、D3D12 和 D3D11 后端执行。
 */
class RHI_API RHIPBRRenderer final
{
public:
    /** 释放图形管线、着色器和网格缓冲区。 */
    ~RHIPBRRenderer();

    /** 禁止复制 GPU 资源所有权。 */
    RHIPBRRenderer(const RHIPBRRenderer&) = delete;
    /** 禁止复制 GPU 资源所有权。 */
    RHIPBRRenderer& operator=(const RHIPBRRenderer&) = delete;

    /** 创建共享球体网格、着色器模块和图形管线。 */
    static RHIStatus Create(RHIDevicePtr device, const RHIPBRRendererDesc& desc, std::unique_ptr<RHIPBRRenderer>& outRenderer);

    /** 录制、提交并等待一帧离屏 PBR 绘制。 */
    RHIStatus Render(const RHIPBRRenderDesc& desc);

private:
    /** 使用指定设备构造尚未初始化的渲染器。 */
    explicit RHIPBRRenderer(RHIDevicePtr device) noexcept;

    /** 创建并上传可复用的 UV 球顶点与索引缓冲区。 */
    RHIStatus CreateSphereMesh(std::uint32_t latitudeSegments, std::uint32_t longitudeSegments);
    /** 根据调用方提供的代码创建着色器与图形管线。 */
    RHIStatus CreatePipeline(const RHIPBRRendererDesc& desc);
    /** 释放已成功创建的 GPU 对象。 */
    void Release() noexcept;

    /** 创建这些资源的设备。 */
    RHIDevicePtr m_device;
    /** 球体顶点缓冲区。 */
    RHIBufferHandle m_vertexBuffer{};
    /** 球体索引缓冲区。 */
    RHIBufferHandle m_indexBuffer{};
    /** 球体索引数量。 */
    std::uint32_t m_indexCount = 0;
    /** 顶点着色器模块。 */
    RHIShaderModuleHandle m_vertexShader{};
    /** 片段或像素着色器模块。 */
    RHIShaderModuleHandle m_fragmentShader{};
    /** 金属度-粗糙度图形管线。 */
    RHIGraphicsPipelineHandle m_pipeline{};
};

/** 返回可供 D3D11 和 D3D12 编译的 HLSL 版 PBR 着色器源代码。 */
RHI_API std::string_view GetRHIPBRHlslSource() noexcept;

/** 使用内建 HLSL 源代码创建指定入口点的着色器模块描述。 */
RHI_API RHIShaderModuleDesc CreateRHIPBRHlslShaderDesc(std::string debugName, std::string entryPoint);

} // namespace RHI
