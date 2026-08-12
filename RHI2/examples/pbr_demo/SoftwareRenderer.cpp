#include "SoftwareRenderer.h"

#include <cmath>
#include <fstream>

namespace pbrdemo {

std::vector<uint8_t> renderScenePixels(const PbrMaterial& material,
                                        int width, int height, float lightPhase) {
    std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 3);
    const Vec3 camera{0.0f, 1.65f, 4.2f};
    const Vec3 sphereCenter{0.0f, 1.0f, 0.0f};
    const Vec3 light{-2.5f * std::cos(lightPhase), 4.0f,
                     2.5f + 1.2f * std::sin(lightPhase)};

    auto toneMap = [](float value) {
        return static_cast<uint8_t>(255.0f * std::pow(
            saturate(value / (value + 1.0f)), 1.0f / 2.2f));
    };

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float screenX = (2.0f * (x + 0.5f) / width - 1.0f) * 1.05f;
            const float screenY = (1.0f - 2.0f * (y + 0.5f) / height) * 1.05f;
            const Vec3 ray = normalize({screenX, screenY + 0.1f, -3.2f});
            const Vec3 sphereRayOrigin = camera - sphereCenter;
            const float b = dot(sphereRayOrigin, ray);
            const float c = dot(sphereRayOrigin, sphereRayOrigin) - 1.0f;
            const float discriminant = b * b - c;
            Vec3 color{0.018f, 0.022f, 0.03f};
            float hit = 1.0e30f; bool sphereHit = false;
            if (discriminant >= 0.0f) { const float nearHit = -b - std::sqrt(discriminant); if (nearHit > 0.0f) { hit=nearHit; sphereHit=true; } }
            const float planeHit = ray.y < -0.0001f ? -camera.y / ray.y : 1.0e30f;
            if (sphereHit || planeHit < hit) {
                const Vec3 point = camera + ray * (sphereHit ? hit : planeHit);
                const Vec3 normal = sphereHit ? normalize(point - sphereCenter) : Vec3{0,1,0};
                const Vec3 toLight = light - point; const float lightDistance = std::sqrt(dot(toLight,toLight));
                const Vec3 lightDirection = toLight * (1.0f / lightDistance); const Vec3 viewDirection = normalize(camera-point);
                // 硬阴影测试：如果球体挡住了光线，就降低该地面点的光照。
                const Vec3 shadowOrigin = point + normal * 0.002f; const Vec3 shadowRay = lightDirection;
                const Vec3 shadowSphereOrigin = shadowOrigin - sphereCenter; const float shadowB=dot(shadowSphereOrigin,shadowRay), shadowC=dot(shadowSphereOrigin,shadowSphereOrigin)-1.0f; const float shadowDisc=shadowB*shadowB-shadowC;
                const bool occluded = shadowDisc > 0.0f && (-shadowB-std::sqrt(shadowDisc)) > 0.0f && sphereHit == false;
                PbrMaterial surface = material;
                if (!sphereHit) {
                    surface.albedo = {0.52f, 0.55f, 0.60f};
                    surface.metallic = 0.0f;
                    surface.roughness = 0.82f;
                }
                const float visibility = occluded ? 0.025f : 1.0f;
                const float attenuation = 1.0f / (lightDistance * lightDistance);
                color = Vec3{0.025f, 0.03f, 0.04f} + evaluatePbr(
                    surface, normal, viewDirection, lightDirection,
                    {22.0f * attenuation * visibility,
                     20.0f * attenuation * visibility,
                     18.0f * attenuation * visibility});
            }

            const size_t index = (static_cast<size_t>(y) * width + x) * 3;
            pixels[index + 0] = toneMap(color.x);
            pixels[index + 1] = toneMap(color.y);
            pixels[index + 2] = toneMap(color.z);
        }
    }
    return pixels;
}

void writeSceneImages(const PbrMaterial& material,
                       const std::string& ppmPath,
                       const std::string& bmpPath) {
    constexpr int width = 512;
    constexpr int height = 512;
    const auto pixels = renderScenePixels(material, width, height, 0.0f);

    std::ofstream ppm(ppmPath, std::ios::binary);
    ppm << "P6\n" << width << " " << height << "\n255\n";
    ppm.write(reinterpret_cast<const char*>(pixels.data()),
              static_cast<std::streamsize>(pixels.size()));

    const int rowBytes = (width * 3 + 3) & ~3;
    const int pixelBytes = rowBytes * height;
    const int fileBytes = 54 + pixelBytes;
    uint8_t header[54]{};
    header[0] = 'B'; header[1] = 'M';
    auto put32 = [&header](int offset, uint32_t value) {
        header[offset + 0] = static_cast<uint8_t>(value & 255);
        header[offset + 1] = static_cast<uint8_t>((value >> 8) & 255);
        header[offset + 2] = static_cast<uint8_t>((value >> 16) & 255);
        header[offset + 3] = static_cast<uint8_t>((value >> 24) & 255);
    };
    put32(2, fileBytes); put32(10, 54); put32(14, 40);
    put32(18, width); put32(22, height); header[26] = 1; header[28] = 24;
    put32(34, pixelBytes);

    std::ofstream bmp(bmpPath, std::ios::binary);
    bmp.write(reinterpret_cast<const char*>(header), sizeof(header));
    std::vector<uint8_t> row(static_cast<size_t>(rowBytes));
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const size_t source = (static_cast<size_t>(y) * width + x) * 3;
            row[x * 3 + 0] = pixels[source + 2];
            row[x * 3 + 1] = pixels[source + 1];
            row[x * 3 + 2] = pixels[source + 0];
        }
        bmp.write(reinterpret_cast<const char*>(row.data()), rowBytes);
    }
}

} // 命名空间 pbrdemo
