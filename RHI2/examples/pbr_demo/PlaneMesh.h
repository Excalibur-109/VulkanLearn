#pragma once

#include "SphereMesh.h"

namespace pbrdemo {

// 创建居中的 XZ 地面平面。向上的法线使地面可以复用 PBR 光照代码，
// 同时接收球体投射的阴影。
void makeGroundPlane(float halfExtent, std::vector<SphereVertex>& vertices,
                     std::vector<uint32_t>& indices);

} // 命名空间 pbrdemo
