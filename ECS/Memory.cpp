#include "Memory.hpp"

#include <bit>
#include <cstdlib>
#include <limits>
#include <new>
#include <stdexcept>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace ecs {
namespace {

[[nodiscard]] std::size_t AlignUp(std::size_t value, std::size_t alignment) {
    if (value > std::numeric_limits<std::size_t>::max() - (alignment - 1U)) {
        throw std::bad_alloc();
    }
    return (value + alignment - 1U) & ~(alignment - 1U);
}

} // namespace

void* AllocateRawMemory(std::size_t size, std::size_t alignment) {
    if (alignment == 0 || !std::has_single_bit(alignment)) {
        throw std::invalid_argument("Raw memory alignment must be a non-zero power of two");
    }

    const std::size_t allocationSize = size == 0 ? 1 : size;
    void* memory                     = nullptr;

    if (alignment <= alignof(std::max_align_t)) {
        memory = std::malloc(allocationSize);
    } else {
#if defined(_WIN32)
        memory = _aligned_malloc(allocationSize, alignment);
#else
        memory = std::aligned_alloc(alignment, AlignUp(allocationSize, alignment));
#endif
    }

    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

void FreeRawMemory(void* memory, std::size_t alignment) noexcept {
    if (memory == nullptr) {
        return;
    }

#if defined(_WIN32)
    if (alignment > alignof(std::max_align_t)) {
        _aligned_free(memory);
        return;
    }
#else
    (void)alignment;
#endif
    std::free(memory);
}

} // namespace ecs
