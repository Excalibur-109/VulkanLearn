#include "CommandBuffer.hpp"

namespace ecs {

CommandBuffer::~CommandBuffer() = default;

void CommandBuffer::Playback(World& world) {
    playback_.Clear();
    recording_.Swap(playback_);
    try {
        for (Command& command : playback_.commands) {
            command.Execute(world);
        }
    } catch (...) {
        playback_.Clear();
        throw;
    }
    playback_.Clear();
}

void CommandBuffer::Clear() noexcept {
    recording_.Clear();
    playback_.Clear();
}

std::size_t CommandBuffer::ReservedBytes() const noexcept {
    return recording_.arena.ReservedBytes() + playback_.arena.ReservedBytes();
}

} // namespace ecs

