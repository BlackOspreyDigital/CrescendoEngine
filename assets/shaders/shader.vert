#version 450

// --- INPUTS (Attributes) ---
// These match the Vertex struct in C++
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

// --- OUTPUTS (Must match Fragment Shader inputs exactly) ---
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord; // Matches shader.frag location 1 (Vec2)
layout(location = 2) out vec3 fragNormal;   // Matches shader.frag location 2 (Vec3)
layout(location = 3) out vec3 fragPos;

// --- PUSH CONSTANTS ---
layout(push_constant) uniform constants {
    mat4 renderMatrix; // MVP
    mat4 modelMatrix;  // World Space
    vec4 camPos;
    vec4 pbrParams;
    vec4 sunDir;
    vec4 sunColor;
    vec4 albedoTint;
} PushConstants; 

void main() {
    // 1. Screen Position (MVP)
    gl_Position = PushConstants.renderMatrix * vec4(inPos, 1.0);

    // 2. World Position (Model Matrix)
    fragPos = vec3(PushConstants.modelMatrix * vec4(inPos, 1.0)); 

    // 3. Normal (Model Matrix)
    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.modelMatrix)));
    fragNormal = normalize(normalMatrix * inNormal);

    // 4. Pass-throughs
    fragColor = inColor;
    fragTexCoord = inUV; // Passing the input UV to the output
}