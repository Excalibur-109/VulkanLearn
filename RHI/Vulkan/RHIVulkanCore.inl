#pragma once

#include "RHIVulkanPrivate.inl"

namespace rhi {

RHIVulkan::RHIVulkan()
    : impl_(std::make_unique<Impl>()) {
}

RHIVulkan::~RHIVulkan() {
    shutdown();
}

RHIVulkan::RHIVulkan(RHIVulkan&&) noexcept = default;

RHIVulkan& RHIVulkan::operator=(RHIVulkan&&) noexcept = default;

// 初始化 Vulkan 后端的主流程：
// 1. 收集 layer/extension，创建 VkInstance 和可选 debug messenger；
// 2. 创建/接收 VkSurfaceKHR，用于后续 swapchain 和 present queue 查询；
// 3. 枚举物理设备并打分，选出满足 requiredFeatures 的 GPU；
// 4. 创建 logical device，启用需要的 feature/extension，并取出各类队列；
// 5. 创建 command pool、descriptor pool，并把设备限制整理成 RHICapabilities。
bool RHIVulkan::initialize(const RHIVulkanDesc& desc, std::string* errorMessage) {
    try {
        if (isInitialized()) {
            shutdown();
        }

        impl_ = std::make_unique<Impl>();
        impl_->initDesc = desc;
        impl_->native.surface = desc.surface.surface;
        impl_->ownsSurface = desc.surface.ownsSurface;

        const bool wantsValidation = desc.backend.validation != RHIValidationMode::Disabled;
        const RHIRenderFeature requestedFeatures = desc.backend.optionalFeatures | desc.backend.requiredFeatures;
        const bool wantsDebugUtils = wantsValidation || RHIHasAny(requestedFeatures, RHIRenderFeature::DebugMarkers);
        const auto availableLayers = enumerateInstanceLayers();
        const auto availableExtensions = enumerateInstanceExtensions();

        std::vector<const char*> layers;
        if (wantsValidation && hasLayer(availableLayers, "VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
        }

        std::vector<const char*> instanceExtensions;
        for (const char* extension : desc.requiredInstanceExtensions) {
            if (!hasExtension(availableExtensions, extension)) {
                throw std::runtime_error(std::string("Missing Vulkan instance extension: ") + extension);
            }
            appendUniqueExtension(instanceExtensions, extension);
        }
        for (const char* extension : desc.optionalInstanceExtensions) {
            if (hasExtension(availableExtensions, extension)) {
                appendUniqueExtension(instanceExtensions, extension);
            }
        }
        if (wantsDebugUtils && hasExtension(availableExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            appendUniqueExtension(instanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else if (RHIHasAny(desc.backend.requiredFeatures, RHIRenderFeature::DebugMarkers)) {
            throw std::runtime_error("Missing Vulkan instance extension: VK_EXT_debug_utils");
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = desc.backend.applicationName.empty() ? "VulkanLearn" : desc.backend.applicationName.c_str();
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = desc.backend.engineName.c_str();
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = makeDebugMessengerCreateInfo();

        VkInstanceCreateInfo instanceInfo{};
        instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceInfo.pApplicationInfo = &appInfo;
        instanceInfo.enabledLayerCount = static_cast<u32>(layers.size());
        instanceInfo.ppEnabledLayerNames = layers.data();
        instanceInfo.enabledExtensionCount = static_cast<u32>(instanceExtensions.size());
        instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
        if (wantsValidation) {
            instanceInfo.pNext = &debugCreateInfo;
        }

        if (vkCreateInstance(&instanceInfo, nullptr, &impl_->native.instance) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateInstance failed");
        }

        auto createDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(impl_->native.instance, "vkCreateDebugUtilsMessengerEXT"));
        if (wantsValidation && createDebugMessenger != nullptr) {
            createDebugMessenger(impl_->native.instance, &debugCreateInfo, nullptr, &impl_->debugMessenger);
        }

        if (impl_->native.surface == VK_NULL_HANDLE && desc.surface.createSurface) {
            // GLFW 等窗口库必须等 VkInstance 创建后才能创建 VkSurfaceKHR。
            impl_->native.surface = desc.surface.createSurface(impl_->native.instance);
            if (impl_->native.surface == VK_NULL_HANDLE) {
                throw std::runtime_error("The Vulkan surface factory returned a null VkSurfaceKHR");
            }
        }

        u32 physicalDeviceCount = 0;
        vkEnumeratePhysicalDevices(impl_->native.instance, &physicalDeviceCount, nullptr);
        if (physicalDeviceCount == 0) {
            throw std::runtime_error("No Vulkan physical device was found");
        }

        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        vkEnumeratePhysicalDevices(impl_->native.instance, &physicalDeviceCount, physicalDevices.data());

        int bestScore = -1;
        for (VkPhysicalDevice device : physicalDevices) {
            const int score = scorePhysicalDevice(device, impl_->native.surface, desc);
            if (score > bestScore) {
                bestScore = score;
                impl_->native.physicalDevice = device;
            }
        }
        if (impl_->native.physicalDevice == VK_NULL_HANDLE) {
            throw std::runtime_error("No Vulkan physical device satisfies the required capabilities");
        }

        impl_->queueFamilies = findQueueFamilies(impl_->native.physicalDevice, impl_->native.surface);

        std::set<u32> uniqueFamilies = {
            impl_->queueFamilies.graphics,
            impl_->queueFamilies.compute,
            impl_->queueFamilies.transfer,
            impl_->queueFamilies.present
        };
        uniqueFamilies.erase(RHI_INVALID_INDEX);

        const float queuePriority = 1.0F;
        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        for (u32 family : uniqueFamilies) {
            VkDeviceQueueCreateInfo queueInfo{};
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = family;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &queuePriority;
            queueInfos.push_back(queueInfo);
        }

        const auto availableDeviceExtensions = enumerateDeviceExtensions(impl_->native.physicalDevice);
        std::vector<const char*> deviceExtensions;
        for (const char* extension : desc.requiredDeviceExtensions) {
            if (!hasExtension(availableDeviceExtensions, extension)) {
                throw std::runtime_error(std::string("Missing Vulkan device extension: ") + extension);
            }
            appendUniqueExtension(deviceExtensions, extension);
        }
        for (const char* extension : desc.optionalDeviceExtensions) {
            if (hasExtension(availableDeviceExtensions, extension)) {
                appendUniqueExtension(deviceExtensions, extension);
            }
        }
        if (impl_->native.surface != VK_NULL_HANDLE) {
            appendUniqueExtension(deviceExtensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
        }

        const VulkanDeviceSupport support = queryVulkanDeviceSupport(impl_->native.physicalDevice);

        VkPhysicalDeviceFeatures enabledFeatures{};
        enabledFeatures.samplerAnisotropy = support.features.samplerAnisotropy;
        enabledFeatures.geometryShader = support.features.geometryShader && RHIHasAny(desc.backend.optionalFeatures | desc.backend.requiredFeatures, RHIRenderFeature::GeometryShader);
        enabledFeatures.tessellationShader = support.features.tessellationShader && RHIHasAny(desc.backend.optionalFeatures | desc.backend.requiredFeatures, RHIRenderFeature::Tessellation);

        VkPhysicalDeviceVulkan12Features enabled12{};
        enabled12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        enabled12.timelineSemaphore = support.features12.timelineSemaphore;
        enabled12.drawIndirectCount = support.features12.drawIndirectCount;

        VkPhysicalDeviceVulkan13Features enabled13{};
        enabled13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        enabled13.dynamicRendering = support.features13.dynamicRendering;
        enabled13.synchronization2 = support.features13.synchronization2;
        enabled12.pNext = &enabled13;

        VkDeviceCreateInfo deviceInfo{};
        deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceInfo.pNext = &enabled12;
        deviceInfo.queueCreateInfoCount = static_cast<u32>(queueInfos.size());
        deviceInfo.pQueueCreateInfos = queueInfos.data();
        deviceInfo.enabledExtensionCount = static_cast<u32>(deviceExtensions.size());
        deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
        deviceInfo.pEnabledFeatures = &enabledFeatures;

        if (vkCreateDevice(impl_->native.physicalDevice, &deviceInfo, nullptr, &impl_->native.device) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDevice failed");
        }

        vkGetDeviceQueue(impl_->native.device, impl_->queueFamilies.graphics, 0, &impl_->native.graphicsQueue);
        if (impl_->queueFamilies.compute != RHI_INVALID_INDEX) {
            vkGetDeviceQueue(impl_->native.device, impl_->queueFamilies.compute, 0, &impl_->native.computeQueue);
        }
        if (impl_->queueFamilies.transfer != RHI_INVALID_INDEX) {
            vkGetDeviceQueue(impl_->native.device, impl_->queueFamilies.transfer, 0, &impl_->native.transferQueue);
        }
        if (impl_->queueFamilies.present != RHI_INVALID_INDEX) {
            vkGetDeviceQueue(impl_->native.device, impl_->queueFamilies.present, 0, &impl_->native.presentQueue);
        }
        impl_->native.graphicsQueueFamily = impl_->queueFamilies.graphics;
        impl_->native.computeQueueFamily = impl_->queueFamilies.compute;
        impl_->native.transferQueueFamily = impl_->queueFamilies.transfer;
        impl_->native.presentQueueFamily = impl_->queueFamilies.present;

        // vkSetDebugUtilsObjectNameEXT 是 device-level command。设备创建完成后应通过
        // vkGetDeviceProcAddr 获取，避免依赖 loader 是否从 instance 查询返回兼容跳板。
        impl_->setDebugUtilsObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
            vkGetDeviceProcAddr(impl_->native.device, "vkSetDebugUtilsObjectNameEXT"));

        VkCommandPoolCreateInfo commandPoolInfo{};
        commandPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        // 当前帧录制路径会频繁分配、提交并释放短生命周期 command buffer。
        // TRANSIENT 是给驱动的使用提示；RESET_COMMAND_BUFFER 保留单独重置的扩展空间。
        commandPoolInfo.flags =
            VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
            VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        commandPoolInfo.queueFamilyIndex = impl_->queueFamilies.graphics;
        if (vkCreateCommandPool(impl_->native.device, &commandPoolInfo, nullptr, &impl_->graphicsCommandPool) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateCommandPool(graphics) failed");
        }

        std::array<VkDescriptorPoolSize, 6> poolSizes{{
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4096},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4096},
            {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 4096},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1024},
            {VK_DESCRIPTOR_TYPE_SAMPLER, 2048},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4096}
        }};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 4096;
        poolInfo.poolSizeCount = static_cast<u32>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        if (vkCreateDescriptorPool(impl_->native.device, &poolInfo, nullptr, &impl_->descriptorPool) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorPool failed");
        }

        VkPhysicalDeviceMemoryProperties memoryProperties{};
        vkGetPhysicalDeviceMemoryProperties(impl_->native.physicalDevice, &memoryProperties);

        impl_->caps.api = RHIGraphicsAPI::Vulkan;
        impl_->caps.adapterName = support.properties.deviceName;
        impl_->caps.maxTexture2DSize = support.properties.limits.maxImageDimension2D;
        impl_->caps.maxTexture3DSize = support.properties.limits.maxImageDimension3D;
        impl_->caps.maxTextureCubeSize = support.properties.limits.maxImageDimensionCube;
        impl_->caps.maxTextureArrayLayers = support.properties.limits.maxImageArrayLayers;
        impl_->caps.maxColorAttachments = support.properties.limits.maxColorAttachments;
        impl_->caps.maxBindSets = support.properties.limits.maxBoundDescriptorSets;
        impl_->caps.maxVertexBuffers = support.properties.limits.maxVertexInputBindings;
        impl_->caps.maxVertexAttributes = support.properties.limits.maxVertexInputAttributes;
        impl_->caps.maxPushConstantSize = support.properties.limits.maxPushConstantsSize;
        impl_->caps.minUniformBufferOffsetAlignment = support.properties.limits.minUniformBufferOffsetAlignment;
        impl_->caps.minStorageBufferOffsetAlignment = support.properties.limits.minStorageBufferOffsetAlignment;
        impl_->caps.optimalBufferCopyOffsetAlignment = support.properties.limits.optimalBufferCopyOffsetAlignment;
        impl_->caps.optimalBufferCopyRowPitchAlignment = support.properties.limits.optimalBufferCopyRowPitchAlignment;
        impl_->caps.maxSamplerAnisotropy = support.properties.limits.maxSamplerAnisotropy;
        impl_->caps.supportsCompute = impl_->queueFamilies.compute != RHI_INVALID_INDEX;
        impl_->caps.supportsGeometryShader = support.features.geometryShader == VK_TRUE;
        impl_->caps.supportsTessellation = support.features.tessellationShader == VK_TRUE;
        impl_->caps.supportsSamplerAnisotropy = support.features.samplerAnisotropy == VK_TRUE;
        impl_->caps.supportsSamplerCompare = true;
        impl_->caps.supportsTimestampQuery = support.properties.limits.timestampComputeAndGraphics == VK_TRUE;
        impl_->caps.supportsOcclusionQuery = support.features.occlusionQueryPrecise == VK_TRUE;
        impl_->caps.supportsPipelineStatisticsQuery = support.features.pipelineStatisticsQuery == VK_TRUE;
        impl_->caps.supportsIndirectDraw = true;
        impl_->caps.supportsDrawIndirectCount = support.features12.drawIndirectCount == VK_TRUE;
        impl_->caps.supportsDynamicRendering = support.features13.dynamicRendering == VK_TRUE;
        impl_->caps.supportsDebugMarkers = impl_->setDebugUtilsObjectName != nullptr;
        impl_->caps.supportsTextureCompressionBC = support.features.textureCompressionBC == VK_TRUE;
        impl_->caps.supportsTextureCompressionETC2 = support.features.textureCompressionETC2 == VK_TRUE;
        impl_->caps.supportsTextureCompressionASTC = support.features.textureCompressionASTC_LDR == VK_TRUE;
        impl_->supportsTimelineSemaphore = support.features12.timelineSemaphore == VK_TRUE;

        if (impl_->caps.supportsCompute)                 impl_->caps.features |= RHIRenderFeature::Compute;
        if (impl_->caps.supportsGeometryShader)          impl_->caps.features |= RHIRenderFeature::GeometryShader;
        if (impl_->caps.supportsTessellation)            impl_->caps.features |= RHIRenderFeature::Tessellation;
        if (impl_->caps.supportsSamplerAnisotropy)       impl_->caps.features |= RHIRenderFeature::SamplerAnisotropy;
        if (impl_->caps.supportsTimestampQuery)          impl_->caps.features |= RHIRenderFeature::TimestampQuery;
        if (impl_->caps.supportsOcclusionQuery)          impl_->caps.features |= RHIRenderFeature::OcclusionQuery;
        if (impl_->caps.supportsPipelineStatisticsQuery) impl_->caps.features |= RHIRenderFeature::PipelineStatisticsQuery;
        if (impl_->caps.supportsDynamicRendering)        impl_->caps.features |= RHIRenderFeature::DynamicRendering;
        if (impl_->caps.supportsDebugMarkers)            impl_->caps.features |= RHIRenderFeature::DebugMarkers;
        if (impl_->caps.supportsTextureCompressionBC)    impl_->caps.features |= RHIRenderFeature::TextureCompressionBC;
        if (impl_->caps.supportsTextureCompressionETC2)  impl_->caps.features |= RHIRenderFeature::TextureCompressionETC2;
        if (impl_->caps.supportsTextureCompressionASTC)  impl_->caps.features |= RHIRenderFeature::TextureCompressionASTC;

        for (u32 i = 0; i < memoryProperties.memoryHeapCount; ++i) {
            if ((memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
                impl_->caps.dedicatedVideoMemory += memoryProperties.memoryHeaps[i].size;
            } else {
                impl_->caps.sharedSystemMemory += memoryProperties.memoryHeaps[i].size;
            }
        }

        return true;
    } catch (const std::exception& error) {
        if (errorMessage != nullptr) {
            *errorMessage = error.what();
        }
        shutdown();
        return false;
    }
}

void RHIVulkan::shutdown() noexcept {
    if (!impl_) {
        return;
    }

    if (impl_->native.device == VK_NULL_HANDLE) {
        if (impl_->debugMessenger != VK_NULL_HANDLE) {
            auto destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(impl_->native.instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyDebugMessenger != nullptr) {
                destroyDebugMessenger(impl_->native.instance, impl_->debugMessenger, nullptr);
            }
            impl_->debugMessenger = VK_NULL_HANDLE;
        }

        if (impl_->native.instance != VK_NULL_HANDLE && impl_->ownsSurface && impl_->native.surface != VK_NULL_HANDLE) {
            vkDestroySurfaceKHR(impl_->native.instance, impl_->native.surface, nullptr);
            impl_->native.surface = VK_NULL_HANDLE;
        }

        if (impl_->native.instance != VK_NULL_HANDLE) {
            vkDestroyInstance(impl_->native.instance, nullptr);
            impl_->native.instance = VK_NULL_HANDLE;
        }
        return;
    }

    vkDeviceWaitIdle(impl_->native.device);

    // 销毁顺序按依赖反向来：swapchain/image view 依赖 texture，bind set 依赖 layout，
    // pipeline 依赖 pipeline layout，底层 buffer/texture 最后释放。Vulkan 对象销毁时
    // 不会自动追踪这些关系，所以后端需要保持明确顺序。
    for (u64 i = impl_->swapchains.size(); i > 0; --i)       destroy(RHISwapchain(i));
    for (u64 i = impl_->pipelines.size(); i > 0; --i)        destroy(RHIPipeline(i));
    for (u64 i = impl_->pipelineCaches.size(); i > 0; --i)   destroy(RHIPipelineCache(i));
    for (u64 i = impl_->pipelineLayouts.size(); i > 0; --i)  destroy(RHIPipelineLayout(i));
    for (u64 i = impl_->bindSets.size(); i > 0; --i)       destroy(RHIBindSet(i));
    for (u64 i = impl_->bindSetLayouts.size(); i > 0; --i) destroy(RHIBindSetLayout(i));
    for (u64 i = impl_->queryPools.size(); i > 0; --i)       destroy(RHIQueryPool(i));
    for (u64 i = impl_->gpuWaitGPUSignals.size(); i > 0; --i)       destroy(RHIGPUWaitGPUSignal(i));
    for (u64 i = impl_->cpuWaitGPUSignals.size(); i > 0; --i)           destroy(RHICPUWaitGPUSignal(i));
    for (u64 i = impl_->shaders.size(); i > 0; --i)          destroy(RHIShader(i));
    for (u64 i = impl_->samplers.size(); i > 0; --i)         destroy(RHISampler(i));
    for (u64 i = impl_->textureViews.size(); i > 0; --i)     destroy(RHITextureView(i));
    for (u64 i = impl_->textures.size(); i > 0; --i)         destroy(RHITexture(i));
    for (u64 i = impl_->buffers.size(); i > 0; --i)          destroy(RHIBuffer(i));

    if (impl_->graphicsCommandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(impl_->native.device, impl_->graphicsCommandPool, nullptr);
        impl_->graphicsCommandPool = VK_NULL_HANDLE;
    }

    if (impl_->descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(impl_->native.device, impl_->descriptorPool, nullptr);
        impl_->descriptorPool = VK_NULL_HANDLE;
    }

    vkDestroyDevice(impl_->native.device, nullptr);
    impl_->native.device = VK_NULL_HANDLE;

    if (impl_->debugMessenger != VK_NULL_HANDLE) {
        auto destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(impl_->native.instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyDebugMessenger != nullptr) {
            destroyDebugMessenger(impl_->native.instance, impl_->debugMessenger, nullptr);
        }
        impl_->debugMessenger = VK_NULL_HANDLE;
    }

    if (impl_->ownsSurface && impl_->native.surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(impl_->native.instance, impl_->native.surface, nullptr);
        impl_->native.surface = VK_NULL_HANDLE;
    }

    if (impl_->native.instance != VK_NULL_HANDLE) {
        vkDestroyInstance(impl_->native.instance, nullptr);
        impl_->native.instance = VK_NULL_HANDLE;
    }
}

bool RHIVulkan::isInitialized() const noexcept {
    return impl_ != nullptr && impl_->native.device != VK_NULL_HANDLE;
}

const RHICapabilities& RHIVulkan::capabilities() const noexcept {
    return impl_->caps;
}

const RHIVulkanNativeHandles& RHIVulkan::nativeHandles() const noexcept {
    return impl_->native;
}

// Buffer 在 Vulkan 中分成两步：先创建 VkBuffer 得到资源形状和 usage，再查询 memory
// requirements，分配合适 memory type，最后 vkBindBufferMemory 绑定。persistentlyMapped
// 只适合 CPU 可见内存，用来让上层长期写入动态数据。

} // namespace rhi






