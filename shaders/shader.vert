#version 450

struct Particle {
    vec2 position;
    vec2 velocity;
    vec3 color;
};

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;
    mat4 view;
    mat4 proj;
} ubo;

layout(std140, binding = 1) readonly buffer ParticleSSBOIn {
    Particle particlesIn[];
};

layout(std140, binding = 2) buffer ParticleSSBOOut {
    Particle particlesOut[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 texCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * vec4(inPosition, 1.0);
    // particlesOut[index] = particlesIn[index].position + particlesIn[index].velocity * ubo.deltaTime;
    fragColor = inColor;
    fragTexCoord = texCoord;
}