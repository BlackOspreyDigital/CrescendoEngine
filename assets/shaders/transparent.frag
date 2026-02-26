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
layout(binding = 5) uniform sampler2D refractionTexture;

struct EntityData {
    vec4 pos;
    vec4 rot;
    vec4 scale;
    vec4 sphereBounds;
    vec4 albedoTint;
    vec4 pbrParams;    // x=Roughness, y=Metallic, z=Emission, w=NormalStrength
    vec4 volumeParams; // x=Transmission, y=Thickness, z=AttDist, w=IOR
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

// --- Z-UP SPHERICAL MAPPING ---
vec2 EquirectangularUV(vec3 v) {
    // [FIX] Invert v.z so the sky maps to the top (V=0) of the Vulkan texture
    vec2 uv = vec2(atan(v.y, v.x), asin(-v.z)); 
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

// --- HEIGHT FOG ---
vec3 ApplyFog(vec3 rgb, float dist, float worldZ) {
    float fogDensity = global.fogColor.a;
    float fogFalloff = global.fogParams.x;
    float maxOpacity = global.fogParams.y;
    float fogStart   = global.fogParams.z;
    float fogHeight  = global.fogParams.w;

    float d = max(dist - fogStart, 0.0);
    float distFactor = exp(-d * fogDensity);

    float fogDepth = max(fogHeight - worldZ, 0.0); 
    float heightFactor = exp(-fogFalloff * fogDepth);
    
    float fogFactor = clamp(distFactor * heightFactor, 1.0 - maxOpacity, 1.0);
    return mix(global.fogColor.rgb, rgb, fogFactor);
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

    float roughness = ent.pbrParams.x;
    float metallic  = ent.pbrParams.y;
    float emission  = ent.pbrParams.z;
    float transmission = ent.volumeParams.x; 

    // --- HEMISPHERE GI ---
    float skyWeight = N.z * 0.5 + 0.5; 
    vec3 hemiAmbient = mix(global.groundColor.rgb, global.skyColor.rgb, skyWeight) * albedo * 0.15;

    // --- PBR MATH ---
    float NdotL = max(dot(N, L), 0.0);
    float shininess = (1.0 - roughness) * (1.0 - roughness) * 128.0; 
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, max(shininess, 1.0));
    
    vec3 kS = vec3(spec) * (1.0 - roughness);
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    kD *= (1.0 - transmission); // Light passes THROUGH transmissive materials

    vec3 directLight = (kD * albedo / 3.14159 + kS) * 1.0 * NdotL * global.sunColor.rgb;
    vec3 emissive = albedo * emission;
    vec3 finalColor = directLight + hemiAmbient + emissive;

    // --- GLASS REFRACTION LOGIC ---
    float thickness     = max(ent.volumeParams.y, 1.0);
    float densitySlider = ent.volumeParams.z;
    float ior           = max(ent.volumeParams.w, 1.0);
    vec3  tintColor     = clamp(ent.volumeColor.rgb, vec3(0.001), vec3(0.999));

    float NdotV = abs(dot(N, V));
    float f0 = pow((1.0 - ior) / (1.0 + ior), 2.0);
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - NdotV, 5.0);

    vec3 transmissionColor = vec3(0.0);
    if (transmission > 0.0) {
        vec2 screenUV = gl_FragCoord.xy / vec2(global.params.z, global.params.w);
        vec2 distortion = (N.xy * (1.0 - (1.0 / ior))) * 0.1;
        
        float maxLod = 10.0;
        float blurLevel = roughness * maxLod;

        vec3 sceneRefr = textureLod(refractionTexture, screenUV + distortion, blurLevel).rgb;
        
        float density = densitySlider * 5.0; 
        vec3 absorption = -log(tintColor) * density;
        vec3 transmittance = exp(-absorption * thickness);
        
        transmissionColor = sceneRefr * transmittance; 
    }
    
    // --- DYNAMIC SKYBOX REFLECTION ---
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
    vec3 finalReflection = skyRefl * (kS + fresnel);

    // --- FINAL ATMOSPHERIC PASS ---
    float dist = length(global.cameraPos.xyz - fragPos);
    vec3 finalOutput;

    if (transmission > 0.0) {
        // Output Alpha 1.0 so we don't accidentally double-blend with the background
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