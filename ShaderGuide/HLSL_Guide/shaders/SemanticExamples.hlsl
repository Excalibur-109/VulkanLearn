cbuffer CameraUniforms : register(b0, space0)
{
    column_major float4x4 viewProjection;
};

// HLSL 顶点输入使用语义名和 C++ input layout 对齐。
struct SemanticVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

// HLSL 阶段输出也使用语义。
// SV_Position 是系统语义，告诉 D3D 这是裁剪空间位置。
// TEXCOORD0/TEXCOORD1 是普通插值通道。
struct SemanticVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

SemanticVertexOutput SemanticVS(SemanticVertexInput input, uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    SemanticVertexOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0));
    output.uv = input.uv + float2(vertexId, instanceId) * 0.0;
    output.normal = input.normal;
    return output;
}

// SV_Target0 表示写入第 0 个 render target。
float4 SemanticPS(SemanticVertexOutput input) : SV_Target0
{
    const float3 normalColor = normalize(input.normal) * 0.5 + 0.5;
    return float4(normalColor * float3(input.uv, 1.0), 1.0);
}

RWStructuredBuffer<uint> Values : register(u0, space0);

[numthreads(64, 1, 1)]
void SemanticCS(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint3 groupThreadId : SV_GroupThreadID,
    uint3 groupId : SV_GroupID)
{
    Values[dispatchThreadId.x] = groupId.x * 1000u + groupThreadId.x;
}
