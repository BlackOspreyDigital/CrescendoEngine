#version 450

void main() {
    // Generate a full screen triangle natively without any vertex buffers!
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    
    // Map the UVs directly to Vulkan's Clip Space [-1, 1]
    gl_Position = vec4(uv * 2.0 - 1.0, 0.0, 1.0);
}