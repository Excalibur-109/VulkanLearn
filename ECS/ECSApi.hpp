#pragma once

#if defined(_WIN32) && defined(ECS_SHARED)
#if defined(ECS_EXPORTS)
#define ECS_API __declspec(dllexport)
#else
#define ECS_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && defined(ECS_SHARED)
#define ECS_API __attribute__((visibility("default")))
#else
#define ECS_API
#endif
