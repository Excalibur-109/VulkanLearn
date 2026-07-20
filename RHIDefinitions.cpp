#include "RHIDefinitions.hpp"

#include <type_traits>

namespace rhi {

static_assert(sizeof(u8)  == 1);
static_assert(sizeof(u16) == 2);
static_assert(sizeof(u32) == 4);
static_assert(sizeof(u64) == 8);
static_assert(sizeof(i32) == 4);

static_assert(std::is_trivially_copyable_v<RHIBuffer>);
static_assert(std::is_trivially_copyable_v<RHITexture>);
static_assert(std::is_same_v<std::underlying_type_t<RHIGraphicsAPI>, u8>);
static_assert(std::is_same_v<std::underlying_type_t<RHIRenderFeature>, u64>);
static_assert(RHIEnumFlags<RHITextureAspect>);
static_assert(!RHIEnumFlags<RHIFormat>);
static_assert(RHIHasAll(
    RHITextureAspect::Depth | RHITextureAspect::Stencil,
    RHITextureAspect::Depth | RHITextureAspect::Stencil));

} // namespace rhi






