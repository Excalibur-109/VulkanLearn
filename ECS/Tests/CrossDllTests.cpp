#include "CrossDllComponents.hpp"

#include "ECS/World.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

} // namespace

int main() {
    try {
        // 宿主先请求 Position；插件内部先请求 Velocity，覆盖不同注册顺序的场景。
        const ecs::ComponentTypeId hostPosition =
            ecs::GetComponentTypeId<CrossDllPosition>();
        const CrossDllComponentIds pluginIds = GetPluginComponentIds();
        const ecs::ComponentTypeId hostVelocity =
            ecs::GetComponentTypeId<CrossDllVelocity>();

        Check(hostPosition == pluginIds.position, "Position ID differs across EXE and DLL");
        Check(hostVelocity == pluginIds.velocity, "Velocity ID differs across EXE and DLL");
        Check(hostPosition != hostVelocity, "Different component GUIDs received the same ID");

        bool duplicateGuidRejected = false;
        try {
            (void)ecs::AcquireComponentTypeId(
                ecs::GetComponentGuid<CrossDllPosition>(),
                "A different component using the Position GUID");
        } catch (const std::logic_error&) {
            duplicateGuidRejected = true;
        }
        Check(duplicateGuidRejected, "A duplicate component GUID was accepted");

        // World 使用相同的中心 ID 建立稠密列索引，不能再分配一套 World 私有 ID。
        ecs::World world;
        Check(
            world.RegisterComponent<CrossDllPosition>() == hostPosition,
            "World Position registration did not use the central ID");
        Check(
            world.RegisterComponent<CrossDllVelocity>() == hostVelocity,
            "World Velocity registration did not use the central ID");

        // 先让 EXE 占用一个创建包缓存槽，再让 DLL 在同一个 World 创建另一种组合。
        // 若两个模块各自从 0 分配 pack ID，第二次创建会错误复用 Position Archetype。
        const ecs::Entity hostEntity = world.CreateEntity(CrossDllPosition{5.0F});
        const ecs::Entity pluginEntity = CreatePluginVelocityEntity(world);
        Check(world.Has<CrossDllPosition>(hostEntity), "Host creation pack used a wrong Archetype");
        Check(world.Has<CrossDllVelocity>(pluginEntity), "DLL creation pack used a wrong Archetype");
        Check(
            world.Get<CrossDllVelocity>(pluginEntity).x == 7.0F,
            "DLL-created component data was not preserved");

        const ecs::Entity entity =
            world.CreateEntity(CrossDllPosition{2.0F}, CrossDllVelocity{3.0F});
        Check(world.Get<CrossDllPosition>(entity).x == 2.0F, "World Position access failed");
        Check(world.Get<CrossDllVelocity>(entity).x == 3.0F, "World Velocity access failed");
        Check(ecs::RegisteredComponentTypeCount() >= 2U, "Central registry lost component IDs");

        std::cout << "Cross-DLL component ID synchronization passed.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Cross-DLL ECS test failed: " << exception.what() << '\n';
        return 1;
    }
}
