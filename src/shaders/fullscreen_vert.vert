#version 450

layout (location = 0) out vec2 outUV;

void main() 
{
    // Generate a triangle that covers the screen based on the Vertex Index
    // (0, 0), (2, 0), (0, 2)
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(outUV * 2.0f - 1.0f, 0.0f, 1.0f);
}