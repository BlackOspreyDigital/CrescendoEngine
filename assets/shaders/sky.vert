#version 450

layout(location = 0) out vec2 outUV;

void main() {
    // 1. Generate UV coordinates (0.0 to 2.0) purely from the vertex ID
    outUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    
    // 2. Map UVs to Vulkan's Clip Space (-1.0 to +3.0)
    // By setting Z and W to 1.0, we perform the "Infinity Hack" 
    // This forces the skybox to render at the absolute maximum depth behind everything else!
    gl_Position = vec4(outUV * 2.0 - 1.0, 1.0, 1.0);
}