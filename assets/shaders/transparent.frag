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

    // 2. NORMAL MAPPING
    vec3 N = normalize(fragNormal);
    float normalStr = ent.pbrParams.w;
    int normalTexID = int(ent.volumeColor.a);
    
    // Only calculate TBN if we have a normal map strength > 0
    if (normalStr > 0.0) {
        // Sample Normal Map
        vec3 mapNormal = texture(texSampler[normalTexID], fragTexCoord).rgb;
        // Transform from [0,1] to [-1,1]
        mapNormal = mapNormal * 2.0 - 1.0;
        // Apply Slider Strength
        mapNormal.xy *= normalStr;
        
        // [FIX] Mathematical safety guard to prevent NaNs/fragmenting!
        // Only process the normal if the vector has an actual length.
        if (length(mapNormal) > 0.001) {
            mapNormal = normalize(mapNormal);

            // Build TBN Matrix (Tangent Space -> World Space)
            vec3 T = normalize(fragTangent);
            vec3 B = normalize(fragBitangent);
            
            // Gram-Schmidt re-orthogonalization
            T = normalize(T - dot(T, N) * N);
            mat3 TBN = mat3(T, B, N);
            
            // Transform Normal
            N = normalize(TBN * mapNormal);
        }
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
    // [FIX] Move transmission up here so the lighting math can see it!
    float transmission = ent.volumeParams.x; 

    float NdotL = max(dot(N, L), 0.0);
    float shininess = (1.0 - roughness) * (1.0 - roughness) * 128.0; 
    
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, max(shininess, 1.0));
    
    vec3 kS = vec3(spec) * (1.0 - roughness);
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    
    // Light passes THROUGH transmissive materials, it doesn't bounce off!
    kD *= (1.0 - transmission); 

    vec3 directLight = (kD * albedo / 3.14159 + kS) * 1.0 * NdotL * global.sunColor.rgb;

    // Calculate our ambient and emissive lighting using the already-declared emission variable
    vec3 ambient = albedo * 0.03; 
    vec3 emissive = albedo * emission;

    vec3 finalColor = directLight + ambient + emissive;

    // 5. VOLUME / GLASS LOGIC
    float thickness     = max(ent.volumeParams.y, 1.0); // Ensure thickness is never 0
    float densitySlider = ent.volumeParams.z;
    float ior           = max(ent.volumeParams.w, 1.0);
    // Safe tint to prevent black/white math explosions
    vec3  tintColor     = clamp(ent.volumeColor.rgb, vec3(0.001), vec3(0.999)); 
    float volumeOpacity = 0.0;

    // Two-sided normals for glass: prevents backfaces from glitching out
    float NdotV = abs(dot(N, V));

    // Calculate real Fresnel based on your IOR slider
    float f0 = pow((1.0 - ior) / (1.0 + ior), 2.0);
    float fresnel = f0 + (1.0 - f0) * pow(1.0 - NdotV, 5.0);

    vec3 transmissionColor = vec3(0.0);

    if (transmission > 0.0) {
        // Calculate where this pixel is on the screen
        vec2 screenUV = gl_FragCoord.xy / vec2(global.params.z, global.params.w);

        // Offset UVs based on normal and IOR
        vec2 distortion = (N.xy * (1.0 - (1.0 / ior))) * 0.1;

        // Calculate blur level based on roughness
        float maxLod = 10.0; 
        float blurLevel = roughness * maxLod;

        // Sample the blurred background
        vec3 sceneRefr = textureLod(refractionTexture, screenUV + distortion, blurLevel).rgb;

        // [FIX] Artist-Friendly Density Math!
        // We MULTIPLY by density instead of dividing by distance.
        // 0.0 = Perfectly Clear, 1.0 = Highly Colored.
        float density = densitySlider * 5.0; // You can raise/lower this 5.0 if it gets dark too fast!
        
        vec3 absorption = -log(tintColor) * density;
        vec3 transmittance = exp(-absorption * thickness);
        
        // Apply to background color
        transmissionColor = sceneRefr * transmittance; 
        
        // Calculate resulting opacity
        float avgTrans = dot(transmittance, vec3(0.333));
        volumeOpacity = 1.0 - avgTrans;
    }
    
    // 6. Skybox Reflection 
    vec3 R = reflect(-V, N);
    vec3 skyRefl = texture(skyTexture, EquirectangularUV(normalize(R))).rgb;
    skyRefl *= (1.0 - roughness);
    
    // Apply our true IOR Fresnel to the reflection
    vec3 finalReflection = skyRefl * (kS + fresnel);

    // 7. Final Alpha
    // [FIX] Stop hardware double-blending! 
    // Because we manually composited the background via refraction, we MUST output a fully opaque pixel.
    float finalAlpha = mix(albedoSample.a, 1.0, transmission);

    // Add the refracted interior color to our final output
    finalColor += (transmissionColor * transmission);

    outColor = vec4(finalColor + finalReflection, finalAlpha);
}