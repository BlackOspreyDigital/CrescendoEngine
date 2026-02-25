#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;
layout(location = 6) in flat int inEntityIndex;
layout(location = 7) in vec2 fragTexCoord1; 

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler[100];
layout(binding = 1) uniform sampler2D skyTexture;
// NOTE: Binding 5 (refractionTexture) has been removed!

struct EntityData {
    vec4 pos;
    vec4 rot;
    vec4 scale;
    vec4 sphereBounds;
    vec4 albedoTint;
    vec4 pbrParams;    // x=Roughness, y=Metallic, z=Emission, w=NormalStrength
    vec4 volumeParams; // Unused in opaque, but kept for struct alignment
    vec4 volumeColor;
};

layout(std430, set = 0, binding = 2) readonly buffer ObjectBuffer { 
    EntityData entities[];
};

layout(set = 0, binding = 3) uniform GlobalUniforms {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 params;
    vec4 fogColor;
    vec4 fogParams;
    vec4 skyColor;
    vec4 groundColor;
    mat4 lightSpaceMatrices[4]; // Added to match C++
    vec4 cascadeSplits;         // Added to match C++
} global;

// Exponential Height Fog MATH
vec3 ApplyFog(vec3 rgb, float dist, float worldZ) {
    float fogDensity = global.fogColor.a;
    float fogFalloff = global.fogParams.x;
    float maxOpacity = global.fogParams.y; // Fixed typo: fogParmas -> fogParams
    float fogStart   = global.fogParams.z;
    float fogHeight  = global.fogParams.w;

    // calculate distance-based falloff 
    float d = max(dist - fogStart, 0.0);
    float distFactor = exp(-d * fogDensity);

    // calculate height-based falloff 
    // Fixed: GLSL uses pow(), and we apply height falloff to the density
    float heightFactor = exp(-fogFalloff * (worldZ - fogHeight));

    // combine and clamp to ensure we never over-fog beyond maxOpacity
    float fogFactor = clamp(distFactor * heightFactor, 1.0 - maxOpacity, 1.0);

    return mix(global.fogColor.rgb, rgb, fogFactor);
}

vec2 EquirectangularUV(vec3 v) {
    // Z is now the vertical axis (asin), and Y/X form the horizontal plane (atan)
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z)); 
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

void main() {
    EntityData ent = entities[inEntityIndex];
    int texID = int(ent.albedoTint.w);
    vec3 baseColor = ent.albedoTint.rgb;
    
    vec4 albedoSample = texture(texSampler[texID], fragTexCoord);
    
    // Standard Alpha cutout for opaque objects (leaves, fences, etc.)
    if (albedoSample.a < 0.5) {
        discard;
    }

    vec3 albedo = albedoSample.rgb * baseColor * fragColor;

    // --- NORMAL MAPPING ---
    vec3 N = normalize(fragNormal);
    float normalStr = ent.pbrParams.w;
    int normalTexID = int(ent.volumeColor.a);

    if (normalStr > 0.0) {
        vec3 mapNormal = texture(texSampler[normalTexID], fragTexCoord).rgb;
        mapNormal = mapNormal * 2.0 - 1.0;
        mapNormal.xy *= normalStr;

        if (length(mapNormal) > 0.001) {
            mapNormal = normalize(mapNormal);
            vec3 T = normalize(fragTangent);
            vec3 B = normalize(fragBitangent);
            T = normalize(T - dot(T, N) * N);
            mat3 TBN = mat3(T, B, N);
            N = normalize(TBN * mapNormal);
        }
    }

    // --- UPDATED AMBIENT: HEMISPHERE GI ---
    float skyWeight = N.z * 0.5 + 0.5;
    vec3 hemiAmbient = mix(global.groundColor.rgb, global.skyColor.rgb, skyWeight) * albedo * 0.15;
    
    // --- LIGHTING VECTORS ---
    vec3 V = normalize(global.cameraPos.xyz - fragPos);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5));
    if (length(global.sunDirection.xyz) > 0.01) L = normalize(global.sunDirection.xyz);
    vec3 H = normalize(V + L);

    float roughness = ent.pbrParams.x;
    float metallic  = ent.pbrParams.y;
    float emission  = ent.pbrParams.z;

    // --- PBR MATH ---
    float NdotL = max(dot(N, L), 0.0);
    float shininess = (1.0 - roughness) * (1.0 - roughness) * 128.0; 
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, max(shininess, 1.0));
    
    vec3 kS = vec3(spec) * (1.0 - roughness);
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    vec3 directLight = (kD * albedo / 3.14159 + kS) * 1.0 * NdotL * global.sunColor.rgb;
    vec3 ambient = albedo * 0.03;
    vec3 emissive = albedo * emission;

    vec3 finalColor = directLight + hemiAmbient + emissive;

    // --- SKYBOX REFLECTION ---
    vec3 R = reflect(-V, N);
    vec3 skyRefl = vec3(0.0);
    int skyType = int(global.params.y);

    if (skyType == 0) {
        skyRefl = global.skyColor.rgb;
    }
    else if (skyType == 1) {
        // Procedural reflection
        float upBlend = smoothstep(-0.2, 0.4, R.z);
        skyRefl = mix(global.groundColor.rgb, global.skyColor.rgb, upBlend);

        // Add a bright specular highlight from the procedural sun 
        float sunDot = max(dot(R, normalize(global.sunDirection.xyz)), 0.0);
        skyRefl += global.sunColor.rgb * global.sunDirection.w * pow(sunDot, 256.0);
    }
    else {
        // HDR reflection 
        skyRefl = texture(skyTexture, EquirectangularUV(normalize(R))).rgb; // <--- FIXED (uppercase 'R')
    }

    skyRefl *= (1.0 - roughness);
}