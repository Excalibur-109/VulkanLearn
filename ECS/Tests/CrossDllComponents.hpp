#pragma once

#include "ECS/Component.hpp"

namespace ecs {
class World;
struct Entity;
} // namespace ecs

/**
 * 该头文件同时被宿主 EXE 和测试插件 DLL 编译。组件的 C++ 类型名相同还不够：
 * 真正跨模块、跨版本的身份由显式 ComponentGuid 决定。
 */
struct CrossDllPosition {
    ECS_DECLARE_COMPONENT_GUID(
        0xB1427D44A04A4E95ULL,
        0xA72371DF582C1171ULL);

    float x = 0.0F;
};

struct CrossDllVelocity {
    ECS_DECLARE_COMPONENT_GUID(
        0xD8D5864F1FF74609ULL,
        0x81DCE7B09C019A34ULL);

    float x = 0.0F;
};

struct CrossDllComponentIds {
    ecs::ComponentTypeId position = ecs::INVALID_COMPONENT_TYPE;
    ecs::ComponentTypeId velocity = ecs::INVALID_COMPONENT_TYPE;
};

#if defined(_WIN32)
#if defined(ECS_CROSS_DLL_PLUGIN_EXPORTS)
#define ECS_CROSS_DLL_API __declspec(dllexport)
#else
#define ECS_CROSS_DLL_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define ECS_CROSS_DLL_API __attribute__((visibility("default")))
#else
#define ECS_CROSS_DLL_API
#endif

extern "C" ECS_CROSS_DLL_API CrossDllComponentIds GetPluginComponentIds();
extern "C" ECS_CROSS_DLL_API ecs::Entity CreatePluginVelocityEntity(ecs::World& world);
