#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

// Binding 0: Original Scene (viewportImage)
// Binding 1: Blurred Bloom (bloomBlurImage)
layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D bloomBlur;

void main() {
    const float gamma = 2.2;
    const float exposure = 1.0;
    const float bloomIntensity = 1.5; // Adjustable

    vec3 hdrColor = texture(sceneColor, fragTexCoord).rgb;      
    vec3 bloomColor = texture(bloomBlur, fragTexCoord).rgb;

    // 1. Additive Blend
    hdrColor += bloomColor * bloomIntensity; 

    // 2. Tone Mapping (Reinhardt)
    vec3 result = vec3(1.0) - exp(-hdrColor * exposure);
    
    // 3. Gamma Correction
    result = pow(result, vec3(1.0 / gamma));

    outColor = vec4(result, 1.0);
}