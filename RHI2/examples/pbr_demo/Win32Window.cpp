#include "Win32Window.h"

#if defined(_WIN32)
#include "SoftwareRenderer.h"
#include <cstring>
#include <windows.h>

namespace pbrdemo {
namespace {
struct WindowState {
    PbrMaterial material;
    std::vector<uint8_t> pixels;
    int width = 0;
    int height = 0;
    float lightPhase = 0.0f;
    BITMAPINFO bitmap{};
};

void refresh(WindowState& state, int width, int height) {
    if (width <= 0 || height <= 0) return;
    state.width = width;
    state.height = height;
    state.pixels = renderScenePixels(state.material, width, height, state.lightPhase);
    std::memset(&state.bitmap, 0, sizeof(state.bitmap));
    state.bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    state.bitmap.bmiHeader.biWidth = width;
    state.bitmap.bmiHeader.biHeight = -height; // 使用从上到下的 DIB 布局。
    state.bitmap.bmiHeader.biPlanes = 1;
    state.bitmap.bmiHeader.biBitCount = 24;
    state.bitmap.bmiHeader.biCompression = BI_RGB;
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* state = reinterpret_cast<WindowState*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTA*>(lParam);
        state = static_cast<WindowState*>(create->lpCreateParams);
        SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_SIZE:
        if (state) refresh(*state, LOWORD(lParam), HIWORD(lParam));
        return 0;
    case WM_TIMER:
        if (state) {
            state->lightPhase += 0.035f;
            refresh(*state, state->width, state->height);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd, &paint);
        if (state && !state->pixels.empty()) {
            StretchDIBits(dc, 0, 0, state->width, state->height,
                          0, 0, state->width, state->height,
                          state->pixels.data(), &state->bitmap,
                          DIB_RGB_COLORS, SRCCOPY);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, message, wParam, lParam);
    }
}
} // 匿名命名空间

int runPbrWindow(const PbrMaterial& material) {
    HINSTANCE instance = GetModuleHandleA(nullptr);
    constexpr const char* className = "RHI_PBR_SPHERE_WINDOW";
    WNDCLASSA windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = windowProc;
    windowClass.lpszClassName = className;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassA(&windowClass);

    WindowState state;
    state.material = material;
    HWND window = CreateWindowExA(
        0, className, "RHI PBR Sphere - Vulkan / D3D11 / D3D12 RHI",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
        900, 700, nullptr, nullptr, instance, &state);
    if (!window) return 1;

    SetTimer(window, 1, 33, nullptr); // 教学示例约以 30 FPS 刷新。
    MSG message{};
    while (GetMessageA(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return static_cast<int>(message.wParam);
}

} // 命名空间 pbrdemo
#else
namespace pbrdemo { int runPbrWindow(const PbrMaterial&) { return 0; } }
#endif
