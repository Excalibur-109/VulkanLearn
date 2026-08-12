#pragma once

#include "PbrMath.h"

namespace pbrdemo {

// 创建并维护驻留的 Win32 HWND，按 Esc 键关闭窗口。
int runPbrWindow(const PbrMaterial& material);

} // 命名空间 pbrdemo
