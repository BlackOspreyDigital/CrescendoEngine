#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

// [CRITICAL] Binding 0 to match C++ (was Binding 1)
layout(binding = 0) uniform sampler2D texSampler[100];

layout(push_constant) uniform PushConstants {
    mat4 renderMatrix; 
    vec4 camPos;
    vec4 pbrParams;
    vec4 albedoColor;
    int textureID;
} push;

void main() {
    // 1. Safe Texture Lookup
    // If ID is bad, use 0 (Default White)
    int id = push.textureID;
    if (id < 0 || id >= 100) id = 0; 
    
    vec4 texColor = texture(texSampler[id], fragTexCoord);

    // 2. Multiply by Vertex Color (from Material/GLTF)
    outColor = texColor * vec4(fragColor, 1.0);
}