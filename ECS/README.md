# Archetype ECS

本目录是一套独立于 RHI 和旧 Render 代码的 C++20 Archetype ECS。公共入口为
`ECS/ECS.hpp`，命名空间为 `ecs`。

## 1. 核心数据结构

| 类型 | 责任 |
| --- | --- |
| `Entity` | `{index, generation}` 轻量句柄，不保存组件地址 |
| `ComponentRegistry` | 将 C++ 组件类型映射为 World 内稳定的 `ComponentTypeId`，保存移动/析构操作表 |
| `Signature` | 动态 bitset，第 N 位表示是否含有组件类型 N |
| `Archetype` | 拥有同一种 Signature 的所有实体和 Chunk |
| `Chunk` | 固定容量的 SoA 组件内存，默认目标大小 16 KiB |
| `World` | 拥有实体槽位、注册表和 Archetype，统一完成实体迁移 |
| `Filter` | `all + any + none` 三组 Archetype 匹配条件 |
| `Query<T...>` | 缓存匹配的 Archetype，并按行或按 Chunk 遍历 |
| `CommandBuffer` | 延迟结构变化，避免查询过程中使当前 Chunk/row 失效 |
| `System` | 一组长期运行的游戏规则，不拥有实体数据 |
| `SystemScheduler` | 按 order 调用 System，并在每个 System 后回放命令 |

## 2. 内存布局

假设某个 Archetype 的 Signature 为 `{Position, Rotation, Velocity}`，一个 Chunk 不是
按对象交错存储，而是按组件列分开：

```text
Archetype: Position + Rotation + Velocity

Chunk (一次对齐分配)
+----------------------------+  entityOffset
| Entity[capacity]           |  Entity 反查句柄
+----------------------------+  positionOffset（按 alignof(Position) 对齐）
| Position[capacity]         |  系统连续读取的位置列
+----------------------------+  rotationOffset
| Rotation[capacity]         |
+----------------------------+  velocityOffset
| Velocity[capacity]         |
+----------------------------+
```

它是 SoA（Structure of Arrays），不是：

```text
Entity0: Position Rotation Velocity
Entity1: Position Rotation Velocity
Entity2: Position Rotation Velocity
```

运动系统只访问 Position 和 Velocity 时，CPU 不必把 Rotation 一起拉入缓存。每一列会
按组件自己的 `alignof(T)` 对齐，因此 over-aligned 组件也可以安全使用。

World 另外保存稀疏反向索引：

```text
records[entity.index] -> { generation, alive, Archetype*, Chunk*, row }
```

所以 `Get<Position>(entity)` 不需要扫描 Archetype。generation 能检测已经销毁的旧句柄。

## 3. 增删组件为何会迁移

Archetype 由完整组件集合定义。给 `{Position, Velocity}` 实体添加 `Health` 的流程为：

1. 计算目标 Signature `{Position, Velocity, Health}`。
2. 查找或创建目标 Archetype。
3. 在目标 Chunk 预留一行，先构造可能抛异常的新 Health。
4. 将 Position、Velocity 移动到目标列。
5. 在源 Chunk 执行 swap-remove，用最后一行填补空洞。
6. 修正当前实体和被交换实体的 `{Chunk*, row}` 记录。

所有组件必须可析构且 `nothrow move constructible`。这样第 4～6 步不会因异常中断；
新组件若在第 3 步构造失败，目标行会回滚，原实体保持不变。

## 4. 基本用法

```cpp
#include "ECS/ECS.hpp"

struct Position { float x; float y; float z; };
struct Velocity { float x; float y; float z; };
struct Disabled {};

ecs::World world;
const ecs::Entity player = world.CreateEntity(
    Position{0.0F, 0.0F, 0.0F},
    Velocity{1.0F, 0.0F, 0.0F});

world.Add<Disabled>(player);
world.Remove<Disabled>(player);
Position& position = world.Get<Position>(player);
```

组件可以显式注册名称，便于编辑器和调试器展示：

```cpp
world.RegisterComponent<Position>("Position");
```

## 5. Query 与 Filter

```cpp
auto moving = world.MakeQuery<Position, const Velocity>();
moving.WithAny<Player, Enemy>().Without<Disabled>();

moving.Each([](ecs::Entity entity, Position& position, const Velocity& velocity) {
    position.x += velocity.x;
});
```

- 模板参数自动进入 `Filter::all`。
- `With<T...>()` 添加额外的 all 条件，但不把这些组件传给回调。
- `WithAny<T...>()` 要求至少出现一种。
- `Without<T...>()` 排除任意含这些组件的 Archetype。
- 模板参数写成 `const T` 时，回调只能得到 `const T&`。

整 Chunk 遍历适合 SIMD 或提交到 Job System：

```cpp
moving.EachChunk([](
    std::span<const ecs::Entity> entities,
    std::span<Position> positions,
    std::span<const Velocity> velocities) {
    for (std::size_t i = 0; i < entities.size(); ++i) {
        positions[i].x += velocities[i].x;
    }
});
```

Query 缓存的是匹配 Archetype 列表。只有新组件组合首次出现或 Filter 改变时才重建
缓存；实体在已有 Archetype 间迁移不需要重新匹配。Archetype 使用哈希表保存，因此
Query 不承诺跨 Archetype 的实体访问顺序；需要稳定顺序的逻辑应显式收集并排序。

## 6. System 与延迟命令

```cpp
class LifetimeSystem final : public ecs::System {
public:
    LifetimeSystem() : System("Lifetime", 100) {}

protected:
    void OnUpdate(ecs::World& world, ecs::CommandBuffer& commands, float dt) override {
        world.MakeQuery<Lifetime>().Each([&](ecs::Entity entity, Lifetime& lifetime) {
            lifetime.seconds -= dt;
            if (lifetime.seconds <= 0.0F) {
                commands.Destroy(entity); // 查询结束后执行
            }
        });
    }
};

ecs::SystemScheduler scheduler(world);
scheduler.AddSystem<LifetimeSystem>();
scheduler.Update(deltaTime);
```

`OnCreate`、`OnUpdate`、`OnDestroy` 构成 System 生命周期。order 小的 System 先执行，同
order 保持注册顺序；禁用的 System 会跳过。Scheduler 在每个 System 后立即 Playback，
所以后面的 System 能看到前一个 System 的结构变化。

## 7. 复杂度与失效规则

| 操作 | 典型复杂度 | 说明 |
| --- | --- | --- |
| `IsAlive` / `Get<T>` / `Has<T>` | O(1) | 记录表和组件列定位 |
| 创建实体 | O(C) | C 为实体组件种类数 |
| Add / Remove | O(C) | 迁移全部保留组件 |
| Destroy | O(C) | 析构并 swap-remove |
| Query 匹配刷新 | O(A * W) | A 为 Archetype 数，W 为 Signature word 数 |
| Query 遍历 | O(N) | 不逐实体检查 Signature |

以下操作会使当前实体的组件引用失效：

- 给该实体 Add/Remove 组件；
- 销毁该实体；
- swap-remove 将同 Chunk 最后一行移动到该行。

因此不能跨结构变化长期保存组件指针。Query 回调内允许修改已有组件值，但直接调用
Create/Add/Remove/Destroy 会抛出异常；应写入 `CommandBuffer`。

## 8. 当前边界与后续扩展点

这套实现包含单线程游戏逻辑 ECS 的完整闭环，但没有伪装成已经具备以下引擎能力：

- 并行 System 依赖图和读写冲突分析；
- Chunk 并行任务窃取；
- Entity/Component 序列化、Prefab 和场景差量；
- 组件变更事件、关系组件和层级传播；
- Stable Pointer / Pinning（当前迁移会改变组件地址）；
- Chunk 空洞回收和跨 Chunk 压缩策略；
- 编辑器反射属性（注册表目前只有类型名和生命周期函数）。

这些应建立在当前 World/Archetype/Chunk 不变量之上，而不应塞进 RHI 或渲染器。
