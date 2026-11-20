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

vec3 ApplyFog(vec3 rgb, float dist, vec3 fragPos) {
    float fogDensity = global.fogColor.a;
    if (fogDensity <= 0.00001) return rgb;

    float fogFalloff = max(global.fogParams.x, 0.001);
    float maxOpacity = clamp(global.fogParams.y, 0.0, 1.0);
    float fogStart   = max(global.fogParams.z, 0.0);
    float fogFloor   = global.fogParams.w;

    vec3 ray = fragPos;
    float rayLen = max(dist, 0.001);
    vec3 v = ray / rayLen;

    float cz = max(global.cameraPos.z - fogFloor, 0.0);
    float vz = v.z;

    float tau = 0.0;
    if (abs(vz) > 0.001) {
        float t0 = min(fogStart, rayLen);
        float t1 = rayLen;
        float intStart = exp(-fogFalloff * (cz + t0 * vz));
        float intEnd   = exp(-fogFalloff * (cz + t1 * vz));
        tau = (fogDensity / (fogFalloff * vz)) * (intStart - intEnd);
        tau = max(tau, 0.0);
    } else {
        float d = max(dist - fogStart, 0.0);
        tau = fogDensity * exp(-fogFalloff * cz) * d;
    }

    float fowRadius = global.params.w;
    float visionClear = 0.0;
    if (fowRadius > 1.0) {
        vec3 worldPos = fragPos + global.cameraPos.xyz;
        float distToBase = length(worldPos.xy - global.fogParams.yz);
        visionClear = smoothstep(fowRadius, fowRadius * 0.70, distToBase);
    }
    float tauEffective = tau * (1.0 - clamp(visionClear, 0.0, 1.0));
    float transmittance = clamp(exp(-tauEffective), 1.0 - maxOpacity, 1.0);
    return mix(global.fogColor.rgb, rgb, transmittance);
}

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float perlinNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(dot(hash2(i + vec2(0.0, 0.0)), f - vec2(0.0, 0.0)),
                   dot(hash2(i + vec2(1.0, 0.0)), f - vec2(1.0, 0.0)), u.x),
               mix(dot(hash2(i + vec2(0.0, 1.0)), f - vec2(0.0, 1.0)),
                   dot(hash2(i + vec2(1.0, 1.0)), f - vec2(1.0, 1.0)), u.x), u.y);
}

float fbmWater(vec2 p) {
    float total = 0.0;
    total += 0.5000 * perlinNoise(p); p *= 2.02;
    total += 0.2500 * perlinNoise(p); p *= 2.03;
    total += 0.1250 * perlinNoise(p);
    return total;
}

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

    // Procedural Perlin Wave Normal & Distortion (Eliminates bitangent grid artifacts)
    vec2 waveCoord1 = fragPos.xy * 0.08 + vec2(time * 0.04, time * 0.03);
    vec2 waveCoord2 = fragPos.xy * 0.18 - vec2(time * 0.03, time * 0.05);
    float eps = 0.08;
    float hL = fbmWater(waveCoord1 - vec2(eps, 0.0)) * 0.6 + fbmWater(waveCoord2 - vec2(eps, 0.0)) * 0.4;
    float hR = fbmWater(waveCoord1 + vec2(eps, 0.0)) * 0.6 + fbmWater(waveCoord2 + vec2(eps, 0.0)) * 0.4;
    float hD = fbmWater(waveCoord1 - vec2(0.0, eps)) * 0.6 + fbmWater(waveCoord2 - vec2(0.0, eps)) * 0.4;
    float hU = fbmWater(waveCoord1 + vec2(0.0, eps)) * 0.6 + fbmWater(waveCoord2 + vec2(0.0, eps)) * 0.4;

    vec3 waveNormal = normalize(vec3(-(hR - hL) * 0.45, -(hU - hD) * 0.45, 1.0));
    vec3 N = normalize(waveNormal);
    vec2 distortion = vec2(hR - hL, hU - hD) * 0.015;

    vec3 refractColor = textureLod(refractionMap, screenUV + distortion, 0.0).rgb;

    // 2. Z-UP PROCEDURAL SKY REFLECTION (Fixes the Equator Highlights)
    vec3 V = normalize(fragPos);
    vec3 R = reflect(V, N); 

    float skyBlend = clamp(R.z * 2.0 + 0.2, 0.0, 1.0); 
    
    float ambientIntensity = global.params.x;
    vec3 reflection = mix(global.groundColor.rgb, global.skyColor.rgb, skyBlend) * ambientIntensity;

    // Sun Reflection
    vec3 sunDir = normalize(global.sunDirection.xyz);
    float sunDot = max(dot(R, sunDir), 0.0);
    float sunGlow = pow(sunDot, 256.0) * global.sunDirection.w; 
    reflection += global.sunColor.rgb * sunGlow;

    // 3. Fresnel Effect
    float fresnel = max(0.0, 1.0 - max(dot(-V, N), 0.0));
    fresnel = pow(fresnel, 3.0); 
    fresnel = mix(0.1, 0.8, fresnel); 

    // 4. Dithered Optical Depth & Submerged Gradient
    const float bayerMatrix[16] = float[16](
        0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
       12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0,
        3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
       15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0
    );
    int bayerIdx = (int(gl_FragCoord.x) % 4) + (int(gl_FragCoord.y) % 4) * 4;
    float dither = bayerMatrix[bayerIdx] - 0.5;

    vec3 worldPosGlobal = fragPos + global.cameraPos.xyz;
    float waterDepth = max(0.0, -worldPosGlobal.z * 0.2 + (1.0 - N.z) * 3.0);
    float depthFactor = clamp(waterDepth * 0.35 + dither * 0.06, 0.15, 0.95);
    vec3 shallowColor = mix(refractColor, ent.albedoTint.rgb * 1.2, 0.25);
    vec3 deepWaterColor = vec3(0.02, 0.10, 0.20);
    vec3 baseColor = mix(shallowColor, deepWaterColor, depthFactor);

    // Dithered depth shadow attenuation
    float depthShadow = clamp(1.0 - waterDepth * 0.20 + dither * 0.08, 0.40, 1.0);
    baseColor *= depthShadow;
    // Apply Fresnel Reflection
    vec3 finalColor = mix(baseColor, reflection, fresnel);
    float dist = length(fragPos);
    finalColor = ApplyFog(finalColor, dist, fragPos);

    outColor = vec4(finalColor, 1.0);
    outNormalRoughness = vec4(N, roughness);
}