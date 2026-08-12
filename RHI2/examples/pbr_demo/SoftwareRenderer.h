#pragma once

#include "PbrMath.h"

#include <cstdint>
#include <string>
#include <vector>

namespace pbrdemo {

// 返回按从上到下排列的 24 位 RGB 像素，供软件预览窗口显示。
std::vector<uint8_t> renderScenePixels(const PbrMaterial& material,
                                        int width, int height, float lightPhase);

// 写出便于调试的标准图像文件，其中 BMP 可以在 Windows 中直接打开。
void writeSceneImages(const PbrMaterial& material,
                       const std::string& ppmPath,
                       const std::string& bmpPath);

} // 命名空间 pbrdemo
