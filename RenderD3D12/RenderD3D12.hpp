#pragma once

#include "../RenderDefinitions.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#include <memory>
#include <string>
#include <vector>

struct D3D12SurfaceDesc {
    HWND hwnd = nullptr;
};

struct D3D12RendererDesc {
    RenderBackendDesc backend{};
    D3D12SurfaceDesc surface{};
    D3D_FEATURE_LEVEL minimumFeatureLevel = D3D_FEATURE_LEVEL_11_0;
    bool allowWarpFallback = true;
};

struct D3D12NativeHandles {
    IDXGIFactory6* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    ID3D12Device* device = nullptr;
    ID3D12CommandQueue* graphicsQueue = nullptr;
    ID3D12CommandAllocator* commandAllocator = nullptr;
    ID3D12GraphicsCommandList* commandList = nullptr;
    ID3D12Fence* fence = nullptr;
    UINT64 fenceValue = 0;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
    HWND hwnd = nullptr;
};

class D3D12Renderer {
public:
    D3D12Renderer();
    ~D3D12Renderer();

    D3D12Renderer(const D3D12Renderer&) = delete;
    D3D12Renderer& operator=(const D3D12Renderer&) = delete;
    D3D12Renderer(D3D12Renderer&&) noexcept;
    D3D12Renderer& operator=(D3D12Renderer&&) noexcept;

    [[nodiscard]] bool initialize(const D3D12RendererDesc& desc, std::string* errorMessage = nullptr);
    void shutdown() noexcept;

    [[nodiscard]] bool isInitialized() const noexcept;
    [[nodiscard]] const RenderCapabilities& capabilities() const noexcept;
    [[nodiscard]] const D3D12NativeHandles& nativeHandles() const noexcept;

    [[nodiscard]] BufferHandle createBuffer(const BufferDesc& desc);
    [[nodiscard]] TextureHandle createTexture(const TextureDesc& desc);
    [[nodiscard]] TextureViewHandle createTextureView(const TextureViewDesc& desc);
    [[nodiscard]] SamplerHandle createSampler(const SamplerDesc& desc);
    [[nodiscard]] ShaderHandle createShaderModule(const ShaderDesc& desc);
    [[nodiscard]] BindGroupLayoutHandle createBindGroupLayout(const BindGroupLayoutDesc& desc);
    [[nodiscard]] BindGroupHandle createBindGroup(const BindGroupDesc& desc);
    [[nodiscard]] PipelineLayoutHandle createPipelineLayout(const PipelineLayoutDesc& desc);
    [[nodiscard]] PipelineCacheHandle createPipelineCache(const PipelineCacheDesc& desc);
    [[nodiscard]] PipelineHandle createGraphicsPipeline(const GraphicsPipelineDesc& desc);
    [[nodiscard]] PipelineHandle createComputePipeline(const ComputePipelineDesc& desc);
    [[nodiscard]] QueryPoolHandle createQueryPool(const QueryPoolDesc& desc);
    [[nodiscard]] SemaphoreHandle createSemaphore(const SemaphoreDesc& desc);
    [[nodiscard]] FenceHandle createFence(const FenceDesc& desc);
    [[nodiscard]] SwapchainHandle createSwapchain(const SwapchainDesc& desc);

    [[nodiscard]] std::vector<TextureHandle> getSwapchainImages(SwapchainHandle handle) const;
    [[nodiscard]] std::vector<TextureViewHandle> getSwapchainImageViews(SwapchainHandle handle) const;
    [[nodiscard]] Format getSwapchainFormat(SwapchainHandle handle) const;
    [[nodiscard]] Extent2D getSwapchainExtent(SwapchainHandle handle) const;

    [[nodiscard]] bool acquireNextImage(
        SwapchainHandle swapchain,
        SemaphoreHandle signalSemaphore,
        FenceHandle signalFence,
        u32* imageIndex,
        std::string* errorMessage = nullptr);

    [[nodiscard]] bool submit(const QueueSubmitDesc& desc, std::string* errorMessage = nullptr);
    [[nodiscard]] bool present(const PresentDesc& desc, std::string* errorMessage = nullptr);
    [[nodiscard]] bool submitFrame(const FramePacket& packet, std::string* errorMessage = nullptr);

    void waitIdle() const noexcept;

    void destroy(BufferHandle handle) noexcept;
    void destroy(TextureHandle handle) noexcept;
    void destroy(TextureViewHandle handle) noexcept;
    void destroy(SamplerHandle handle) noexcept;
    void destroy(ShaderHandle handle) noexcept;
    void destroy(BindGroupLayoutHandle handle) noexcept;
    void destroy(BindGroupHandle handle) noexcept;
    void destroy(PipelineLayoutHandle handle) noexcept;
    void destroy(PipelineCacheHandle handle) noexcept;
    void destroy(PipelineHandle handle) noexcept;
    void destroy(QueryPoolHandle handle) noexcept;
    void destroy(SemaphoreHandle handle) noexcept;
    void destroy(FenceHandle handle) noexcept;
    void destroy(SwapchainHandle handle) noexcept;

private:
    [[nodiscard]] bool recordAndSubmitFrame(const FramePacket& packet, std::string* errorMessage);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
