#include "CrossDllComponents.hpp"

#include "ECS/World.hpp"

CrossDllComponentIds GetPluginComponentIds() {
    // 故意先请求 Velocity，验证 DLL 与宿主的首次请求顺序不必相同。
    const ecs::ComponentTypeId velocity = ecs::GetComponentTypeId<CrossDllVelocity>();
    const ecs::ComponentTypeId position = ecs::GetComponentTypeId<CrossDllPosition>();
    return CrossDllComponentIds{position, velocity};
}

ecs::Entity CreatePluginVelocityEntity(ecs::World& world) {
    // CreateEntity 在插件中实例化，组件包缓存 ID 仍必须与宿主使用同一个分配中心。
    return world.CreateEntity(CrossDllVelocity{7.0F});
}
