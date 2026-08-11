#pragma once

#include "RHI/RHIDescriptors.hpp"

#include <cstddef>
#include <vector>

namespace RHI
{

/** 标识着色器模块使用的可移植源代码或中间表示。*/
enum class RHIShaderCodeFormat : std::uint8_t
{
    /** 后端无关的二进制中间表示。*/
    IntermediateBinary,
    /** 后端无关的着色语言源文本。*/
    SourceText,
    /** 由一个已注册实现接受的后端私有二进制载荷。*/
    BackendBinary,
};

/** 定义顶点属性的标量和向量编码。*/
enum class RHIVertexFormat : std::uint8_t
{
    /** 两个 32 位浮点分量。*/
    Float2,
    /** 三个 32 位浮点分量。*/
    Float3,
    /** 四个 32 位浮点分量。*/
    Float4,
    /** 两个 16 位浮点分量。*/
    Half2,
    /** 四个 16 位浮点分量。*/
    Half4,
    /** 四个 8 位无符号归一化分量。*/
    UNorm8x4,
    /** 四个 8 位有符号归一化分量。*/
    SNorm8x4,
    /** 四个 8 位无符号整数分量。*/
    UInt8x4,
    /** 两个 16 位无符号整数分量。*/
    UInt16x2,
    /** 四个 16 位无符号整数分量。*/
    UInt16x4,
    /** 单个 32 位无符号整数分量。*/
    UInt32,
    /** 两个 32 位无符号整数分量。*/
    UInt32x2,
    /** 三个 32 位无符号整数分量。*/
    UInt32x3,
    /** 四个 32 位无符号整数分量。*/
    UInt32x4,
};

/** 定义顶点缓冲区数据推进频率。*/
enum class RHIVertexStepMode : std::uint8_t
{
    /** 每个顶点推进一次。*/
    PerVertex,
    /** 每个实例推进一次。*/
    PerInstance,
};

/** 描述已编译或源着色器代码及其入口点。*/
struct RHIShaderModuleDesc final
{
    /** 仅用于诊断和捕获的调试名称。*/
    std::string DebugName;
    /** Code 使用的表示形式。*/
    RHIShaderCodeFormat CodeFormat = RHIShaderCodeFormat::IntermediateBinary;
    /** 创建返回前由调用方持有的着色器代码。*/
    std::vector<std::byte> Code;
    /** 模块为创建管线导出的函数。*/
    std::string EntryPoint = "main";
};

/** 描述从顶点缓冲区读取的一个属性。*/
struct RHIVertexAttribute final
{
    /** 着色器输入位置。*/
    std::uint32_t Location = 0;
    /** 属性数据编码。*/
    RHIVertexFormat Format = RHIVertexFormat::Float3;
    /** 相对一个顶点元素起始处的字节偏移。*/
    std::uint32_t Offset = 0;
};

/** 描述图形管线绑定的一个顶点缓冲区。*/
struct RHIVertexBufferLayout final
{
    /** 相邻元素之间的字节步长。*/
    std::uint32_t Stride = 0;
    /** 缓冲区推进频率。*/
    RHIVertexStepMode StepMode = RHIVertexStepMode::PerVertex;
    /** 从该缓冲区读取的属性。*/
    std::vector<RHIVertexAttribute> Attributes;
};

} // namespace RHI
