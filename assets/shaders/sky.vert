#version 450

layout(location = 0) out vec3 outUVW;

// --- THE FIX: Intercept the perfectly synced C++ Push Constant! ---
layout(push_constant) uniform SkyboxPush {
    mat4 invViewProj;
} push;

void main() {
    // 1. Mathematically generate a full-screen triangle using the Vertex ID
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 inPos = uv * 2.0 - 1.0; 

    // 2. Unproject the 2D screen quad into a 3D ray using the Push Constant
    vec4 target = push.invViewProj * vec4(inPos, 1.0, 1.0);
    outUVW = target.xyz / target.w;
    
    // 3. Force depth to exactly 1.0 so it renders strictly behind everything
    gl_Position = vec4(inPos, 1.0, 1.0);
}