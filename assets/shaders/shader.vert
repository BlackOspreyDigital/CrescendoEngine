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
    mat4 modelMatrix; // [NEW]
    vec4 camPos;
    vec4 pbrParams;
    vec4 sunDir;
    vec4 sunColor;
} PushConstants; 

void main() {
    // 1. Position on Screen (Clip Space) - Uses MVP
    gl_Position = PushConstants.renderMatrix * vec4(inPos, 1.0);

    // 2. Position in World (World Space) - Uses Model Matrix
    // [FIX] This enables correct lighting calculations!
    fragPos = vec3(PushConstants.modelMatrix * vec4(inPos, 1.0));

    // 3. Normal in World Space
    // [FIX] Inverting the MODEL matrix gives correct normals.
    // Inverting the Render (MVP) matrix gave garbage results.
    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.modelMatrix)));
    fragNormal = normalize(normalMatrix * inNormal);

    fragColor = inColor;
    fragTexCoord = inUV;
}