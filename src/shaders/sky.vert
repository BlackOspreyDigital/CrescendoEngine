#version 450

layout(location = 0) out vec2 outUV;

void main() {
    // 1. Generate UV coordinates (0.0 to 2.0) purely from the vertex ID
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);

    // 2. In Reversed-Z, the absolute maximum depth is 0.0!
    gl_Position = vec4(outUV * 2.0 - 1.0, 0.0, 1.0);
}