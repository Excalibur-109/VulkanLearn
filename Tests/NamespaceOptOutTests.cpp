#define MATH_DISABLE_GLOBAL_NAMESPACE_EXPORTS
#include "Math.hpp"

// 单独的翻译单元验证关闭全局导出后，原始 math:: API 仍可正常编译和使用。
void CompileNamespaceOptOutAPI() {
    const math::float3 direction = math::Normalize(math::float3(0.0F, 1.0F, 0.0F));
    const math::float4x4 transform = math::TranslationMatrix(direction);
    const math::Color color(math::ToneMapACES(math::float3(1.0F)), 1.0F);
    static_cast<void>(transform);
    static_cast<void>(color);
}
