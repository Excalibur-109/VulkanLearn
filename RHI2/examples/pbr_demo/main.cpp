#include "NativeSceneRenderer.h"
#include "SoftwareRenderer.h"
#include "Win32Window.h"

#include <iostream>
#include <string>

namespace {

rhi::Backend parseBackend(int argc, char** argv) {
#if defined(_WIN32)
    rhi::Backend backend = rhi::Backend::D3D11;
#else
    rhi::Backend backend = rhi::Backend::Software;
#endif

    if (argc < 2) return backend;
    const std::string name = argv[1];
    if (name == "software") return rhi::Backend::Software;
    if (name == "vulkan") return rhi::Backend::Vulkan;
    if (name == "d3d11") return rhi::Backend::D3D11;
    if (name == "d3d12") return rhi::Backend::D3D12;
    return backend;
}

bool hasNoWaitFlag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--no-wait") return true;
    }
    return false;
}

} // 匿名命名空间

int main(int argc, char** argv) {
    try {
        const rhi::Backend backend = parseBackend(argc, argv);

    // 无窗口模式只用于快速验证 CPU 参考渲染器和生成调试图像。
    // 真正的 GPU 呈现必须通过 HWND 创建原生交换链。
        if (hasNoWaitFlag(argc, argv)) {
            if (backend != rhi::Backend::Software) {
                std::cerr << "--no-wait 只支持 software 后端；D3D11/Vulkan/D3D12 必须创建 HWND。\n";
                return 2;
            }
            pbrdemo::PbrMaterial material;
            pbrdemo::writeSceneImages(material, "pbr_scene.ppm", "pbr_scene.bmp");
            std::cout << "已生成软件参考图像：pbr_scene.bmp\n";
            return 0;
        }

        if (backend == rhi::Backend::Software) {
            std::cerr << "software 后端只用于离线参考图像，请使用：pbr_demo software --no-wait\n";
            return 2;
        }

        pbrdemo::NativeSceneRenderer renderer(backend);
        return pbrdemo::runPbrWindow(renderer);
    } catch (const std::exception& error) {
        std::cerr << "启动渲染器失败：" << error.what() << '\n';
        return 1;
    }
}
