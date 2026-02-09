#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragWorldPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler[100];

// [FIX] Match C++ Struct Layout Exactly
layout(push_constant) uniform PushConstants {
    mat4 renderMatrix; 
    vec4 camPos;
    vec4 pbrParams;    // x = TextureID, y = Roughness, z = Metallic
    vec4 sunDir;
    vec4 sunColor;
} push;

void main() {
    // 1. [FIX] Get ID from pbrParams.x (Float -> Int)
    int id = int(push.pbrParams.x);
    
    // Safety check
    if (id < 0 || id >= 100) id = 0; 
    
    vec4 texColor = texture(texSampler[id], fragTexCoord);

    // 2. Simple Lighting (Optional: Use sunDir/sunColor if you want)
    // For now, just show the texture so we know it works.
    outColor = texColor * vec4(fragColor, 1.0);
}