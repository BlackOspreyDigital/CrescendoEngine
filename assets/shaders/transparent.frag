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
    vec4 pointLightParams;
    PointLight pointLights[16];
} global;

vec2 EquirectangularUV(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(-v.z));
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

// PCF, Normal Bias, and Bounding-Box Cascade Selection
float CalculateShadow(vec3 worldPos, vec3 normal, float viewDepth) {
    // Reduced bias to prevent light leaking (Peter Panning)
    vec3 bias = normal * 0.015; 
    
    int cascadeIndex = 3;
    vec3 projCoords = vec3(0.0);
    
    // Find the tightest, highest-res cascade that actually contains this pixel
    for (int i = 0; i < 4; i++) {
        vec4 fragPosLightSpace = global.lightSpaceMatrices[i] * vec4(worldPos + bias, 1.0);
        vec3 coords = fragPosLightSpace.xyz / fragPosLightSpace.w;
        coords.xy = coords.xy * 0.5 + 0.5;
        
        // Check if we are safely inside the shadow map bounds 
        // (We use 0.01 to 0.99 to give a 1% margin for the PCF blurring so it doesn't sample the edge)
        if (coords.x > 0.01 && coords.x < 0.99 && 
            coords.y > 0.01 && coords.y < 0.99 && 
            coords.z > 0.0 && coords.z < 1.0) {
            
            cascadeIndex = i;
            projCoords = coords;
            break;
        }
    }

    // If somehow behind the light's far plane, don't shadow
    if (projCoords.z > 1.0) return 1.0;

    // PCF (Percentage Closer Filtering) 3x3 Grid
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            shadow += texture(shadowMap, vec4(projCoords.xy + vec2(x, y) * texelSize, float(cascadeIndex), projCoords.z));
        }
    }
    
    return shadow / 9.0;
}

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

void main() {
    EntityData ent = entities[inEntityIndex];
    int texID = int(ent.albedoTint.w);
    vec3 baseColor = ent.albedoTint.rgb;

    vec4 albedoSample = texture(texSampler[texID], fragTexCoord);
    if (ent.volumeParams.x <= 0.0 && albedoSample.a < 0.5) {
        discard;
    }

    vec3 albedo = albedoSample.rgb * baseColor * fragColor;

    // --- NORMAL MAPPING ---
    vec3 N = normalize(fragNormal);
    float normalStr = ent.pbrParams.w;
    int normalTexID = int(ent.volumeColor.a);

    // FIX: Added the "&& normalTexID > 0" check!
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
    
    // --- LIGHTING VECTORS ---
    vec3 V = normalize(global.cameraPos.xyz - fragPos);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5));
    if (length(global.sunDirection.xyz) > 0.01) L = normalize(global.sunDirection.xyz);
    vec3 H = normalize(V + L);

    // --- EXTRACT PARAMS (With the NaN fix!) ---
    float roughness = max(ent.pbrParams.x, 0.04);
    float metallic  = ent.pbrParams.y;
    float emission  = ent.pbrParams.z;
    float transmission = ent.volumeParams.x;

    // --- PBR MATH (Cook-Torrance GGX) ---
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 radiance = global.sunColor.rgb * global.sunDirection.w;

    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);       
    
    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;
    kD *= (1.0 - transmission); // Let light pass through glass!

    float NdotL = max(dot(N, L), 0.0);
    float viewDist = length(global.cameraPos.xyz - fragPos);
    float shadowFactor = CalculateShadow(fragPos, N, viewDist); 
    
    vec3 directLight = (kD * albedo / PI + specular) * radiance * NdotL * shadowFactor;
    
    float skyWeight = N.z * 0.5 + 0.5;
    vec3 hemiAmbient = mix(global.groundColor.rgb, global.skyColor.rgb, skyWeight) * albedo * 0.15;
    vec3 emissive = albedo * emission;

    vec3 finalColor = directLight + hemiAmbient + emissive;

    float thickness     = max(ent.volumeParams.y, 1.0);
    float attenDist     = max(ent.volumeParams.z, 0.001); 
    float ior           = max(ent.volumeParams.w, 1.0);
    vec3  tintColor     = clamp(ent.volumeColor.rgb, vec3(0.001), vec3(0.999));

    float NdotV = abs(dot(N, V));
    float f0 = pow((1.0 - ior) / (1.0 + ior), 2.0);
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - NdotV, 5.0);

    vec3 transmissionColor = vec3(0.0);
    if (transmission > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(global.params.z, global.params.w);
        
        // Convert Normal to View Space so X/Y always align with the screen
        vec3 viewNormal = mat3(global.view) * N;
        
        // Calculate base distortion
        vec2 distortion = viewNormal.xy * (1.0 - (1.0 / ior)) * 0.1;
        if (normalStr > 1.0) distortion *= normalStr; 

        float maxLod = 10.0;
        float blurLevel = roughness * maxLod;
        vec3 sceneRefr = textureLod(refractionTexture, screenUV + distortion, blurLevel).rgb;
        
        // Proper Beer-Lambert Law 
        vec3 absorption = -log(tintColor) / attenDist;
        vec3 transmittance = exp(-absorption * thickness);
        
        transmissionColor = sceneRefr * transmittance;
    }
    
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

    float dist = length(global.cameraPos.xyz - fragPos);
    vec3 finalOutput;

    if (transmission > 0.0) {
        finalOutput = transmissionColor + finalColor + finalReflection;
        finalOutput = ApplyFog(finalOutput, dist, fragPos.z);
        outColor = vec4(finalOutput, 1.0); 
    } 
    else {
        finalOutput = finalColor + finalReflection;
        finalOutput = ApplyFog(finalOutput, dist, fragPos.z);
        outColor = vec4(finalOutput, albedoSample.a); 
    }
}