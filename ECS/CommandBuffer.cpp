#include "CommandBuffer.hpp"

namespace ecs {

void CommandBuffer::Playback(World& world) {
    // 先交换到局部数组：命令执行期间新写入本 CommandBuffer 的操作留到下一次 Playback。
    std::vector<std::unique_ptr<CommandBase>> pending;
    pending.swap(commands_);
    for (const std::unique_ptr<CommandBase>& command : pending) {
        command->Execute(world);
    }
}

} // namespace ecs

