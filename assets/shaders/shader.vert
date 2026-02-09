#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragWorldPos;

// [FIX] Match C++ Struct Layout Exactly (5 items)
layout(push_constant) uniform PushConstants {
    mat4 renderMatrix; 
    vec4 camPos;
    vec4 pbrParams;    // x = TextureID, y = Roughness, z = Metallic
    vec4 sunDir;       // [Changed] Was albedoColor
    vec4 sunColor;     // [Changed] Was int textureID
} push;

void main() {
    vec4 worldPos = push.renderMatrix * vec4(inPosition, 1.0);
    gl_Position = worldPos;
    
    fragWorldPos = worldPos.xyz;
    
    // [FIX] Use vertex color directly. 
    // If you need global tinting later, use sunColor or add a specific tint field.
    fragColor = inColor; 
    
    fragTexCoord = inTexCoord;
    fragNormal = inNormal;
}