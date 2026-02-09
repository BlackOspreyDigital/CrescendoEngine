#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;

layout(push_constant) uniform constants {
    mat4 renderMatrix;
    vec4 camPos;
    vec4 pbrParams;
    vec4 sunDir;
    vec4 sunColor;
} PushConstants; 

void main() {
    // Vertex position in clip space
    gl_Position = PushConstants.renderMatrix * vec4(inPos, 1.0);
    fragPos = inPos; // <--- This must be the raw position for correct world-space lighting!

    // Calculate Normal Matrix manually
    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.renderMatrix)));

    fragColor = inColor;
    fragNormal = normalize(normalMatrix * inNormal);
    fragTexCoord = inUV;
}