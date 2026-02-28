#version 450

layout (location = 0) out vec3 outUVW;

layout(push_constant) uniform constants {
    mat4 invViewProj;
} PushConstants;

void main() {
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(uv * 2.0f - 1.0f, 1.0f, 1.0f); 

    vec4 target = PushConstants.invViewProj * gl_Position;
    outUVW = target.xyz / target.w;
}