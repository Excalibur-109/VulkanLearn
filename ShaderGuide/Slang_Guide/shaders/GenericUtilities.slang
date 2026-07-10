#include "Common.slang"

// Slang 的泛型不是 C++ template 语法，而是 __generic。
// 这里约束 T 为 Slang 内建浮点类型，所以 float、float2、float3、float4 都可使用。
__generic<typename T>
T BlendValue(T a, T b, float weight)
    where T : __BuiltinFloatingPointType
{
    return lerp(a, b, SaturateFloat(weight));
}

struct DebugVertexInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
};

struct DebugVertexOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

cbuffer DebugUniforms : register(b0, space0)
{
    column_major float4x4 debugViewProjection;
    float debugBlendWeight;
    float3 debugPadding;
};

[shader("vertex")]
DebugVertexOutput DebugVS(DebugVertexInput input)
{
    DebugVertexOutput output;
    output.position = mul(debugViewProjection, float4(input.position, 1.0));
    output.color = BlendValue(input.color, float4(1.0, 1.0, 1.0, input.color.a), debugBlendWeight);
    return output;
}

[shader("fragment")]
float4 DebugPS(DebugVertexOutput input) : SV_Target0
{
    return input.color;
}
