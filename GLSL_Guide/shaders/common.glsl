#ifndef GLSL_GUIDE_COMMON_GLSL
#define GLSL_GUIDE_COMMON_GLSL

const float PI = 3.14159265359;
const float EPSILON = 0.0001;

float saturate(float value) {
    return clamp(value, 0.0, 1.0);
}

vec2 saturate(vec2 value) {
    return clamp(value, vec2(0.0), vec2(1.0));
}

vec3 saturate(vec3 value) {
    return clamp(value, vec3(0.0), vec3(1.0));
}

vec3 srgbToLinear(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(2.2));
}

vec3 linearToSrgb(vec3 color) {
    return pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
}

vec3 reinhardToneMap(vec3 color) {
    return color / (color + vec3(1.0));
}

#endif
