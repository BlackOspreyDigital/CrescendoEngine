#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;

// Match C++ MeshPushConstants struct exactly
layout(push_constant) uniform PushConstants {
    mat4 renderMatrix; 
    vec4 camPos;
    vec4 pbrParams;    // x=roughness, y=metallic
    vec4 albedoColor;
    int textureID;
} push;

void main() {
    vec4 worldPos = push.renderMatrix * vec4(inPosition, 1.0);
    gl_Position = worldPos; // Assuming renderMatrix includes View/Proj
    
    fragWorldPos = worldPos.xyz;
    fragColor = inColor * push.albedoColor.rgb;
    fragTexCoord = inTexCoord;
    fragNormal = inNormal; // Pass normals for lighting
}