#version 450

layout (location = 0) out vec3 outViewDir;

// Sky only needs the View/Projection matrix, which maps 
// to the first slot of the C++ PushConstant struct.
layout(push_constant) uniform constants {
    mat4 invViewProj;
} PushConstants;

void main()
{
    // Generate a full screen triangle using vertex id 0,1,2
    // This is a standard trick to draw a background without a mesh
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex &2);
    gl_Position = vec4(uv * 2.0f - 1.0f, 1.0f, 1.0f); // depth = 1.0 for far away

    vec4 target = PushConstants.invViewProj * gl_Position;
    outViewDir = target.xyz / target.w;
    
    // --- DELETED: normalMatrix calculation ---
    // The sky has no normals, so we don't calculate them here.
}