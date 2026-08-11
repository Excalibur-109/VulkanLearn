#pragma once

/** 定义 RHI 库使用的导入/导出标记。*/
#if defined(_WIN32) && defined(RHI_SHARED)
    #if defined(RHI_BUILD_LIBRARY)
        #define RHI_API __declspec(dllexport)
    #else
        #define RHI_API __declspec(dllimport)
    #endif
#else
    #define RHI_API
#endif
