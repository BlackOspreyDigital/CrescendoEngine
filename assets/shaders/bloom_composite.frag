#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D sceneTex;
layout(binding = 1) uniform sampler2D bloomTex;
layout(binding = 2) uniform sampler2D ssrTex; // <-- ADD THIS

// Make sure your push constants match the C++ struct!
layout(push_constant) uniform Params {
    float exposure;
    float gamma;
    float bloomStrength;
    float bloomThreshold;
    float blurRadius;
    float ssaoUVScale; 
    float ssrUVScale;  // We need this to sample the Half-Res SSR correctly!
} params;

void main() {
    vec3 color = texture(sceneTex, fragUV).rgb;
    vec3 bloom = texture(bloomTex, fragUV).rgb;
    
    // Sample SSR using the UV scale (in case you have Half-Res turned on)
    vec4 ssr = texture(ssrTex, fragUV * params.ssrUVScale);

    // Mix the Reflection into the scene based on its alpha (hit mask)
    color += ssr.rgb * ssr.a;

    // Mix the Bloom
    color += bloom * params.bloomStrength;

    // Basic Tone Mapping & Gamma Correction
    color = vec3(1.0) - exp(-color * params.exposure);
    color = pow(color, vec3(1.0 / params.gamma));

    outColor = vec4(color, 1.0);
}