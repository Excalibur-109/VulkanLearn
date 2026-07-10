cbuffer CameraUniforms : register(b0, space0)
{
    column_major float4x4 viewProjection;
};

struct SemanticVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct SemanticVertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float3 normal : TEXCOORD1;
};

[shader("vertex")]
SemanticVertexOutput SemanticVS(SemanticVertexInput input, uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    SemanticVertexOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0));
    output.uv = input.uv + float2(vertexId, instanceId) * 0.0;
    output.normal = input.normal;
    return output;
}

[shader("fragment")]
float4 SemanticPS(SemanticVertexOutput input) : SV_Target0
{
    const float3 normalColor = normalize(input.normal) * 0.5 + 0.5;
    return float4(normalColor * float3(input.uv, 1.0), 1.0);
}

RWStructuredBuffer<uint> Values : register(u0, space0);

[shader("compute")]
[numthreads(64, 1, 1)]
void SemanticCS(
    uint3 dispatchThreadId : SV_DispatchThreadID,
    uint3 groupThreadId : SV_GroupThreadID,
    uint3 groupId : SV_GroupID)
{
    Values[dispatchThreadId.x] = groupId.x * 1000u + groupThreadId.x;
}
