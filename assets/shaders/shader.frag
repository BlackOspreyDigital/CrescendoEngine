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
layout(binding = 1) uniform samplerCube skyTexture;
layout(binding = 4) uniform sampler2DArrayShadow shadowMap;
layout(binding = 5) uniform sampler2D refractionTexture;


struct EntityData {
    vec4 pos;
    vec4 rot;
    vec4 scale;
    vec4 sphereBounds;
    vec4 albedoTint;
    vec4 pbrParams;
    vec4 volumeParams;
    vec4 volumeColor;
    vec4 advancedPbr;  
    vec4 padding0;
    vec4 padding1;
    vec4 padding2;
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

// Exponential Height Fog MATH
vec3 ApplyFog(vec3 rgb, float dist, float worldZ) {
    float fogDensity = global.fogColor.a;
    if (fogDensity <= 0.0001) return rgb; // Safely early-out if fog is disabled

    float fogFalloff = max(global.fogParams.x, 0.001);
    float maxOpacity = clamp(global.fogParams.y, 0.0, 1.0);
    float fogStart   = max(global.fogParams.z, 0.0);
    float fogHeight  = global.fogParams.w;

    float d = max(dist - fogStart, 0.0);
    
    // As worldZ goes ABOVE the fog plane, density drops off.
    float heightFalloff = max(worldZ - fogHeight, 0.0);
    float effectiveDensity = fogDensity * exp(-fogFalloff * heightFalloff);
    
    float fogFactor = exp(-d * effectiveDensity);
    fogFactor = clamp(fogFactor, 1.0 - maxOpacity, 1.0); // Limit maximum thickness
    
    return mix(global.fogColor.rgb, rgb, fogFactor);
}

vec2 EquirectangularUV(vec3 v) {
    // Z is now the vertical axis (asin), and Y/X form the horizontal plane (atan)
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z)); 
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}



const float PI = 3.14159265359;

// Calculates how many micro-facets are aligned with the halfway vector (The highlight shape)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return num / denom;
}

// Calculates geometric shadowing (micro-facets casting shadows on each other)
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Calculates the Fresnel effect (more reflective at grazing angles)
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// --- PCF HELPER ---
float SamplePCF(vec3 coords, int cascadeIndex) {
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            shadow += texture(shadowMap, vec4(coords.xy + vec2(x, y) * texelSize, float(cascadeIndex), coords.z));
        }
    }
    return shadow / 9.0;
}

// PCF, Normal Bias, and Bounding-Box Cascade Selection
float CalculateShadow(vec3 worldPos, vec3 normal, float viewDepth) {
    vec3 bias = normal * 0.015;
    int cascadeIndex = -1;
    vec3 projCoords = vec3(0.0);
    
    // Find the tightest, highest-res cascade that actually contains this pixel
    for (int i = 0; i < 4; i++) {
        vec4 fragPosLightSpace = global.lightSpaceMatrices[i] * vec4(worldPos + bias, 1.0);
        vec3 coords = fragPosLightSpace.xyz / fragPosLightSpace.w;
        coords.xy = coords.xy * 0.5 + 0.5;
        if (coords.x > 0.01 && coords.x < 0.99 && 
            coords.y > 0.01 && coords.y < 0.99 && 
            coords.z > 0.0 && coords.z < 1.0) {
     
            cascadeIndex = i;
            projCoords = coords;
            break;
        }
    }

    if (cascadeIndex == -1 || projCoords.z > 1.0) return 1.0;
    float shadow = SamplePCF(projCoords, cascadeIndex);

    // ==========================================
    // 1. CROSS-CASCADE BLENDING
    // ==========================================
    float currentSplitDist = global.cascadeSplits[cascadeIndex];
    float blendBand = 5.0; 
    
    if (cascadeIndex < 3 && viewDepth > (currentSplitDist - blendBand)) {
        vec4 nextLightSpace = global.lightSpaceMatrices[cascadeIndex + 1] * vec4(worldPos + bias, 1.0);
        vec3 nextCoords = nextLightSpace.xyz / nextLightSpace.w;
        nextCoords.xy = nextCoords.xy * 0.5 + 0.5;
        float nextShadow = SamplePCF(nextCoords, cascadeIndex + 1);
        float blendFactor = smoothstep(currentSplitDist - blendBand, currentSplitDist, viewDepth);
        shadow = mix(shadow, nextShadow, blendFactor);
    }

    // ==========================================
    // 2. FAR DISTANCE FADE
    // ==========================================
    float maxShadowDist = global.cascadeSplits[3];
    float fadeBand = 15.0; 
    float fadeFactor = smoothstep(maxShadowDist - fadeBand, maxShadowDist, viewDepth);
    return mix(shadow, 1.0, fadeFactor);
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

            // --- TBN BASIS ---
            vec3 T = normalize(fragTangent);
            vec3 B = normalize(fragBitangent);
            vec3 N_basis = normalize(fragNormal);

            // Re-orthogonalize T with respect to N (Gram-Schmidt)
            T = normalize(T - dot(T, N_basis) * N_basis);
            
            mat3 TBN = mat3(T, B, N_basis);
            N = normalize(TBN * mapNormal);
        }
    }

    // --- LIGHTING VECTORS ---
    vec3 V = normalize(global.cameraPos.xyz - fragPos);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5));
    if (length(global.sunDirection.xyz) > 0.01) L = normalize(global.sunDirection.xyz);
    vec3 H = normalize(V + L);

    // --- ORM EXTRACTION ---
    float roughness = max(ent.pbrParams.x, 0.04); // Clamped to prevent NaN!
    float metallic  = ent.pbrParams.y;
    float emission  = ent.pbrParams.z;
    float ao        = 1.0; 

    int ormTexID = int(ent.advancedPbr.w);
    if (ormTexID > 0) {
        vec3 ormSample = texture(texSampler[ormTexID], fragTexCoord).rgb;
        ao        = ormSample.r;             
        roughness = max(roughness * ormSample.g, 0.04); 
        metallic  = metallic * ormSample.b;  
    }

    // --- BASE REFLECTIVITY (F0) ---
    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    // --- HEMISPHERE GI (with AO!) ---
    float skyWeight = N.z * 0.5 + 0.5;
    vec3 hemiAmbient = mix(global.groundColor.rgb, global.skyColor.rgb, skyWeight) * albedo * 0.15 * ao;

    // --- 1. DIRECTIONAL LIGHT & SHADOWS ---
    vec3 directLight = vec3(0.0);
    float viewDist = length(global.cameraPos.xyz - fragPos);
    float shadowFactor = CalculateShadow(fragPos, N, viewDist); 

    if (length(global.sunDirection.xyz) > 0.01) {
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 0.0);
        
        // --- BASE LAYER (Standard GGX) ---
        float NDF = DistributionGGX(N, H, roughness);   
        float G   = GeometrySmith(N, V, L, roughness);
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
        
        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * NdotV * NdotL + 0.0001;
        vec3 baseSpecular = numerator / denominator;
        
        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        // --- CLEARCOAT LAYER (Varnish / Car Paint) ---
        float ccWeight    = ent.advancedPbr.x;
        float ccRoughness = max(ent.advancedPbr.y, 0.04); 
        
        float ccNDF = DistributionGGX(N, H, ccRoughness);
        float ccG   = GeometrySmith(N, V, L, ccRoughness);
        vec3 ccF    = fresnelSchlick(max(dot(H, V), 0.0), vec3(0.04)); 
        
        vec3 ccSpecular = (ccNDF * ccG * ccF) / denominator;

        // --- SHEEN LAYER (Velvet / Peach Fuzz) ---
        float sheenWeight = ent.advancedPbr.z;
        vec3 sheenColor = albedo * sheenWeight * pow(clamp(1.0 - NdotV, 0.0, 1.0), 4.0) * pow(clamp(1.0 - NdotL, 0.0, 1.0), 4.0);

        // --- ENERGY CONSERVATION & BLENDING ---
        vec3 baseLight = (kD * albedo / PI + baseSpecular);
        baseLight = mix(baseLight, baseLight * (vec3(1.0) - ccF), ccWeight);

        vec3 radiance = global.sunColor.rgb * global.sunDirection.w;
        directLight = (baseLight + (ccSpecular * ccWeight) + sheenColor) * radiance * NdotL * shadowFactor;
    }

    // --- 2. POINT LIGHTS (GGX + CLEARCOAT) ---
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
        vec3 H_pt = normalize(V + L_pt);

        float attenuation = clamp(1.0 - (dist_pt * dist_pt) / (radius * radius), 0.0, 1.0);
        attenuation *= attenuation; 

        float NdotL_pt = max(dot(N, L_pt), 0.0);
        float NdotV_pt = max(dot(N, V), 0.0);
        
        // --- BASE LAYER ---
        float NDF_pt = DistributionGGX(N, H_pt, roughness);   
        float G_pt   = GeometrySmith(N, V, L_pt, roughness);      
        vec3 F_pt    = fresnelSchlick(max(dot(H_pt, V), 0.0), F0);       
        
        vec3 numerator_pt    = NDF_pt * G_pt * F_pt;
        float denominator_pt = 4.0 * NdotV_pt * NdotL_pt + 0.0001;
        vec3 baseSpecular_pt = numerator_pt / denominator_pt;
        
        vec3 kS_pt = F_pt;
        vec3 kD_pt = vec3(1.0) - kS_pt;
        kD_pt *= 1.0 - metallic;

        // --- CLEARCOAT LAYER ---
        float ccWeight    = ent.advancedPbr.x;
        float ccRoughness = max(ent.advancedPbr.y, 0.04);
        
        float ccNDF_pt = DistributionGGX(N, H_pt, ccRoughness);
        float ccG_pt   = GeometrySmith(N, V, L_pt, ccRoughness);
        vec3 ccF_pt    = fresnelSchlick(max(dot(H_pt, V), 0.0), vec3(0.04)); 
        
        vec3 ccSpecular_pt = (ccNDF_pt * ccG_pt * ccF_pt) / denominator_pt;

        // --- SHEEN LAYER ---
        float sheenWeight = ent.advancedPbr.z;
        vec3 sheenColor_pt = albedo * sheenWeight * pow(clamp(1.0 - NdotV_pt, 0.0, 1.0), 4.0) * pow(clamp(1.0 - NdotL_pt, 0.0, 1.0), 4.0);

        // --- BLENDING ---
        vec3 baseLight_pt = (kD_pt * albedo / PI + baseSpecular_pt);
        baseLight_pt = mix(baseLight_pt, baseLight_pt * (vec3(1.0) - ccF_pt), ccWeight);

        vec3 radiance_pt = lightColor * intensity * attenuation;
        pointLightAccum += (baseLight_pt + (ccSpecular_pt * ccWeight) + sheenColor_pt) * radiance_pt * NdotL_pt;
    }

    // --- 3. COMBINE ALL LIGHTING ---
    vec3 emissive = albedo * emission;
    vec3 finalColor = directLight + pointLightAccum + hemiAmbient + emissive;

    // --- SKYBOX REFLECTION ---
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
        skyRefl = texture(skyTexture, normalize(R)).rgb;
    }
    
    skyRefl *= (1.0 - roughness);
    
    // --- AMBIENT REFLECTION (GGX) ---
    vec3 F_ambient = fresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 finalReflection = skyRefl * F_ambient;
    
    vec3 finalOutput = finalColor + finalReflection;

    // Fog Integration
    float dist = length(global.cameraPos.xyz - fragPos);
    finalOutput = ApplyFog(finalOutput, dist, fragPos.z);

    outColor = vec4(finalOutput, albedoSample.a);
}