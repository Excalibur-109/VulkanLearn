#include "PlaneMesh.h"

namespace pbrdemo {

void makeGroundPlane(float halfExtent, std::vector<SphereVertex>& vertices,
                     std::vector<uint32_t>& indices) {
    vertices = {
        {-halfExtent, 0.0f, -halfExtent, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
        { halfExtent, 0.0f, -halfExtent, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f},
        { halfExtent, 0.0f,  halfExtent, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f},
        {-halfExtent, 0.0f,  halfExtent, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f}};
    indices = {0, 1, 2, 0, 2, 3};
}

} // 命名空间 pbrdemo
