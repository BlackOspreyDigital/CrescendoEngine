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

struct EntityData {
    mat4 modelMatrix;
    vec4 sphereBounds;
    vec4 albedoTint;
    vec4 pbrParams;    // x=Roughness, y=Metallic, z=Emission, w=NormalStrength
    vec4 volumeParams; // x=Transmission, y=Thickness, z=AttDist, w=IOR
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
} global;

vec2 EquirectangularUV(vec3 v) {
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= vec2(0.1591, 0.3183); 
    uv += 0.5;
    return uv;
}

void main() {
    EntityData ent = entities[inEntityIndex];
    int texID = int(ent.albedoTint.w);
    vec3 baseColor = ent.albedoTint.rgb;
    
    // 1. Base Albedo
    vec4 albedoSample = texture(texSampler[texID], fragTexCoord);

    if (ent.volumeParams.x <= 0.0 && albedoSample.a < 0.5) {
        discard;
    }

    vec3 albedo = albedoSample.rgb * baseColor * fragColor;

    // 2. NORMAL MAPPING [RESTORED]
    vec3 N = normalize(fragNormal);
    float normalStr = ent.pbrParams.w; // The "Normal Strength" Slider
    
    // Only calculate TBN if we have a normal map strength > 0
    if (normalStr > 0.0) {
        // Assume Normal Map is always at texID + 1 (Your engine convention)
        int normalTexID = texID + 1;
        
        // Sample Normal Map
        vec3 mapNormal = texture(texSampler[normalTexID], fragTexCoord).rgb;
        
        // Transform from [0,1] to [-1,1]
        mapNormal = mapNormal * 2.0 - 1.0;
        
        // Apply Slider Strength (Scale X and Y, leave Z alone)
        mapNormal.xy *= normalStr;
        mapNormal = normalize(mapNormal);

        // Build TBN Matrix (Tangent Space -> World Space)
        vec3 T = normalize(fragTangent);
        vec3 B = normalize(fragBitangent);
        
        // Gram-Schmidt re-orthogonalization to ensure clean angles
        T = normalize(T - dot(T, N) * N);
        
        mat3 TBN = mat3(T, B, N);
        
        // Transform Normal
        N = normalize(TBN * mapNormal);
    }

    // 3. Lighting Vectors
    vec3 V = normalize(global.cameraPos.xyz - fragPos);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5));
    if (length(global.sunDirection.xyz) > 0.01) L = normalize(global.sunDirection.xyz);
    vec3 H = normalize(V + L);

    float roughness = ent.pbrParams.x;
    float metallic  = ent.pbrParams.y;
    float emission  = ent.pbrParams.z;
    
    // 4. PBR Lighting
    float NdotL = max(dot(N, L), 0.0);
    float shininess = (1.0 - roughness) * 64.0;
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, max(shininess, 1.0));

    vec3 kS = vec3(spec);
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    vec3 directLight = (kD * albedo / 3.14159 + kS) * 1.0 * NdotL * global.sunColor.rgb;
    vec3 ambient = albedo * 0.03;
    vec3 emissive = albedo * emission;

    vec3 finalColor = directLight + ambient + emissive;

    // 5. VOLUME / GLASS LOGIC (Maintained from previous fix)
    float transmission = ent.volumeParams.x;
    float thickness    = ent.volumeParams.y;
    float attDist      = ent.volumeParams.z; 
    vec3  attColor     = ent.volumeColor.rgb;
    float volumeOpacity = 0.0;

    if (transmission > 0.0) {
        vec3 safeAttColor = (length(attColor) < 0.01) ? vec3(1.0) : attColor;

        if (attDist > 0.001) {
            vec3 absorption = -log(max(safeAttColor, vec3(0.0001))) / attDist;
            vec3 transmittance = exp(-absorption * thickness);
            finalColor *= transmittance;
            float avgTrans = dot(transmittance, vec3(0.333));
            volumeOpacity = 1.0 - avgTrans;
        } else {
            finalColor *= safeAttColor;
            volumeOpacity = 1.0; 
        }
    }

    // 6. Skybox Reflection (Fresnel)
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    vec3 R = reflect(-V, N);
    vec3 skyRefl = texture(skyTexture, EquirectangularUV(normalize(R))).rgb;
    skyRefl *= (1.0 - roughness);
    vec3 finalReflection = skyRefl * (kS + fresnel) * transmission;

    // 7. Final Alpha
    float baseAlpha = 1.0 - transmission;
    float finalAlpha = baseAlpha + (volumeOpacity * transmission) + length(finalReflection);
    finalAlpha = clamp(finalAlpha, 0.0, 1.0);

    outColor = vec4(finalColor + finalReflection, finalAlpha);
}