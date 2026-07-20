// =============================================================================
// PBR Demo · Vulkan
//
// 渲染两个网格：
//   - 1 Plane (4 顶点 / 6 索引，水平地面)
//   - 1 Sphere (UV 球体，代码生成 32×64 = 2080 顶点 / ~4000 索引)
//
// 单一 PBR 管线：Cook-Torrance GGX + Schlick Fresnel + 半球 ambient。
// 顶点数据 pos + normal + uv = 32 字节 stride，host-visible staging 上传到
// device-local vertex/index buffer。
//
// 相机：固定俯视 (0, 3, 6) 看原点，球体自转。
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

// 必须在 include vulkan.h 之前定义 platform guard，否则 vulkan_win32.h 不会展开
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <vulkan/vulkan.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

constexpr uint32_t WINDOW_WIDTH  = 1280;
constexpr uint32_t WINDOW_HEIGHT = 800;

constexpr int    MAX_FRAMES_IN_FLIGHT = 2;
constexpr float  PI                   = 3.14159265359f;

// -----------------------------------------------------------------------------
// 顶点布局
// -----------------------------------------------------------------------------
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;

    static VkVertexInputBindingDescription bindingDescription() {
        VkVertexInputBindingDescription desc{};
        desc.binding   = 0;
        desc.stride    = sizeof(Vertex);
        desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return desc;
    }

    static std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attrs{};
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
        attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Vertex, uv)};
        return attrs;
    }
};

// UBO 必须按 16 字节对齐（Vulkan minUniformBufferOffsetAlignment）
struct alignas(16) UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
    glm::vec4 lightDir;
    glm::vec4 lightColor;
    glm::vec4 cameraPos;
    glm::vec4 baseColor;
    glm::vec4 materialParams;
};

// -----------------------------------------------------------------------------
// Mesh 数据结构
// -----------------------------------------------------------------------------
struct Mesh {
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
};

static Mesh makePlane(float halfSize = 4.0f) {
    Mesh m;
    m.vertices = {
        {{-halfSize, 0.0f, -halfSize}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ halfSize, 0.0f, -halfSize}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ halfSize, 0.0f,  halfSize}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-halfSize, 0.0f,  halfSize}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    };
    m.indices = {0, 1, 2, 0, 2, 3};
    return m;
}

// UV Sphere 代码生成。rings=纬度分段，segments=经度分段。
// 极点不重复（按 vulkan-tutorial 的 UV sphere 风格）。
static Mesh makeUVSphere(int rings, int segments, float radius = 1.0f) {
    Mesh m;
    m.vertices.reserve((rings + 1) * (segments + 1));
    m.indices.reserve(rings * segments * 6);

    for (int y = 0; y <= rings; ++y) {
        for (int x = 0; x <= segments; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(segments);
            const float v = static_cast<float>(y) / static_cast<float>(rings);
            const float theta = u * 2.0f * PI;            // 经度
            const float phi   = v * PI;                   // 纬度 [0, PI]

            const float sinPhi   = std::sin(phi);
            const float cosPhi   = std::cos(phi);
            const float sinTheta = std::sin(theta);
            const float cosTheta = std::cos(theta);

            const float px = -cosTheta * sinPhi;
            const float py =  cosPhi;
            const float pz =  sinTheta * sinPhi;

            Vertex vert{};
            vert.position = glm::vec3(px, py, pz) * radius;
            vert.normal   = glm::vec3(px, py, pz);   // 球面上 = 单位法线
            vert.uv       = glm::vec2(u, v);
            m.vertices.push_back(vert);
        }
    }

    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            const uint32_t a = static_cast<uint32_t>(y)       * (segments + 1) + static_cast<uint32_t>(x);
            const uint32_t b = (static_cast<uint32_t>(y) + 1) * (segments + 1) + static_cast<uint32_t>(x);
            const uint32_t c = (static_cast<uint32_t>(y) + 1) * (segments + 1) + static_cast<uint32_t>(x + 1);
            const uint32_t d = static_cast<uint32_t>(y)       * (segments + 1) + static_cast<uint32_t>(x + 1);
            m.indices.insert(m.indices.end(), {a, b, c, a, c, d});
        }
    }
    return m;
}

// -----------------------------------------------------------------------------
// Vulkan helper
// -----------------------------------------------------------------------------
static std::vector<char> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }
    const std::streamsize size = file.tellg();
    if (size <= 0) {
        return {};
    }
    std::vector<char> buffer(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), size);
    return buffer;
}

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice,
                               uint32_t typeFilter,
                               VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0 &&
            (memProps.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("failed to find suitable memory type");
}

// -----------------------------------------------------------------------------
// 队列族查找
// -----------------------------------------------------------------------------
struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    bool isComplete() const { return graphics.has_value() && present.has_value(); }
};

static QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface) {
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, families.data());

    for (uint32_t i = 0; i < families.size(); ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphics = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport == VK_TRUE) {
            indices.present = i;
        }
        if (indices.isComplete()) {
            break;
        }
    }
    return indices;
}

static bool checkDeviceExtensionSupport(VkPhysicalDevice device, const std::vector<const char*>& required) {
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> available(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, available.data());
    std::set<std::string> requiredSet(required.begin(), required.end());
    for (const auto& e : available) {
        requiredSet.erase(e.extensionName);
    }
    return requiredSet.empty();
}

struct SwapchainSupportDetails {
    VkSurfaceCapabilitiesKHR        capabilities{};
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR>   presentModes;
};

static SwapchainSupportDetails querySwapchainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
    SwapchainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

static VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) {
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return f;
        }
    }
    return formats[0];
}

static VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR) {
            return m;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

static VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, HWND hwnd) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    }
    RECT rect{};
    GetClientRect(hwnd, &rect);
    VkExtent2D actual = {
        std::clamp(static_cast<uint32_t>(rect.right - rect.left), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(rect.bottom - rect.top), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };
    return actual;
}

// -----------------------------------------------------------------------------
// 主应用
// -----------------------------------------------------------------------------
class PBRDemoApp {
public:
    void run(HINSTANCE hInstance) {
        initWindow(hInstance);
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    // 窗口
    HWND hwnd_ = nullptr;

    // Vulkan 核心对象
    VkInstance                instance_          = VK_NULL_HANDLE;
    VkPhysicalDevice          physicalDevice_    = VK_NULL_HANDLE;
    VkDevice                  device_            = VK_NULL_HANDLE;
    VkSurfaceKHR              surface_           = VK_NULL_HANDLE;
    VkQueue                   graphicsQueue_     = VK_NULL_HANDLE;
    VkQueue                   presentQueue_      = VK_NULL_HANDLE;
    VkSwapchainKHR            swapchain_         = VK_NULL_HANDLE;
    std::vector<VkImage>      swapchainImages_;
    VkFormat                  swapchainImageFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D                swapchainExtent_{};
    std::vector<VkImageView>  swapchainImageViews_;
    VkRenderPass              renderPass_        = VK_NULL_HANDLE;
    VkDescriptorSetLayout     descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout          pipelineLayout_    = VK_NULL_HANDLE;
    VkPipeline                graphicsPipeline_  = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> swapchainFramebuffers_;
    VkCommandPool             commandPool_       = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> commandBuffers_;

    // 同步
    std::vector<VkSemaphore> imageAvailableSemaphores_;
    std::vector<VkSemaphore> renderFinishedSemaphores_;
    std::vector<VkFence>     inFlightFences_;
    uint32_t                 currentFrame_      = 0;
    bool                     framebufferResized_ = false;

    // Mesh GPU buffer（合并到一块大 buffer，draw 时偏移）
    VkBuffer       vertexBuffer_   = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory_   = VK_NULL_HANDLE;
    VkBuffer       indexBuffer_    = VK_NULL_HANDLE;
    VkDeviceMemory indexMemory_    = VK_NULL_HANDLE;

    // 描述 mesh 在 buffer 里的范围
    VkDeviceSize   sphereVertexOffset_ = 0;
    VkDeviceSize   sphereIndexOffset_  = 0;
    uint32_t       sphereIndexCount_   = 0;
    VkDeviceSize   planeVertexOffset_  = 0;
    VkDeviceSize   planeIndexOffset_   = 0;
    uint32_t       planeIndexCount_    = 0;

    // Uniform / Descriptor
    std::vector<VkBuffer>       uniformBuffers_;
    std::vector<VkDeviceMemory> uniformBuffersMemory_;
    std::vector<void*>          uniformBuffersMapped_;
    VkDescriptorPool            descriptorPool_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets_;

    // 帧计时
    std::chrono::steady_clock::time_point startTime_;

    // -------------------------------------------------------------------------
    void initWindow(HINSTANCE hInstance) {
        WNDCLASS wc{};
        wc.lpfnWndProc   = wndProc;
        wc.hInstance     = hInstance;
        wc.lpszClassName = "PBRDemoClass";
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        RegisterClass(&wc);

        RECT rect{0, 0, static_cast<LONG>(WINDOW_WIDTH), static_cast<LONG>(WINDOW_HEIGHT)};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        const int width  = rect.right  - rect.left;
        const int height = rect.bottom - rect.top;

        hwnd_ = CreateWindowEx(
            0, wc.lpszClassName, "PBR Demo · Plane + Sphere",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT, CW_USEDEFAULT, width, height,
            nullptr, nullptr, hInstance, this);
        if (hwnd_ == nullptr) {
            throw std::runtime_error("CreateWindowEx failed");
        }
    }

    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipeline();
        createCommandPool();
        createDepthResources();
        createFramebuffers();
        createVertexBuffer();
        createIndexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
        startTime_ = std::chrono::steady_clock::now();
    }

    void mainLoop() {
        MSG msg{};
        while (true) {
            while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    return;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            if (framebufferResized_) {
                recreateSwapchain();
                framebufferResized_ = false;
            }
            drawFrame();
        }
    }

    void cleanup() {
        vkDeviceWaitIdle(device_);

        cleanupSwapchain();

        vkDestroyBuffer(device_, indexBuffer_,  nullptr);
        vkFreeMemory  (device_, indexMemory_,   nullptr);
        vkDestroyBuffer(device_, vertexBuffer_, nullptr);
        vkFreeMemory  (device_, vertexMemory_,  nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vkDestroyBuffer(device_, uniformBuffers_[i], nullptr);
            vkFreeMemory  (device_, uniformBuffersMemory_[i], nullptr);
        }
        vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        vkDestroyPipeline(device_, graphicsPipeline_, nullptr);
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
        vkDestroyRenderPass(device_, renderPass_, nullptr);

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            vkDestroySemaphore(device_, renderFinishedSemaphores_[i], nullptr);
            vkDestroySemaphore(device_, imageAvailableSemaphores_[i], nullptr);
            vkDestroyFence    (device_, inFlightFences_[i], nullptr);
        }

        vkDestroyCommandPool(device_, commandPool_, nullptr);
        vkDestroyDevice(device_, nullptr);
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        vkDestroyInstance(instance_, nullptr);
        DestroyWindow(hwnd_);
    }

    // -------------------------------------------------------------------------
    // Instance
    // -------------------------------------------------------------------------
    void createInstance() {
        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "PBR Demo";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "Excalibur";
        appInfo.engineVersion      = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_3;

        const std::vector<const char*> extensions = {
            VK_KHR_SURFACE_EXTENSION_NAME,
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        };

        VkInstanceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo        = &appInfo;
        createInfo.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        createInfo.ppEnabledExtensionNames = extensions.data();
        if (vkCreateInstance(&createInfo, nullptr, &instance_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateInstance failed");
        }
    }

    void createSurface() {
        VkWin32SurfaceCreateInfoKHR info{};
        info.sType     = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        info.hinstance = GetModuleHandle(nullptr);
        info.hwnd      = hwnd_;
        if (vkCreateWin32SurfaceKHR(instance_, &info, nullptr, &surface_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateWin32SurfaceKHR failed");
        }
    }

    // -------------------------------------------------------------------------
    // 物理设备 / 逻辑设备
    // -------------------------------------------------------------------------
    void pickPhysicalDevice() {
        uint32_t deviceCount = 0;
        vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr);
        if (deviceCount == 0) {
            throw std::runtime_error("no Vulkan-capable GPU found");
        }
        std::vector<VkPhysicalDevice> devices(deviceCount);
        vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

        for (const auto& d : devices) {
            const QueueFamilyIndices indices = findQueueFamilies(d, surface_);
            const bool extensionsSupported = checkDeviceExtensionSupport(d, {VK_KHR_SWAPCHAIN_EXTENSION_NAME});
            const bool swapchainAdequate   = !querySwapchainSupport(d, surface_).formats.empty();
            if (indices.isComplete() && extensionsSupported && swapchainAdequate) {
                physicalDevice_ = d;
                return;
            }
        }
        throw std::runtime_error("no suitable physical device");
    }

    void createLogicalDevice() {
        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_, surface_);
        std::vector<uint32_t> uniqueFamilies = {indices.graphics.value(), indices.present.value()};

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        const float queuePriority = 1.0f;
        for (uint32_t family : uniqueFamilies) {
            VkDeviceQueueCreateInfo info{};
            info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            info.queueFamilyIndex = family;
            info.queueCount       = 1;
            info.pQueuePriorities = &queuePriority;
            queueCreateInfos.push_back(info);
        }

        VkPhysicalDeviceFeatures features{};
        const std::vector<const char*> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        VkDeviceCreateInfo createInfo{};
        createInfo.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount    = static_cast<uint32_t>(queueCreateInfos.size());
        createInfo.pQueueCreateInfos       = queueCreateInfos.data();
        createInfo.pEnabledFeatures        = &features;
        createInfo.enabledExtensionCount   = static_cast<uint32_t>(deviceExtensions.size());
        createInfo.ppEnabledExtensionNames = deviceExtensions.data();
        if (vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDevice failed");
        }
        vkGetDeviceQueue(device_, indices.graphics.value(), 0, &graphicsQueue_);
        vkGetDeviceQueue(device_, indices.present.value(),  0, &presentQueue_);
    }

    // -------------------------------------------------------------------------
    // Swapchain
    // -------------------------------------------------------------------------
    void createSwapchain() {
        const SwapchainSupportDetails support = querySwapchainSupport(physicalDevice_, surface_);
        const VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(support.formats);
        const VkPresentModeKHR   presentMode   = chooseSwapPresentMode(support.presentModes);
        const VkExtent2D         extent        = chooseSwapExtent(support.capabilities, hwnd_);

        uint32_t imageCount = support.capabilities.minImageCount + 1;
        if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
            imageCount = support.capabilities.maxImageCount;
        }

        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_, surface_);
        const uint32_t families[] = {indices.graphics.value(), indices.present.value()};

        VkSwapchainCreateInfoKHR info{};
        info.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface          = surface_;
        info.minImageCount    = imageCount;
        info.imageFormat      = surfaceFormat.format;
        info.imageColorSpace  = surfaceFormat.colorSpace;
        info.imageExtent      = extent;
        info.imageArrayLayers = 1;
        info.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.preTransform     = support.capabilities.currentTransform;
        info.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode      = presentMode;
        info.clipped          = VK_TRUE;
        info.oldSwapchain     = VK_NULL_HANDLE;
        if (indices.graphics != indices.present) {
            info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            info.queueFamilyIndexCount = 2;
            info.pQueueFamilyIndices   = families;
        } else {
            info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        if (vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateSwapchainKHR failed");
        }

        vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
        swapchainImages_.resize(imageCount);
        vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, swapchainImages_.data());
        swapchainImageFormat_ = surfaceFormat.format;
        swapchainExtent_      = extent;
    }

    void createImageViews() {
        swapchainImageViews_.resize(swapchainImages_.size());
        for (size_t i = 0; i < swapchainImages_.size(); ++i) {
            VkImageViewCreateInfo info{};
            info.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image    = swapchainImages_[i];
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format   = swapchainImageFormat_;
            info.components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY};
            info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.baseMipLevel   = 0;
            info.subresourceRange.levelCount     = 1;
            info.subresourceRange.baseArrayLayer = 0;
            info.subresourceRange.layerCount     = 1;
            if (vkCreateImageView(device_, &info, nullptr, &swapchainImageViews_[i]) != VK_SUCCESS) {
                throw std::runtime_error("vkCreateImageView failed");
            }
        }
    }

    // -------------------------------------------------------------------------
    // Depth buffer
    // -------------------------------------------------------------------------
    VkImage        depthImage_   = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_  = VK_NULL_HANDLE;
    VkImageView    depthImageView_ = VK_NULL_HANDLE;

    void createDepthResources() {
        VkImageCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType     = VK_IMAGE_TYPE_2D;
        info.format        = VK_FORMAT_D32_SFLOAT;
        info.extent        = {swapchainExtent_.width, swapchainExtent_.height, 1};
        info.mipLevels     = 1;
        info.arrayLayers   = 1;
        info.samples       = VK_SAMPLE_COUNT_1_BIT;
        info.tiling        = VK_IMAGE_TILING_OPTIMAL;
        info.usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        info.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(device_, &info, nullptr, &depthImage_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImage(depth) failed");
        }
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device_, depthImage_, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = req.size;
        alloc.memoryTypeIndex = findMemoryType(physicalDevice_, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device_, &alloc, nullptr, &depthMemory_) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateMemory(depth) failed");
        }
        vkBindImageMemory(device_, depthImage_, depthMemory_, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image    = depthImage_;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format   = VK_FORMAT_D32_SFLOAT;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(device_, &viewInfo, nullptr, &depthImageView_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateImageView(depth) failed");
        }
    }

    // -------------------------------------------------------------------------
    // Render pass / Pipeline
    // -------------------------------------------------------------------------
    void createRenderPass() {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = swapchainImageFormat_;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format         = VK_FORMAT_D32_SFLOAT;
        depthAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = 1;
        subpass.pColorAttachments       = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dependency{};
        dependency.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass    = 0;
        dependency.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.srcAccessMask = 0;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        const std::array<VkAttachmentDescription, 2> attachments = {colorAttachment, depthAttachment};
        VkRenderPassCreateInfo info{};
        info.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        info.attachmentCount = static_cast<uint32_t>(attachments.size());
        info.pAttachments    = attachments.data();
        info.subpassCount    = 1;
        info.pSubpasses      = &subpass;
        info.dependencyCount = 1;
        info.pDependencies   = &dependency;
        if (vkCreateRenderPass(device_, &info, nullptr, &renderPass_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateRenderPass failed");
        }
    }

    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding         = 0;
        uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

        VkDescriptorSetLayoutCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 1;
        info.pBindings    = &uboBinding;
        if (vkCreateDescriptorSetLayout(device_, &info, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorSetLayout failed");
        }
    }

    void createGraphicsPipeline() {
        const std::vector<char> vertCode = readFile(std::string(PBR_SHADER_DIR) + "/pbr.vert.spv");
        const std::vector<char> fragCode = readFile(std::string(PBR_SHADER_DIR) + "/pbr.frag.spv");

        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage  = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName  = "main";

        VkPipelineShaderStageCreateInfo fragStage = vertStage;
        fragStage.stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;

        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {vertStage, fragStage};

        const auto binding   = Vertex::bindingDescription();
        const auto attrs     = Vertex::attributeDescriptions();
        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount   = 1;
        vertexInput.pVertexBindingDescriptions      = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vertexInput.pVertexAttributeDescriptions    = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth   = 1.0f;
        rasterizer.cullMode    = VK_CULL_MODE_BACK_BIT;
        rasterizer.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.rasterizationSamples  = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable  = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState colorBlendAttachment{};
        colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments     = &colorBlendAttachment;

        const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates    = dynamicStates.data();

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts    = &descriptorSetLayout_;
        if (vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreatePipelineLayout failed");
        }

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount          = static_cast<uint32_t>(stages.size());
        pipelineInfo.pStages             = stages.data();
        pipelineInfo.pVertexInputState   = &vertexInput;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState      = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState   = &multisampling;
        pipelineInfo.pDepthStencilState  = &depthStencil;
        pipelineInfo.pColorBlendState    = &colorBlending;
        pipelineInfo.pDynamicState       = &dynamicState;
        pipelineInfo.layout              = pipelineLayout_;
        pipelineInfo.renderPass          = renderPass_;
        pipelineInfo.subpass             = 0;
        if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateGraphicsPipelines failed");
        }

        vkDestroyShaderModule(device_, fragModule, nullptr);
        vkDestroyShaderModule(device_, vertModule, nullptr);
    }

    VkShaderModule createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo info{};
        info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = code.size();
        info.pCode    = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule module = VK_NULL_HANDLE;
        if (vkCreateShaderModule(device_, &info, nullptr, &module) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateShaderModule failed");
        }
        return module;
    }

    void createFramebuffers() {
        swapchainFramebuffers_.resize(swapchainImageViews_.size());
        for (size_t i = 0; i < swapchainImageViews_.size(); ++i) {
            const std::array<VkImageView, 2> attachments = {swapchainImageViews_[i], depthImageView_};
            VkFramebufferCreateInfo info{};
            info.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            info.renderPass      = renderPass_;
            info.attachmentCount = static_cast<uint32_t>(attachments.size());
            info.pAttachments    = attachments.data();
            info.width           = swapchainExtent_.width;
            info.height          = swapchainExtent_.height;
            info.layers          = 1;
            if (vkCreateFramebuffer(device_, &info, nullptr, &swapchainFramebuffers_[i]) != VK_SUCCESS) {
                throw std::runtime_error("vkCreateFramebuffer failed");
            }
        }
    }

    void createCommandPool() {
        const QueueFamilyIndices indices = findQueueFamilies(physicalDevice_, surface_);
        VkCommandPoolCreateInfo info{};
        info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        info.queueFamilyIndex = indices.graphics.value();
        info.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(device_, &info, nullptr, &commandPool_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateCommandPool failed");
        }
    }

    // -------------------------------------------------------------------------
    // Vertex / Index buffer
    // -------------------------------------------------------------------------
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& memory) {
        VkBufferCreateInfo info{};
        info.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size        = size;
        info.usage       = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device_, &info, nullptr, &buffer) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateBuffer failed");
        }
        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device_, buffer, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize  = req.size;
        alloc.memoryTypeIndex = findMemoryType(physicalDevice_, req.memoryTypeBits, properties);
        if (vkAllocateMemory(device_, &alloc, nullptr, &memory) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateMemory failed");
        }
        vkBindBufferMemory(device_, buffer, memory, 0);
    }

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
        VkCommandBufferAllocateInfo alloc{};
        alloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandPool        = commandPool_;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device_, &alloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copy{};
        copy.size = size;
        vkCmdCopyBuffer(cmd, src, dst, 1, &copy);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submit{};
        submit.sType             = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers    = &cmd;
        vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue_);
        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    }

    void createVertexBuffer() {
        const Mesh sphere = makeUVSphere(32, 64, 1.0f);
        const Mesh plane  = makePlane(4.0f);

        const VkDeviceSize sphereVertexSize = sizeof(Vertex) * sphere.vertices.size();
        const VkDeviceSize planeVertexSize  = sizeof(Vertex) * plane.vertices.size();
        const VkDeviceSize totalVertexSize  = sphereVertexSize + planeVertexSize;

        // 单个 buffer：[sphere vertices | plane vertices]
        sphereVertexOffset_ = 0;
        planeVertexOffset_  = sphereVertexSize;

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        createBuffer(totalVertexSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     staging, stagingMem);

        void* data = nullptr;
        vkMapMemory(device_, stagingMem, 0, totalVertexSize, 0, &data);
        std::memcpy(data,                                 sphere.vertices.data(), sphereVertexSize);
        std::memcpy(static_cast<char*>(data) + planeVertexOffset_, plane.vertices.data(),  planeVertexSize);
        vkUnmapMemory(device_, stagingMem);

        createBuffer(totalVertexSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     vertexBuffer_, vertexMemory_);
        copyBuffer(staging, vertexBuffer_, totalVertexSize);

        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
    }

    void createIndexBuffer() {
        const Mesh sphere = makeUVSphere(32, 64, 1.0f);
        const Mesh plane  = makePlane(4.0f);

        const VkDeviceSize sphereIndexSize = sizeof(uint32_t) * sphere.indices.size();
        const VkDeviceSize planeIndexSize  = sizeof(uint32_t) * plane.indices.size();
        const VkDeviceSize totalIndexSize  = sphereIndexSize + planeIndexSize;

        sphereIndexOffset_ = 0;
        sphereIndexCount_  = static_cast<uint32_t>(sphere.indices.size());
        planeIndexOffset_  = sphereIndexSize;
        planeIndexCount_   = static_cast<uint32_t>(plane.indices.size());

        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMem = VK_NULL_HANDLE;
        createBuffer(totalIndexSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     staging, stagingMem);

        void* data = nullptr;
        vkMapMemory(device_, stagingMem, 0, totalIndexSize, 0, &data);
        std::memcpy(data,                                sphere.indices.data(), sphereIndexSize);
        std::memcpy(static_cast<char*>(data) + planeIndexOffset_, plane.indices.data(),  planeIndexSize);
        vkUnmapMemory(device_, stagingMem);

        createBuffer(totalIndexSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     indexBuffer_, indexMemory_);
        copyBuffer(staging, indexBuffer_, totalIndexSize);

        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, stagingMem, nullptr);
    }

    // -------------------------------------------------------------------------
    // Uniform buffer / Descriptor set
    // -------------------------------------------------------------------------
    void createUniformBuffers() {
        const VkDeviceSize size = sizeof(UniformBufferObject);
        uniformBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMemory_.resize(MAX_FRAMES_IN_FLIGHT);
        uniformBuffersMapped_.resize(MAX_FRAMES_IN_FLIGHT);
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            createBuffer(size,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         uniformBuffers_[i], uniformBuffersMemory_[i]);
            vkMapMemory(device_, uniformBuffersMemory_[i], 0, size, 0, &uniformBuffersMapped_[i]);
        }
    }

    void createDescriptorPool() {
        VkDescriptorPoolSize poolSize{};
        poolSize.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSize.descriptorCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        VkDescriptorPoolCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.poolSizeCount = 1;
        info.pPoolSizes    = &poolSize;
        info.maxSets       = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        if (vkCreateDescriptorPool(device_, &info, nullptr, &descriptorPool_) != VK_SUCCESS) {
            throw std::runtime_error("vkCreateDescriptorPool failed");
        }
    }

    void createDescriptorSets() {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout_);
        VkDescriptorSetAllocateInfo info{};
        info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool     = descriptorPool_;
        info.descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT);
        info.pSetLayouts        = layouts.data();
        descriptorSets_.resize(MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateDescriptorSets(device_, &info, descriptorSets_.data()) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateDescriptorSets failed");
        }
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uniformBuffers_[i];
            bufferInfo.offset = 0;
            bufferInfo.range  = sizeof(UniformBufferObject);
            VkWriteDescriptorSet write{};
            write.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet          = descriptorSets_[i];
            write.dstBinding      = 0;
            write.dstArrayElement = 0;
            write.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write.descriptorCount = 1;
            write.pBufferInfo     = &bufferInfo;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }

    // -------------------------------------------------------------------------
    // Command buffer
    // -------------------------------------------------------------------------
    void createCommandBuffers() {
        commandBuffers_.resize(MAX_FRAMES_IN_FLIGHT);
        VkCommandBufferAllocateInfo info{};
        info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        info.commandPool        = commandPool_;
        info.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        info.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
        if (vkAllocateCommandBuffers(device_, &info, commandBuffers_.data()) != VK_SUCCESS) {
            throw std::runtime_error("vkAllocateCommandBuffers failed");
        }
    }

    void recordCommandBuffer(VkCommandBuffer cmd, uint32_t imageIndex) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
            throw std::runtime_error("vkBeginCommandBuffer failed");
        }

        const std::array<VkClearValue, 2> clearValues = {
            VkClearValue{.color = {{0.02f, 0.02f, 0.02f, 1.0f}}},
            VkClearValue{.depthStencil = {1.0f, 0}},
        };
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass        = renderPass_;
        rpInfo.framebuffer       = swapchainFramebuffers_[imageIndex];
        rpInfo.renderArea.offset = {0, 0};
        rpInfo.renderArea.extent = swapchainExtent_;
        rpInfo.clearValueCount   = static_cast<uint32_t>(clearValues.size());
        rpInfo.pClearValues      = clearValues.data();
        vkCmdBeginRenderPass(cmd, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline_);

        VkViewport viewport{};
        viewport.width    = static_cast<float>(swapchainExtent_.width);
        viewport.height   = static_cast<float>(swapchainExtent_.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.extent = swapchainExtent_;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // ---- Draw 1: 球体（自转 + 金属铜色）----
        // 更新 UBO 中的 model / material → bind descriptor → draw。
        // 由于 UBO 是 host-coherent host-visible 映射，CPU 写入 GPU 立即可见，
        // draw 时 GPU 从 UBO 抓取的就是这次 memcpy 后的内容。
        updateUniformBufferForMesh(MeshKind::Sphere);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &sphereVertexOffset_);
        vkCmdBindIndexBuffer  (cmd, indexBuffer_, sphereIndexOffset_, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed      (cmd, sphereIndexCount_, 1, 0, 0, 0);

        // ---- Draw 2: 平面（静止，浅灰非金属）----
        updateUniformBufferForMesh(MeshKind::Plane);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSets_[currentFrame_], 0, nullptr);
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &planeVertexOffset_);
        vkCmdBindIndexBuffer  (cmd, indexBuffer_, planeIndexOffset_, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed      (cmd, planeIndexCount_, 1, 0, 0, 0);

        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
            throw std::runtime_error("vkEndCommandBuffer failed");
        }
    }

    enum class MeshKind { Sphere, Plane };

    void updateUniformBufferForMesh(MeshKind kind) {
        static auto start = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        const float t = std::chrono::duration<float>(now - start).count();

        UniformBufferObject ubo{};

        // 相机：固定俯视 (0, 3, 6) 看原点
        const glm::vec3 eye    = glm::vec3(0.0f, 3.0f, 6.0f);
        const glm::vec3 center = glm::vec3(0.0f, 0.5f, 0.0f);
        const glm::vec3 up     = glm::vec3(0.0f, 1.0f, 0.0f);
        ubo.view = glm::lookAt(eye, center, up);
        ubo.proj = glm::perspective(glm::radians(45.0f),
                                    static_cast<float>(swapchainExtent_.width) / static_cast<float>(swapchainExtent_.height),
                                    0.1f, 100.0f);
        // GLM 是 OpenGL 风格，Vulkan 的 Y 翻转需要在这里做（投影矩阵 Y 取负）
        ubo.proj[1][1] *= -1.0f;

        // 共享光照 + 相机
        ubo.lightDir   = glm::vec4(glm::normalize(glm::vec3(-0.5f, -1.0f, -0.3f)), 0.0f);
        ubo.lightColor = glm::vec4(1.0f, 0.96f, 0.90f, 1.0f);
        ubo.cameraPos  = glm::vec4(eye, 1.0f);

        if (kind == MeshKind::Sphere) {
            // 球体：每帧绕 Y 轴自转，悬浮在 Y=1
            ubo.model          = glm::rotate(glm::mat4(1.0f), t * glm::radians(45.0f), glm::vec3(0.0f, 1.0f, 0.0f))
                              * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            ubo.baseColor      = glm::vec4(0.85f, 0.55f, 0.25f, 0.85f); // 金属铜色（metallic=0.85）
            ubo.materialParams = glm::vec4(0.35f, 1.0f, 0.0f, 0.0f);     // roughness=0.35, ao=1.0
        } else {
            // plane：静止在原点
            ubo.model          = glm::mat4(1.0f);
            ubo.baseColor      = glm::vec4(0.45f, 0.45f, 0.50f, 0.0f);   // 灰蓝非金属
            ubo.materialParams = glm::vec4(0.85f, 1.0f, 0.0f, 0.0f);     // roughness=0.85
        }

        std::memcpy(uniformBuffersMapped_[currentFrame_], &ubo, sizeof(ubo));
    }

    // -------------------------------------------------------------------------
    // 同步对象
    // -------------------------------------------------------------------------
    void createSyncObjects() {
        imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
        inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (vkCreateSemaphore(device_, &semInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device_, &semInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
                vkCreateFence    (device_, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS) {
                throw std::runtime_error("failed to create sync objects");
            }
        }
    }

    // -------------------------------------------------------------------------
    // 渲染循环
    // -------------------------------------------------------------------------
    void drawFrame() {
        vkWaitForFences(device_, 1, &inFlightFences_[currentFrame_], VK_TRUE, UINT64_MAX);
        // UBO 更新在 recordCommandBuffer 内部按 mesh 顺序进行（球体 → plane）

        uint32_t imageIndex = 0;
        const VkResult result = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
            imageAvailableSemaphores_[currentFrame_], VK_NULL_HANDLE, &imageIndex);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            framebufferResized_ = true;
            return;
        }
        if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            throw std::runtime_error("vkAcquireNextImageKHR failed");
        }

        vkResetFences(device_, 1, &inFlightFences_[currentFrame_]);
        vkResetCommandBuffer(commandBuffers_[currentFrame_], 0);
        recordCommandBuffer(commandBuffers_[currentFrame_], imageIndex);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType                 = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount    = 1;
        submit.pWaitSemaphores       = &imageAvailableSemaphores_[currentFrame_];
        submit.pWaitDstStageMask     = &waitStage;
        submit.commandBufferCount    = 1;
        submit.pCommandBuffers       = &commandBuffers_[currentFrame_];
        submit.signalSemaphoreCount  = 1;
        submit.pSignalSemaphores     = &renderFinishedSemaphores_[currentFrame_];
        if (vkQueueSubmit(graphicsQueue_, 1, &submit, inFlightFences_[currentFrame_]) != VK_SUCCESS) {
            throw std::runtime_error("vkQueueSubmit failed");
        }

        VkPresentInfoKHR present{};
        present.sType               = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount  = 1;
        present.pWaitSemaphores     = &renderFinishedSemaphores_[currentFrame_];
        present.swapchainCount      = 1;
        present.pSwapchains         = &swapchain_;
        present.pImageIndices       = &imageIndex;
        const VkResult presentResult = vkQueuePresentKHR(presentQueue_, &present);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
            framebufferResized_ = true;
        } else if (presentResult != VK_SUCCESS) {
            throw std::runtime_error("vkQueuePresentKHR failed");
        }

        currentFrame_ = (currentFrame_ + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    void cleanupSwapchain() {
        vkDestroyImageView(device_, depthImageView_, nullptr);
        vkDestroyImage    (device_, depthImage_,      nullptr);
        vkFreeMemory      (device_, depthMemory_,     nullptr);
        for (auto fb : swapchainFramebuffers_) {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }
        for (auto iv : swapchainImageViews_) {
            vkDestroyImageView(device_, iv, nullptr);
        }
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }

    void recreateSwapchain() {
        vkDeviceWaitIdle(device_);
        cleanupSwapchain();
        createSwapchain();
        createImageViews();
        createDepthResources();
        createFramebuffers();
    }

    // -------------------------------------------------------------------------
    // Win32 message proc
    // -------------------------------------------------------------------------
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_CREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return 0;
        }
        PBRDemoApp* self = reinterpret_cast<PBRDemoApp*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (self != nullptr) {
            if (msg == WM_SIZE) {
                self->framebufferResized_ = true;
                return 0;
            }
            if (msg == WM_CLOSE || msg == WM_DESTROY) {
                PostQuitMessage(0);
                return 0;
            }
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
};

// -----------------------------------------------------------------------------
// WinMain
// -----------------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    PBRDemoApp app;
    try {
        app.run(hInstance);
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "PBR Demo", MB_OK | MB_ICONERROR);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
