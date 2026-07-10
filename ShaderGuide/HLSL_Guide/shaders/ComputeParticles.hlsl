struct Particle
{
    float4 positionLife;
    float4 velocitySize;
};

cbuffer ParticleSimUniforms : register(b0, space0)
{
    float deltaTime;
    float gravity;
    uint particleCount;
    uint reset;
};

RWStructuredBuffer<Particle> Particles : register(u0, space0);

[numthreads(64, 1, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    const uint index = dispatchThreadId.x;
    if (index >= particleCount) {
        return;
    }

    Particle particle = Particles[index];
    particle.velocitySize.y -= gravity * deltaTime;
    particle.positionLife.xyz += particle.velocitySize.xyz * deltaTime;
    particle.positionLife.w -= deltaTime;

    if (reset != 0 || particle.positionLife.w <= 0.0) {
        const float lane = float(index % 64) / 63.0;
        particle.positionLife = float4(lane * 2.0 - 1.0, 1.0, 0.0, 3.0);
        particle.velocitySize = float4(0.0, 1.0 + lane, 0.0, 4.0);
    }

    Particles[index] = particle;
}
