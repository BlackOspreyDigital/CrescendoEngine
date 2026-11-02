#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out float outOcclusion;

// G-Buffer Images
layout(binding = 0) uniform sampler2D gPosition;
layout(binding = 1) uniform sampler2D gNormal;
layout(binding = 2) uniform sampler2D texNoise; 

layout(push_constant) uniform SSAOParams {
    mat4 projection; // Fixed: Added semicolon
    vec4 samples[64]; // Using vec4 for std140 alignment padding
} params;

// A 4x4 noise texture tiled across the screen
const vec2 noiseScale = vec2(1280.0 / 4.0, 720.0 / 4.0); // Adjust to resolution
const float radius = 0.5;
const float bias = 0.025;

void main() {
    vec3 fragPos = texture(gPosition, fragUV).xyz;
    vec3 normal = normalize(texture(gNormal, fragUV).rgb); // Fixed: Added missing normal sampling
    vec3 randomVec = normalize(texture(texNoise, fragUV * noiseScale).xyz);

    // Create TBN matrix to transform kernel from tangent to view space
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for(int i = 0; i < 64; ++i) {
        // transform sample to view space
        vec3 samplePos = TBN * params.samples[i].xyz;
        samplePos = fragPos + samplePos * radius; // Fixed: Added equals sign

        // Project sample to screen space to find its UV coordinates
        vec4 offset = vec4(samplePos, 1.0);
        offset = params.projection * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;

        // Get depth of geometry at this samples UV 
        float sampleDepth = texture(gPosition, offset.xy).z; // Fixed: flaot -> float

        // Range check prevents objects far in the background from occluding foreground objects
        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
        occlusion += (sampleDepth >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
        
    }
    outOcclusion = 1.0 - (occlusion / 64.0);
}