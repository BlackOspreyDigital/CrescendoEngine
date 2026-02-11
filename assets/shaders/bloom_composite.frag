#version 450

layout(location = 0) in vec2 fragTexCoord;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneColor;
layout(binding = 1) uniform sampler2D bloomBlur;

// [FIX] Accept settings from C++
layout(push_constant) uniform PushConstants {
    float bloomIntensity;
    float exposure;
    float gamma;
} settings;

void main() {
    vec3 hdrColor = texture(sceneColor, fragTexCoord).rgb;
    vec3 bloomColor = texture(bloomBlur, fragTexCoord).rgb;

    // 1. Additive Blend with Intensity Control
    hdrColor += bloomColor * settings.bloomIntensity;

    // 2. Tone Mapping (Exposure)
    vec3 result = vec3(1.0) - exp(-hdrColor * settings.exposure);

    // 3. Gamma Correction
    result = pow(result, vec3(1.0 / settings.gamma));

    outColor = vec4(result, 1.0);
}