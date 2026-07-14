#pragma once

#include "Chunk.hpp"
#include "Signature.hpp"

#include <memory>
#include <span>
#include <vector>

namespace ecs {

/**
 * @brief 拥有完全相同组件组合的一组实体。
 *
 * Archetype 只创建 Chunk 和提供列定位；实体记录更新与跨 Archetype 迁移由 World 统一
 * 处理，避免多个模块同时修改 Entity -> Chunk/row 映射。
 */
class Archetype {
public:
    Archetype(Signature signature, const ComponentRegistry& registry);

    [[nodiscard]] const Signature& GetSignature() const noexcept {
        return signature_;
    }

    [[nodiscard]] bool Has(ComponentTypeId componentType) const noexcept {
        return signature_.Test(componentType);
    }

    [[nodiscard]] std::size_t EntityCount() const noexcept {
        return entityCount_;
    }

    [[nodiscard]] const ChunkLayout& GetChunkLayout() const noexcept {
        return layout_;
    }

    [[nodiscard]] std::span<const std::unique_ptr<Chunk>> Chunks() const noexcept {
        return chunks_;
    }

private:
    friend class World;
    template <typename... Components>
    friend class Query;

    [[nodiscard]] ChunkLocation Allocate(Entity entity);
    [[nodiscard]] std::optional<Entity> Remove(Chunk& chunk, u32 row) noexcept;

    Signature signature_;
    std::vector<const ComponentInfo*> componentInfos_;
    ChunkLayout layout_;
    std::vector<std::unique_ptr<Chunk>> chunks_;
    std::size_t entityCount_ = 0;
};

} // namespace ecs

