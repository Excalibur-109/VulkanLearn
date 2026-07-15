#pragma once

#include "Chunk.hpp"
#include "Signature.hpp"

#include <memory>
#include <span>
#include <vector>

namespace ecs {

class Archetype;

struct ArchetypeColumnMove {
    const ComponentInfo* info = nullptr;
    std::size_t sourceOffset  = 0;
    std::size_t targetOffset  = 0;
};

/// 首次结构迁移后缓存目标 Archetype 和所有公共组件的源/目标列偏移。
struct ArchetypeTransition {
    Archetype* target               = nullptr;
    std::size_t changedTargetOffset = 0;
    std::vector<ArchetypeColumnMove> moves;
};

/// 相同 Signature 的实体集合；负责 Chunk 对象，不负责全局 Entity 位置表。
class Archetype {
public:
    ECS_API Archetype(Signature signature, const ComponentRegistry& registry);
    ECS_API ~Archetype();

    Archetype(const Archetype&)            = delete;
    Archetype& operator=(const Archetype&) = delete;
    Archetype(Archetype&&)                 = delete;
    Archetype& operator=(Archetype&&)      = delete;

    [[nodiscard]] const Signature& GetSignature() const noexcept { return signature_; }
    [[nodiscard]] bool Has(ComponentTypeId type) const noexcept { return signature_.Test(type); }
    [[nodiscard]] std::size_t EntityCount() const noexcept { return entityCount_; }
    [[nodiscard]] const ChunkLayout& GetChunkLayout() const noexcept { return layout_; }
    [[nodiscard]] std::span<const std::unique_ptr<Chunk>> Chunks() const noexcept { return chunks_; }

private:
    friend class World;
    template <typename... Components>
    friend class Query;

    [[nodiscard]] ECS_API ChunkLocation Allocate(Entity entity);
    [[nodiscard]] ECS_API std::optional<Entity> Remove(Chunk& chunk, u32 row) noexcept;

    Signature signature_;
    std::vector<const ComponentInfo*> componentInfos_;
    ChunkLayout layout_;
    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::vector<std::unique_ptr<ArchetypeTransition>> addTransitions_;
    std::vector<std::unique_ptr<ArchetypeTransition>> removeTransitions_;
    std::size_t entityCount_ = 0;
};

} // namespace ecs

