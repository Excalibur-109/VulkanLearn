#pragma once

#include "ECSApi.hpp"

#include <cstddef>

namespace ecs {

/**
 * 分配没有对象生命周期的原始内存。普通对齐使用 malloc；超过 max_align_t 的对齐
 * 使用平台 aligned C 堆接口，避免 alignas(32/64) 组件产生未定义行为。
 */
[[nodiscard]] ECS_API void* AllocateRawMemory(std::size_t size, std::size_t alignment);

/// alignment 必须与 AllocateRawMemory 时一致。
ECS_API void FreeRawMemory(void* memory, std::size_t alignment) noexcept;

} // namespace ecs
