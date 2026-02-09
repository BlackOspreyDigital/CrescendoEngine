#version 450
layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneColor;

void main() {
    vec3 color = texture(sceneColor, fragTexCoord).rgb;
    // Extract brightness using the luminance formula
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    
    if(brightness > 1.0) { // Threshold for "glow"
        outColor = vec4(color, 1.0);
    } else {
        outColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}