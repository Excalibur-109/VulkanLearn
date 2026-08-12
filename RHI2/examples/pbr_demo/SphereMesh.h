#pragma once

#include "PbrMath.h"
#include <cstdint>
#include <vector>

namespace pbrdemo {

struct SphereVertex {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
};

// 创建单位 UV 球。顶点布局与 main.cpp 中 RHI 管线声明的三个属性一致。
void makeSphere(uint32_t slices, uint32_t stacks,
                std::vector<SphereVertex>& vertices,
                std::vector<uint32_t>& indices);

} // 命名空间 pbrdemo
