#version 450

void main() {
    // Depth-only pass 不需要写颜色。固定管线会把 gl_FragCoord.z 写入 depth attachment。
}
