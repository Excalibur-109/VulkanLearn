# Archetype ECS

公共入口是 `ECS/ECS.hpp`，命名空间是 `ecs`。该模块不依赖 RHI、Render、Vulkan、
D3D 或 glm。

## 1. 设计原则

- 用对象划分责任：`World` 管一致性，`Archetype` 管组件组合，`Chunk` 管原始内存。
- 少继承：Component 是任意普通结构体；业务 System 也是普通类，不继承 ECS 基类。
- 用组合和函数指针做类型擦除，不建立 `virtual` 对象层次。
- 热数据使用原始指针和 C 堆；控制对象仍使用 C++ RAII，保证异常路径可以释放资源。

## 2. 核心对象

| 对象 | 作用 |
| --- | --- |
| `Entity` | `{index, generation}` 句柄，不保存组件 |
| `ComponentRegistry` | 进程静态 ComponentTypeId 的直接索引表和移动/析构函数表 |
| `Signature` | 动态 bitset，描述完整组件组合 |
| `Filter` | `all + any + none` Archetype 匹配条件 |
| `Archetype` | 拥有相同 Signature 的实体集合 |
| `Chunk` | 一次 C 堆分配形成的 SoA 组件列 |
| `World` | Entity 记录、Archetype 和结构变化的唯一所有者 |
| `Query` | 缓存匹配 Archetype，逐行或逐 Chunk 访问 |
| `CommandBuffer` | 查询结束后执行的结构变化命令 |
| `LinearArena` | Command payload 使用的可复用页式 bump allocator |
| `JobSystem` | Query 按 Chunk 并行使用的固定线程池 |
| `System` | 组合式类型擦除包装，不是业务类基类 |
| `SystemScheduler` | 按 order 调度并回放命令 |

## 3. Chunk 原始内存布局

例如 `{Position, Rotation, Velocity}` Archetype：

```text
storage_ (std::byte*, 一次 malloc/aligned allocation)

+-----------------------------+ entityOffset
| Entity[capacity]            |
+-----------------------------+ positionOffset, alignof(Position)
| Position[capacity]          |
+-----------------------------+ rotationOffset, alignof(Rotation)
| Rotation[capacity]          |
+-----------------------------+ velocityOffset, alignof(Velocity)
| Velocity[capacity]          |
+-----------------------------+
```

普通组件对齐不超过 `max_align_t`，走 `malloc/free`。`alignas(32/64)` 组件不能假定
`malloc` 满足要求，因此 Windows 使用 `_aligned_malloc/_aligned_free`，其他平台使用
`aligned_alloc/free`。这仍然是 C 运行库堆，只是满足了 C++ over-aligned 对象规则。

Chunk 内没有 `Component*` 指针数组。列地址直接计算：

```cpp
componentAddress = storage + column.offset + componentSize * row;
```

## 4. 实体位置和迁移

World 的稀疏记录表：

```text
records[entity.index] = {
    generation,
    alive,
    Archetype*,
    Chunk*,
    row
}
```

给实体 Add/Remove 组件会改变 Signature，因此需要迁移：

1. 查找或创建目标 Archetype。
2. 在目标 Chunk 预留一行。
3. Add 时先构造可能抛异常的新组件。
4. 将保留组件 noexcept move 到目标列。
5. 从 Archetype 最后一个 Chunk 搬一行填补空洞。
6. 更新当前实体和被交换实体的反向位置。

每个 Archetype 始终保持“除最后一个 Chunk 外全部填满”。分配只访问最后一个 Chunk，
删除会回收空的末尾 Chunk。每条 Add/Remove 边还会缓存目标 Archetype 和公共组件列
offset，首次迁移后不再构造 Signature、查哈希表或查找列。

因此组件必须 `nothrow move constructible`。组件地址不能跨 Add/Remove/Destroy 保存。

## 5. Component 和 Query

Component 不需要基类：

```cpp
struct Position { float x; float y; float z; };
struct Velocity { float x; float y; float z; };
struct Disabled {};

ecs::World world;
ecs::Entity entity = world.CreateEntity(Position{}, Velocity{1, 0, 0});
world.Add<Disabled>(entity);
world.Remove<Disabled>(entity);
```

Query 模板参数自动进入 all 条件，`const T` 表示只读：

```cpp
auto query = world.MakeQuery<Position, const Velocity>();
query.WithAny<Player, Enemy>().Without<Disabled>();

query.Each([](ecs::Entity, Position& p, const Velocity& v) {
    p.x += v.x;
});
```

Query 在匹配 Archetype 时会预绑定组件列 offset。逐实体内层循环直接执行
`typedColumn[row]`，不会调用 `ComponentAt` 或二分查列。高频 System 应长期保存 Query，
不要每帧创建后立即丢弃。

整列访问：

```cpp
query.EachChunk([](
    std::span<const ecs::Entity> entities,
    std::span<Position> positions,
    std::span<const Velocity> velocities) {
    for (std::size_t i = 0; i < entities.size(); ++i) {
        positions[i].x += velocities[i].x;
    }
});
```

并行整列访问：

```cpp
ecs::JobSystem jobs;
query.ParallelEachChunk(jobs, [](
    std::span<const ecs::Entity>,
    std::span<Position> positions,
    std::span<const Velocity> velocities) {
    for (std::size_t i = 0; i < positions.size(); ++i) {
        positions[i].x += velocities[i].x;
    }
});
```

JobSystem 的线程长期存在；每次 ParallelFor 只发布一个函数指针任务，worker 使用原子
索引领取 Chunk，不为每个 job 分配对象。并行回调中的外部可写捕获由调用者负责同步。
同一个 JobSystem 内嵌套 ParallelFor 时自动在当前 worker 串行执行，避免线程池自等待。

## 6. 无继承 System

业务系统是普通类，只需公开 `OnUpdate`。`OnCreate` 和 noexcept `OnDestroy` 可选：

```cpp
class MovementSystem {
public:
    void OnCreate(ecs::World& world) {
        query_.emplace(world);
    }

    void OnUpdate(ecs::World& world, ecs::CommandBuffer& commands, float dt) {
        query_->Each(
            [&](ecs::Entity entity, Position& p, const Velocity& v) {
                p.x += v.x * dt;
                if (p.x > 100.0F) {
                    commands.Add<Disabled>(entity);
                }
            });
    }

    void OnDestroy(ecs::World&) noexcept {}

private:
    std::optional<ecs::Query<Position, const Velocity>> query_;
};

ecs::SystemScheduler scheduler(world);
scheduler.AddSystem<MovementSystem>(ecs::SystemDesc{"Movement", -100, true});
scheduler.SetSystemEnabled<MovementSystem>(false);
scheduler.Update(deltaTime);
```

内部 `System` 保存 `void* object` 和 Update/Destroy 函数指针，没有业务基类、virtual、
dynamic_cast。`CommandBuffer` 也采用相同方式保存 lambda payload。

CommandBuffer 的 payload 不再逐条 malloc。录制和回放各持有一个 64 KiB 页式
`LinearArena`，每条命令只推进 offset；两套 Arena 每轮交换并复用。峰值命令数量已知时
可调用 `commands.Reserve(count)`，同时避免命令描述数组扩容。

## 7. 失效与线程边界

- Query 回调中可以修改已有组件，不能直接 Create/Add/Remove/Destroy。
- 结构变化写入 `CommandBuffer`，遍历结束后 Playback。
- Query 不承诺跨 Archetype 的稳定顺序，需要确定性顺序时显式排序。
- World 的结构变化仍是单写者；只读/组件值更新可通过 `ParallelEachChunk` 并行。
- 当前未实现序列化、Prefab、关系图和编辑器属性反射，它们应建立在本层之上。

## 8. 跨 DLL 组件 ID

`ComponentTypeId` 是进程本次运行中的稠密数组下标，不是可持久化身份。所有 EXE 和 DLL
都会调用 `ECS.dll` 中唯一的 `AcquireComponentTypeId`；中心注册表用互斥锁保护
`ComponentGuid -> ComponentTypeId` 映射，因此不同模块和不同线程首次请求同一 GUID 时，
一定得到同一个 ID。

`CreateEntity<Components...>` 用于快速定位 Archetype 的组件包缓存 ID 也在 `ECS.dll`
统一分配。否则每个 DLL 内的头文件静态计数器都会从 0 开始，不同组件组合可能错误复用
同一个 `World` 缓存槽。组件包先按中心 `ComponentTypeId` 排序，所以参数顺序不同但集合
相同的创建调用会共享缓存项。

跨 DLL、存档、网络协议或热重载组件必须在组件内部声明固定 GUID：

```cpp
struct Transform {
    ECS_DECLARE_COMPONENT_GUID(
        0x1234567890ABCDEFULL,
        0xFEDCBA0987654321ULL);

    float position[3]{};
};
```

未声明时会从编译器类型签名生成后备 GUID，只适合同一工具链、同一份源码内部使用。
重命名类型、改变命名空间或更换编译器都可能改变后备 GUID。存档和网络中保存
`ComponentGuid`，加载后再换取本次运行的 `ComponentTypeId`；不能把运行时 ID 写入文件。

组件元数据含有构造/移动/析构函数指针。如果这些函数由插件 DLL 提供，则该插件仍有
组件实例或注册元数据被使用时不得卸载。热卸载必须先销毁或迁移相关组件，再解除元数据；
中心注册表保留已经分配的 ID，不能把旧 ID 复用给另一个 GUID。

## 9. Release 基准

可执行文件 `ECSBenchmark` 默认创建 25 万实体并执行 1 亿级组件运算。当前机器的多次
Release 运行中位数约为：

| 操作 | 吞吐量 |
| --- | ---: |
| 创建 Position + Velocity（已 Reserve） | 15.5 M entities/s |
| Query::Each | 185 M entity-updates/s |
| Query::EachChunk | 194 M entity-updates/s |
| ParallelEachChunk（7 workers） | 209 M entity-updates/s |
| Add 空标签组件 | 19.8 M entities/s |
| Remove 空标签组件 | 19.6 M entities/s |
| Command 入队（已 Reserve） | 53 M commands/s |
| Command 回放 | 78 M commands/s |

结果受 CPU 调频、内存和组件大小影响，必须在目标平台重新运行，不能把该数字当作固定保证。
