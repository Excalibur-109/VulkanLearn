static const float PI = 3.14159265359;
static const uint OBJECT_LOCAL_TO_WORLD_OFFSET = 0;
static const uint OBJECT_NORMAL_MATRIX_OFFSET = 64;
static const uint OBJECT_MATERIAL_INDEX_OFFSET = 128;
static const uint MATERIAL_STRIDE_BYTES = 32;

cbuffer SceneUniforms : register(b0)
{
    column_major float4x4 sceneView;
    column_major float4x4 sceneProjection;
    column_major float4x4 sceneViewProjection;
    column_major float4x4 sceneLightViewProjection;
    float4 sceneCameraPosition;
    float4 sceneAmbientColorExposure;
};

cbuffer LightUniforms : register(b1)
{
    float4 lightDirectionIntensity;
    float4 lightColorShadowBias;
};

ByteAddressBuffer ObjectBuffer : register(t0);
ByteAddressBuffer MaterialBuffer : register(t1);
Texture2D<float> ShadowMap : register(t2);
SamplerComparisonState ShadowSampler : register(s2);

struct PbrVertexInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct PbrVertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 normal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    float4 lightClipPosition : TEXCOORD3;
    nointerpolation uint materialIndex : TEXCOORD4;
};

float SaturateFloat(float value)
{
    return clamp(value, 0.0, 1.0);
}

float4 LoadFloat4(ByteAddressBuffer buffer, uint byteOffset)
{
    return asfloat(buffer.Load4(byteOffset));
}

float4 TransformPointFromColumns(ByteAddressBuffer buffer, uint matrixByteOffset, float3 position)
{
    const float4 column0 = LoadFloat4(buffer, matrixByteOffset + 0);
    const float4 column1 = LoadFloat4(buffer, matrixByteOffset + 16);
    const float4 column2 = LoadFloat4(buffer, matrixByteOffset + 32);
    const float4 column3 = LoadFloat4(buffer, matrixByteOffset + 48);
    return column0 * position.x + column1 * position.y + column2 * position.z + column3;
}

float3 TransformVectorFromColumns(ByteAddressBuffer buffer, uint matrixByteOffset, float3 vectorValue)
{
    const float4 column0 = LoadFloat4(buffer, matrixByteOffset + 0);
    const float4 column1 = LoadFloat4(buffer, matrixByteOffset + 16);
    const float4 column2 = LoadFloat4(buffer, matrixByteOffset + 32);
    return column0.xyz * vectorValue.x + column1.xyz * vectorValue.y + column2.xyz * vectorValue.z;
}

PbrVertexOutput PbrVertexMain(PbrVertexInput input)
{
    const uint objectBase = 0;
    const float4 worldPosition = TransformPointFromColumns(ObjectBuffer, objectBase + OBJECT_LOCAL_TO_WORLD_OFFSET, input.position);

    PbrVertexOutput output;
    output.position = mul(sceneViewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = normalize(TransformVectorFromColumns(ObjectBuffer, objectBase + OBJECT_NORMAL_MATRIX_OFFSET, input.normal));
    output.uv = input.uv;
    output.lightClipPosition = mul(sceneLightViewProjection, worldPosition);
    output.materialIndex = ObjectBuffer.Load(objectBase + OBJECT_MATERIAL_INDEX_OFFSET);
    return output;
}

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    const float alpha = roughness * roughness;
    const float alpha2 = alpha * alpha;
    const float ndoth = SaturateFloat(dot(normal, halfVector));
    const float denom = ndoth * ndoth * (alpha2 - 1.0) + 1.0;
    return alpha2 / max(PI * denom * denom, 0.0001);
}

float GeometrySchlickGGX(float ndotv, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return ndotv / max(ndotv * (1.0 - k) + k, 0.0001);
}

float GeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    return GeometrySchlickGGX(SaturateFloat(dot(normal, viewDirection)), roughness) *
           GeometrySchlickGGX(SaturateFloat(dot(normal, lightDirection)), roughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0 - f0) * pow(1.0 - SaturateFloat(cosTheta), 5.0);
}

float SampleShadow(float4 lightClipPosition)
{
    const float3 projected = lightClipPosition.xyz / lightClipPosition.w;
    const float2 shadowUv = float2(projected.x * 0.5 + 0.5, -projected.y * 0.5 + 0.5);

    if (shadowUv.x < 0.0 || shadowUv.x > 1.0 ||
        shadowUv.y < 0.0 || shadowUv.y > 1.0 ||
        projected.z < 0.0 || projected.z > 1.0) {
        return 1.0;
    }

    const float bias = lightColorShadowBias.w;
    return ShadowMap.SampleCmpLevelZero(ShadowSampler, shadowUv, projected.z - bias);
}

float4 PbrPixelMain(PbrVertexOutput input) : SV_Target0
{
    const uint materialBase = input.materialIndex * MATERIAL_STRIDE_BYTES;
    const float4 materialBaseColor = LoadFloat4(MaterialBuffer, materialBase + 0);
    const float4 materialMetallicRoughness = LoadFloat4(MaterialBuffer, materialBase + 16);

    const float3 baseColor = pow(materialBaseColor.rgb, float3(2.2, 2.2, 2.2));
    const float metallic = SaturateFloat(materialMetallicRoughness.x);
    const float roughness = clamp(materialMetallicRoughness.y, 0.04, 1.0);

    const float3 normal = normalize(input.normal);
    const float3 viewDirection = normalize(sceneCameraPosition.xyz - input.worldPosition);
    const float3 lightDirection = normalize(-lightDirectionIntensity.xyz);
    const float3 halfVector = normalize(viewDirection + lightDirection);

    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    const float ndotl = SaturateFloat(dot(normal, lightDirection));
    const float ndotv = max(SaturateFloat(dot(normal, viewDirection)), 0.0001);

    const float normalDistribution = DistributionGGX(normal, halfVector, roughness);
    const float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    const float3 fresnel = FresnelSchlick(max(dot(halfVector, viewDirection), 0.0), f0);

    const float3 numerator = normalDistribution * geometry * fresnel;
    const float denominator = max(4.0 * ndotv * ndotl, 0.0001);
    const float3 specular = numerator / denominator;

    const float3 diffuseRatio = (1.0 - fresnel) * (1.0 - metallic);
    const float3 radiance = lightColorShadowBias.rgb * lightDirectionIntensity.w;
    const float shadow = SampleShadow(input.lightClipPosition);

    const float3 ambient = sceneAmbientColorExposure.rgb * baseColor;
    const float3 lit = (diffuseRatio * baseColor / PI + specular) * radiance * ndotl * shadow;
    float3 color = (ambient + lit) * sceneAmbientColorExposure.w;

    color = color / (color + float3(1.0, 1.0, 1.0));
    color = pow(color, float3(1.0 / 2.2, 1.0 / 2.2, 1.0 / 2.2));
    return float4(color, materialBaseColor.a);
}
