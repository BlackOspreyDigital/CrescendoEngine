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
layout(location = 1) out vec4 outNormal; 

layout(binding = 0) uniform sampler2D texSampler[100];
layout(binding = 1) uniform sampler2D skyTexture;



layout(binding = 4) uniform sampler2DArrayShadow shadowMap;

struct EntityData {
    vec4 pos;
    vec4 rot;
    vec4 scale;
    vec4 sphereBounds;
    vec4 albedoTint;
    vec4 pbrParams;
    vec4 volumeParams;
    vec4 volumeColor;
};

layout(std430, set = 0, binding = 2) readonly buffer ObjectBuffer {
    EntityData entities[];
};

struct PointLight {
    vec4 positionAndRadius;
    vec4 colorAndIntensity;
};

layout(set = 0, binding = 3) uniform GlobalUniforms {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 params;
    vec4 fogColor;
    vec4 fogParams;
    vec4 skyColor;
    vec4 groundColor;
    
    // --- POINT LIGHTS ---
    vec4 pointLightParams; // x = count (No stray 'int' variables here!)
    PointLight pointLights[16];
} global;

vec2 EquirectangularUV(vec3 v) {
    // [FIX] Invert v.z so the sky maps to the top (V=0) of the Vulkan texture
    vec2 uv = vec2(atan(v.y, v.x), asin(-v.z)); 
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

// Cascaded Shadow Map Math
float CalculateShadow(vec3 worldPos, float viewDepth) {
    int cascadeIndex = 3;
    if (viewDepth < global.cascadeSplits.x) cascadeIndex = 0;
    else if (viewDepth < global.cascadeSplits.y) cascadeIndex = 1;
    else if (viewDepth < global.cascadeSplits.z) cascadeIndex = 2;

    vec4 fragPosLightSpace = global.lightSpaceMatrices[cascadeIndex] * vec4(worldPos, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.z > 1.0) return 1.0;

    return texture(shadowMap, vec4(projCoords.xy, float(cascadeIndex), projCoords.z));
}

vec3 ApplyFog(vec3 rgb, float dist, float worldZ) {
    float fogDensity = global.fogColor.a;
    float fogFalloff = global.fogParams.x;
    float maxOpacity = global.fogParams.y;
    float fogStart   = global.fogParams.z;
    float fogHeight  = global.fogParams.w;

    float d = max(dist - fogStart, 0.0);
    float distFactor = exp(-d * fogDensity);
    float heightFactor = exp(-fogFalloff * (worldZ - fogHeight));

    float fogFactor = clamp(distFactor * heightFactor, 1.0 - maxOpacity, 1.0);
    return mix(global.fogColor.rgb, rgb, fogFactor);
}

void main() {
    EntityData ent = entities[inEntityIndex];
    int texID = int(ent.albedoTint.w);
    vec3 baseColor = ent.albedoTint.rgb;

    vec4 albedoSample = texture(texSampler[texID], fragTexCoord);

    if (albedoSample.a < 0.5) {
        discard;
    }

    vec3 albedo = albedoSample.rgb * baseColor * fragColor;

    vec3 N = normalize(fragNormal);
    float normalStr = ent.pbrParams.w;
    int normalTexID = int(ent.volumeColor.a);
    
    if (normalStr > 0.0 && normalTexID > 0) {
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

    // Output to Normal G-Buffer (For SSR)
    outNormal = vec4(N, ent.pbrParams.x); // Normal + Roughness

    // Lighting Vectors
    vec3 V = normalize(global.cameraPos.xyz - fragPos);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5));
    if (length(global.sunDirection.xyz) > 0.01) L = normalize(global.sunDirection.xyz);
    vec3 H = normalize(V + L);

    float roughness = ent.pbrParams.x;
    float metallic  = ent.pbrParams.y;
    float emission  = ent.pbrParams.z;

    // Hemisphere GI
    float skyWeight = N.z * 0.5 + 0.5; 
    vec3 hemiAmbient = mix(global.groundColor.rgb, global.skyColor.rgb, skyWeight) * albedo * 0.15;

    // PBR Lighting
    float NdotL = max(dot(N, L), 0.0);
    float shininess = (1.0 - roughness) * (1.0 - roughness) * 128.0; 
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, max(shininess, 1.0));
    
    vec3 kS = vec3(spec) * (1.0 - roughness);
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);

    // --- 1. SHADOWS & DIRECTIONAL LIGHT ---
    float viewDist = length(global.cameraPos.xyz - fragPos);
    float shadowFactor = CalculateShadow(fragPos, viewDist);
    vec3 directLight = (kD * albedo / 3.14159 + kS) * shadowFactor * NdotL * global.sunColor.rgb;
    
    // --- 2. ACCUMULATE POINT LIGHTS ---
    vec3 pointLightAccum = vec3(0.0);
        int pLightCount = int(global.pointLightParams.x);
        for(int i = 0; i < pLightCount; i++) {
        vec3 lightPos = global.pointLights[i].positionAndRadius.xyz;
        float radius = global.pointLights[i].positionAndRadius.w;
        vec3 lightColor = global.pointLights[i].colorAndIntensity.xyz;
        float intensity = global.pointLights[i].colorAndIntensity.w;

        vec3 L_pt = lightPos - fragPos;
        float dist_pt = length(L_pt);
        L_pt = normalize(L_pt);

        // Attenuation (fades out over distance)
        float attenuation = clamp(1.0 - (dist_pt * dist_pt) / (radius * radius), 0.0, 1.0);
        attenuation *= attenuation; 

        float NdotL_pt = max(dot(N, L_pt), 0.0);
        vec3 H_pt = normalize(V + L_pt);
        float NdotH_pt = max(dot(N, H_pt), 0.0);
        float spec_pt = pow(NdotH_pt, max(shininess, 1.0));

        vec3 kS_pt = vec3(spec_pt) * (1.0 - roughness);
        vec3 kD_pt = (vec3(1.0) - kS_pt) * (1.0 - metallic);

        pointLightAccum += (kD_pt * albedo / 3.14159 + kS_pt) * lightColor * intensity * NdotL_pt * attenuation;
    }

    // --- 3. COMBINE ALL LIGHTING ---
    vec3 emissive = albedo * emission;
    vec3 finalColor = directLight + pointLightAccum + hemiAmbient + emissive;

    // Skybox Reflection 
    vec3 R = reflect(-V, N);
    vec3 skyRefl = vec3(0.0);
    int skyType = int(global.params.y);

    if (skyType == 0) {
        skyRefl = global.skyColor.rgb;
    } 
    else if (skyType == 1) {
        float upBlend = smoothstep(-0.2, 0.4, R.z);
        skyRefl = mix(global.groundColor.rgb, global.skyColor.rgb, upBlend);
        float sunDot = max(dot(R, normalize(global.sunDirection.xyz)), 0.0);
        skyRefl += global.sunColor.rgb * global.sunDirection.w * pow(sunDot, 256.0);
    } 
    else {
        skyRefl = texture(skyTexture, EquirectangularUV(normalize(R))).rgb;
    }
    
    skyRefl *= (1.0 - roughness);
    
    float NdotV = abs(dot(N, V));
    float f0 = mix(0.04, 1.0, metallic);
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - NdotV, 5.0);
    
    vec3 finalReflection = skyRefl * (kS + fresnel) * metallic; // Opaque reflection heavily relies on metallic
    
    vec3 finalOutput = finalColor + finalReflection;
    
    // Fog Integration
    float dist = length(global.cameraPos.xyz - fragPos);
    finalOutput = ApplyFog(finalOutput, dist, fragPos.z);

    // Opaque always outputs 1.0 alpha
    outColor = vec4(finalOutput, 1.0);
}