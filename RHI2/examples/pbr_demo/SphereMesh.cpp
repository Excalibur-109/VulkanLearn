#include "SphereMesh.h"

#include <cmath>

namespace pbrdemo {

void makeSphere(uint32_t slices, uint32_t stacks,
                std::vector<SphereVertex>& vertices,
                std::vector<uint32_t>& indices) {
    constexpr float pi = 3.14159265f;
    constexpr float twoPi = 6.28318530f;

    vertices.clear();
    indices.clear();
    vertices.reserve((slices + 1) * (stacks + 1));
    indices.reserve(slices * stacks * 6);

    for (uint32_t y = 0; y <= stacks; ++y) {
        const float v = static_cast<float>(y) / static_cast<float>(stacks);
        const float phi = v * pi;
        for (uint32_t x = 0; x <= slices; ++x) {
            const float u = static_cast<float>(x) / static_cast<float>(slices);
            const float theta = u * twoPi;
            const Vec3 position{
                std::sin(phi) * std::cos(theta),
                std::cos(phi),
                std::sin(phi) * std::sin(theta)};
            vertices.push_back({position.x, position.y, position.z,
                                position.x, position.y, position.z, u, v});
        }
    }

    for (uint32_t y = 0; y < stacks; ++y) {
        for (uint32_t x = 0; x < slices; ++x) {
            const uint32_t a = y * (slices + 1) + x;
            const uint32_t b = a + 1;
            const uint32_t c = a + slices + 1;
            const uint32_t d = c + 1;
            indices.insert(indices.end(), {a, c, b, b, c, d});
        }
    }
}

} // 命名空间 pbrdemo
