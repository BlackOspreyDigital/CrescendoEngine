#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec4 fragClipSpace; // Back to Clip Space!
layout(location = 6) in flat int inEntityIndex;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormalRoughness;

layout(binding = 0) uniform sampler2D texSampler[100];
layout(binding = 1) uniform samplerCube skyTexture;

struct EntityData {
    vec4 pos; vec4 rot; vec4 scale; vec4 sphereBounds;
    vec4 albedoTint; vec4 pbrParams; vec4 volumeParams; vec4 volumeColor;
    vec4 advancedPbr; vec4 extendedPbr; vec4 padding1; vec4 padding2;
};
layout(std430, set = 0, binding = 2) readonly buffer ObjectBuffer { EntityData entities[]; };

layout(set = 0, binding = 3) uniform GlobalUniforms {
    mat4 viewProj; mat4 view; mat4 proj; mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits; vec4 cameraPos; vec4 sunDirection; vec4 sunColor;
    vec4 params; vec4 fogColor; vec4 fogParams; vec4 skyColor; vec4 groundColor;
} global;

layout(binding = 5) uniform sampler2D refractionMap;

layout(push_constant) uniform Constants { uint entityIndex; } PushConsts;

void main() {
    EntityData ent = entities[inEntityIndex];
    float roughness = ent.pbrParams.x;
    float time = global.params.x;

    // 1. CAMERA-LOCKED REFRACTION (Fixes the Sliding)
    vec2 ndc = fragClipSpace.xy / fragClipSpace.w;
    vec2 screenUV = ndc * 0.5 + 0.5;

    // [Vulkan Projection Note]
    // If you invert your view projection in C++, the refraction image might appear upside down.
    // If your underwater terrain is flipped vertically, simply uncomment this line:
    // screenUV.y = 1.0 - screenUV.y; 

    // Subtle distortion
    vec2 distortion = vec2(sin(fragUV.y * 10.0 + time), cos(fragUV.x * 10.0 + time)) * 0.005;

    // KEEP textureLod to prevent Mipmap Panic (the black triangle tearing)
    vec3 refractColor = textureLod(refractionMap, screenUV + distortion, 0.0).rgb;

    // 2. Z-UP PROCEDURAL SKY REFLECTION (Fixes the Equator Highlights)
    vec3 V = normalize(fragPos - global.cameraPos.xyz);
    vec3 N = normalize(fragNormal);
    vec3 R = reflect(V, N); 

    // --- THE FIX: R.z is the vertical axis in your engine! ---
    float skyBlend = clamp(R.z * 2.0 + 0.2, 0.0, 1.0); 
    vec3 reflection = mix(global.groundColor.rgb, global.skyColor.rgb, skyBlend);

    // Sun Reflection
    vec3 sunDir = normalize(global.sunDirection.xyz);
    float sunDot = max(dot(R, sunDir), 0.0);
    float sunGlow = pow(sunDot, 256.0) * global.sunDirection.w; 
    reflection += global.sunColor.rgb * sunGlow;

    // 3. Fresnel Effect
    float fresnel = max(0.0, 1.0 - max(dot(-V, N), 0.0));
    fresnel = pow(fresnel, 3.0); 
    fresnel = mix(0.1, 0.8, fresnel); 

    // 4. Mix Refraction and Water Tint
    vec3 waterTint = ent.albedoTint.rgb;
    float waterOpacity = 0.4; 
    vec3 baseColor = mix(refractColor, waterTint, waterOpacity);

    // Apply Fresnel Reflection
    vec3 finalColor = mix(baseColor, reflection, fresnel);

    outColor = vec4(finalColor, 1.0);
    outNormalRoughness = vec4(N, roughness);
}