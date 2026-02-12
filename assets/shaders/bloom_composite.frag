#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D bloomBlur;

// Now these are controlled by C++!
layout(push_constant) uniform PushConstants {
    float bloomIntensity;
    float exposure;
    float gamma;
} settings;

void main() {
    vec3 hdrColor = texture(sceneColor, fragTexCoord).rgb;
    
    // --- 9-TAP GAUSSIAN BLUR ---
    vec2 tex_offset = 1.0 / textureSize(bloomBlur, 0); // Size of single texel
    vec3 bloomColor = vec3(0.0);
    
    // Center (Highest weight)
    bloomColor += texture(bloomBlur, fragTexCoord).rgb * 0.227027;
    
    // Neighbors (Decreasing weights)
    bloomColor += texture(bloomBlur, fragTexCoord + vec2(tex_offset.x, 0.0)).rgb * 0.1945946;
    bloomColor += texture(bloomBlur, fragTexCoord - vec2(tex_offset.x, 0.0)).rgb * 0.1945946;
    bloomColor += texture(bloomBlur, fragTexCoord + vec2(0.0, tex_offset.y)).rgb * 0.1945946;
    bloomColor += texture(bloomBlur, fragTexCoord - vec2(0.0, tex_offset.y)).rgb * 0.1945946;
    
    // Diagonals (Optional: Uncomment for even thicker, smoother blur)
    /*
    bloomColor += texture(bloomBlur, fragTexCoord + vec2(tex_offset.x, tex_offset.y)).rgb * 0.05;
    bloomColor += texture(bloomBlur, fragTexCoord - vec2(tex_offset.x, tex_offset.y)).rgb * 0.05;
    bloomColor += texture(bloomBlur, fragTexCoord + vec2(tex_offset.x, -tex_offset.y)).rgb * 0.05;
    bloomColor += texture(bloomBlur, fragTexCoord - vec2(tex_offset.x, -tex_offset.y)).rgb * 0.05;
    */

    // Additive Blending
    vec3 result = hdrColor + bloomColor * settings.bloomIntensity;

    // Tone Mapping (Reinhard)
    result = vec3(1.0) - exp(-result * settings.exposure);
    
    // Gamma Correction
    result = pow(result, vec3(1.0 / settings.gamma));

    outColor = vec4(result, 1.0);
}