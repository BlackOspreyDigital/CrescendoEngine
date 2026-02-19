#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D bloomBlur;

// [UPDATED] Push Constants
layout(push_constant) uniform PushConstants {
    float exposure;       // Offset 0
    float gamma;          // Offset 4
    float bloomStrength;  // Offset 8
    float bloomThreshold; // Offset 12 (Used in bright pass, padding here)
    float blurRadius;     // Offset 16 [NEW]
} settings;

void main() {
    vec3 hdrColor = texture(sceneColor, fragTexCoord).rgb;
    
    // Calculate the size of 1 texel
    vec2 tex_offset = 1.0 / textureSize(bloomBlur, 0); 
    
    // [NEW] Apply the Blur Radius Slider
    // 1.0 = Crisp, 2.0-3.0 = Softer/Wider
    tex_offset *= settings.blurRadius; 

    vec3 bloomColor = vec3(0.0);
    
    // Center (Highest weight)
    bloomColor += texture(bloomBlur, fragTexCoord).rgb * 0.227027;
    
    // Neighbors (Decreasing weights)
    bloomColor += texture(bloomBlur, fragTexCoord + vec2(tex_offset.x, 0.0)).rgb * 0.1945946;
    bloomColor += texture(bloomBlur, fragTexCoord - vec2(tex_offset.x, 0.0)).rgb * 0.1945946;
    bloomColor += texture(bloomBlur, fragTexCoord + vec2(0.0, tex_offset.y)).rgb * 0.1945946;
    bloomColor += texture(bloomBlur, fragTexCoord - vec2(0.0, tex_offset.y)).rgb * 0.1945946;
    
    // Additive Blending
    vec3 result = hdrColor + bloomColor * settings.bloomStrength;

    // Tone Mapping (Reinhard)
    result = vec3(1.0) - exp(-result * settings.exposure);

    // Gamma Correction
    result = pow(result, vec3(1.0 / settings.gamma));

    outColor = vec4(result, 1.0);
}