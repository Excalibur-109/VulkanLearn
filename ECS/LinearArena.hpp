#pragma once

#include "Memory.hpp"

#include <cstddef>
#include <vector>

namespace ecs {

/**
 * 只增不减的页式分配器。Allocate 只移动 offset，Reset 一次性复用全部页面。
 * 它适合帧临时数据和 CommandBuffer，不负责调用对象析构。
 */
class LinearArena {
public:
    ECS_API explicit LinearArena(std::size_t defaultBlockSize = 64U * 1024U) noexcept;
    ECS_API ~LinearArena();

    LinearArena(const LinearArena&)            = delete;
    LinearArena& operator=(const LinearArena&) = delete;
    ECS_API LinearArena(LinearArena&& other) noexcept;
    ECS_API LinearArena& operator=(LinearArena&& other) noexcept;

    [[nodiscard]] ECS_API void* Allocate(std::size_t size, std::size_t alignment);
    ECS_API void Reset() noexcept;
    ECS_API void Release() noexcept;
    ECS_API void Swap(LinearArena& other) noexcept;

    [[nodiscard]] ECS_API std::size_t ReservedBytes() const noexcept;

private:
    struct Block {
        std::byte* memory        = nullptr;
        std::size_t capacity     = 0;
        std::size_t offset       = 0;
        std::size_t alignment    = 1;
    };

    [[nodiscard]] void* TryAllocate(Block& block, std::size_t size, std::size_t alignment) noexcept;

    std::vector<Block> blocks_;
    std::size_t currentBlock_     = 0;
    std::size_t defaultBlockSize_ = 64U * 1024U;
};

} // namespace ecs
