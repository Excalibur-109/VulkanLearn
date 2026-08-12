#include "Win32Window.h"

#if defined(_WIN32)
#include <windows.h>

namespace pbrdemo {
namespace {

struct WindowState {
    WindowRenderer* renderer = nullptr;
};

rhi::Extent2D clientExtent(HWND window) {
    RECT clientRect{};
    GetClientRect(window, &clientRect);
    return {
        static_cast<uint32_t>(clientRect.right - clientRect.left),
        static_cast<uint32_t>(clientRect.bottom - clientRect.top)};
}

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrA(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_SIZE:
        if (state != nullptr && state->renderer != nullptr &&
            LOWORD(lParam) != 0 && HIWORD(lParam) != 0) {
            state->renderer->resize(clientExtent(window));
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        BeginPaint(window, &paint);
        if (state != nullptr && state->renderer != nullptr) state->renderer->renderFrame();
        EndPaint(window, &paint);
        return 0;
    }

    case WM_TIMER:
        if (state != nullptr && state->renderer != nullptr) state->renderer->renderFrame();
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        KillTimer(window, 1);
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(window, message, wParam, lParam);
    }
}

} // 匿名命名空间

int runPbrWindow(WindowRenderer& renderer) {
    HINSTANCE instance = GetModuleHandleA(nullptr);
    constexpr const char* className = "RHI_PBR_WINDOW";

    WNDCLASSA windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassA(&windowClass);

    WindowState state{&renderer};
    HWND window = CreateWindowExA(
        0, className, "RHI PBR Scene - Native Graphics Backend",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
        nullptr, nullptr, instance, &state);
    if (window == nullptr) return 1;

    if (!renderer.initialize(window, clientExtent(window))) {
        DestroyWindow(window);
        return 1;
    }

    SetTimer(window, 1, 16, nullptr);
    MSG message{};
    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return static_cast<int>(message.wParam);
}

} // 命名空间 pbrdemo
#else
namespace pbrdemo {
int runPbrWindow(WindowRenderer&) { return 0; }
} // 命名空间 pbrdemo
#endif
