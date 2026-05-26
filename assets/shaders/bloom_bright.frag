#version 450
layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneColor;

// [FIX] Added Push Constant to read the slider value
layout(push_constant) uniform PushConstants {
    layout(offset = 12) float threshold; // Reads from Offset 12 (skipping exposure/gamma/strength)
} pc;

void main() {
    vec3 color = texture(sceneColor, fragTexCoord).rgb;
    // Extract brightness using the luminance formula
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    
    // [FIX] Use pc.threshold instead of hardcoded 1.0
    if(brightness > pc.threshold) { 
        outColor = vec4(color, 1.0);
    } else {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}