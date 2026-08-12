#pragma once

#include "rhi/RHI.h"

namespace pbrdemo {

// 窗口层只处理 HWND 和消息循环；渲染器负责后端设备、资源与每帧命令录制。
// 这样 Win32 代码不会渗透到 RHI 或具体图形后端中。
class WindowRenderer {
public:
    virtual ~WindowRenderer() = default;

    virtual bool initialize(void* nativeWindow, rhi::Extent2D extent) = 0;
    virtual void resize(rhi::Extent2D extent) = 0;
    virtual void renderFrame() = 0;
};

// 创建并维护驻留的 Win32 HWND，按 Esc 键关闭窗口。
int runPbrWindow(WindowRenderer& renderer);

} // 命名空间 pbrdemo
