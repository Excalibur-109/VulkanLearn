#include "LinearArena.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace ecs {
namespace {

[[nodiscard]] std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1U) & ~(alignment - 1U);
}

} // namespace

LinearArena::LinearArena(std::size_t defaultBlockSize) noexcept
    : defaultBlockSize_(std::max<std::size_t>(defaultBlockSize, 1024U)) {
}

LinearArena::~LinearArena() {
    Release();
}

LinearArena::LinearArena(LinearArena&& other) noexcept {
    Swap(other);
}

LinearArena& LinearArena::operator=(LinearArena&& other) noexcept {
    if (this != std::addressof(other)) {
        Release();
        Swap(other);
    }
    return *this;
}

void* LinearArena::Allocate(std::size_t size, std::size_t alignment) {
    if (alignment == 0 || !std::has_single_bit(alignment)) {
        throw std::invalid_argument("LinearArena alignment must be a non-zero power of two");
    }
    size = std::max<std::size_t>(size, 1U);

    for (std::size_t index = currentBlock_; index < blocks_.size(); ++index) {
        if (void* memory = TryAllocate(blocks_[index], size, alignment); memory != nullptr) {
            currentBlock_ = index;
            return memory;
        }
    }

    const std::size_t blockAlignment = std::max(alignment, alignof(std::max_align_t));
    if (size > std::numeric_limits<std::size_t>::max() - blockAlignment) {
        throw std::bad_alloc();
    }
    const std::size_t capacity = AlignUp(
        std::max(defaultBlockSize_, size + blockAlignment),
        blockAlignment);
    Block block{};
    block.memory    = static_cast<std::byte*>(AllocateRawMemory(capacity, blockAlignment));
    block.capacity  = capacity;
    block.alignment = blockAlignment;

    try {
        blocks_.push_back(block);
    } catch (...) {
        FreeRawMemory(block.memory, block.alignment);
        throw;
    }
    currentBlock_ = blocks_.size() - 1U;
    return TryAllocate(blocks_.back(), size, alignment);
}

void LinearArena::Reset() noexcept {
    for (Block& block : blocks_) {
        block.offset = 0;
    }
    currentBlock_ = 0;
}

void LinearArena::Release() noexcept {
    for (const Block& block : blocks_) {
        FreeRawMemory(block.memory, block.alignment);
    }
    blocks_.clear();
    currentBlock_ = 0;
}

void LinearArena::Swap(LinearArena& other) noexcept {
    blocks_.swap(other.blocks_);
    std::swap(currentBlock_, other.currentBlock_);
    std::swap(defaultBlockSize_, other.defaultBlockSize_);
}

std::size_t LinearArena::ReservedBytes() const noexcept {
    std::size_t result = 0;
    for (const Block& block : blocks_) {
        result += block.capacity;
    }
    return result;
}

void* LinearArena::TryAllocate(
    Block& block,
    std::size_t size,
    std::size_t alignment) noexcept {
    if (block.alignment < alignment) {
        return nullptr;
    }
    const std::size_t alignedOffset = AlignUp(block.offset, alignment);
    if (alignedOffset > block.capacity || size > block.capacity - alignedOffset) {
        return nullptr;
    }
    void* result = block.memory + alignedOffset;
    block.offset = alignedOffset + size;
    return result;
}

} // namespace ecs
