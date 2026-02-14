#version 450

// --- INPUTS ---
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;

layout(location = 0) out vec4 outColor;

// --- UNIFORMS ---
layout(binding = 0) uniform sampler2D texSampler[100];
layout(binding = 1) uniform sampler2D skyTexture; // Needed for glass refraction

// --- PUSH CONSTANTS ---
layout(push_constant) uniform constants {
    mat4 renderMatrix;
    mat4 modelMatrix;
    vec4 camPos;    // .w = Transmission Factor (0.0 = Opaque, 1.0 = Glass)
    vec4 pbrParams; // x=TexID, y=Roughness, z=Metallic, w=Emission
    vec4 sunDir;    // .w = Sun Intensity
    vec4 sunColor;  // .w = Normal Strength (Slider)
    vec4 albedoTint;// If Transmission > 0: .rgb = Attenuation Color, .w = Attenuation Distance
} PushConstants;

const float PI = 3.14159265359;

// --- HELPER FUNCTIONS ---
vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

vec3 GetSkyColor(vec3 dir) {
    // Try to sample the sky texture, fall back to procedural if needed
    return texture(skyTexture, SampleSphericalMap(normalize(dir))).rgb;
}

// --- PBR MATH ---
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

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // 1. Unpack Parameters
    int texID = int(PushConstants.pbrParams.x);
    float roughness = PushConstants.pbrParams.y;
    float metallic = PushConstants.pbrParams.z;
    float emissionStrength = PushConstants.pbrParams.w;
    float transmission = PushConstants.camPos.w;
    float normalStrength = PushConstants.sunColor.w;

    // 2. Base Color / Albedo
    vec4 albedoSample = texture(texSampler[texID], fragTexCoord);
    
    if (transmission <= 0.0 && albedoSample.a < 0.5) discard;

    vec3 albedo;
    vec3 attColor = vec3(1.0);
    float attDist = 1.0;

    if (transmission > 0.0) {
        // GLASS MODE
        albedo = albedoSample.rgb * fragColor; 
        attColor = PushConstants.albedoTint.rgb; 
        attDist = PushConstants.albedoTint.w;    
    } else {
        // OPAQUE MODE
        albedo = albedoSample.rgb * PushConstants.albedoTint.rgb * fragColor;
    }

    // 3. Normal Mapping
    // [FIX] Initialize Geometry Normal FIRST so it's valid even if we skip the if-block
    vec3 N = normalize(fragNormal);

    if (normalStrength > 0.0) {
        vec3 T = normalize(fragTangent);
        vec3 B = normalize(fragBitangent);
        mat3 TBN = mat3(T, B, N);

        int normalTexID = texID + 1; // [FIX] Typo was 'textID'
        
        // Sample Normal Map
        vec3 normalMap = texture(texSampler[normalTexID], fragTexCoord).rgb;
        
        // Transform [0,1] -> [-1,1]
        normalMap = normalMap * 2.0 - 1.0;     
        normalMap.xy *= normalStrength;        
        normalMap = normalize(normalMap);
        
        // Update N
        N = normalize(TBN * normalMap);
    }

    // [FIX] DELETED ZOMBIE CODE HERE (The duplicate block causing 'undeclared identifier' errors)

    // 4. Lighting Vectors
    vec3 V = normalize(PushConstants.camPos.xyz - fragPos);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5)); 
    float sunIntensity = 1.0;
    
    if (length(PushConstants.sunDir.xyz) > 0.1) {
        L = normalize(PushConstants.sunDir.xyz);
        sunIntensity = PushConstants.sunDir.w;
    }

    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // 5. PBR Lighting (Specular + Diffuse)
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);
        
    vec3 numerator = NDF * G * F; 
    float denominator = 4.0 * NdotV * NdotL + 0.0001;
    vec3 specular = numerator / denominator;
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= (1.0 - metallic);
    
    vec3 directLight = (kD * albedo / PI + specular) * (PushConstants.sunColor.rgb * sunIntensity) * NdotL;

    // 6. IBL / Ambient
    vec3 skyDiffuse = GetSkyColor(N) * 0.3;
    vec3 iblDiffuse = kD * skyDiffuse * albedo;
    
    vec3 finalColor = directLight + iblDiffuse + (albedo * emissionStrength);

    // 7. VOLUME / TRANSMISSION LOGIC (Glass)
    if (transmission > 0.0) {
        float thickness = 0.5 / (abs(dot(N, V)) + 0.1); 

        vec3 absorption = -log(max(attColor, vec3(0.001))) / max(attDist, 0.001);
        vec3 transmittance = exp(-absorption * thickness);

        // Refraction
        float IOR = 1.5; 
        float eta = 1.0 / IOR; 
        vec3 R = reflect(-V, N); 
        vec3 T_refract = refract(-V, N, eta); 
        if (length(T_refract) < 0.01) T_refract = R;

        vec3 skyReflection = GetSkyColor(R);
        vec3 skyRefraction = GetSkyColor(T_refract);
        
        vec3 glassBodyColor = skyRefraction * transmittance * albedo;
        vec3 glassFinalRGB = mix(glassBodyColor, skyReflection, F);
        
        float opacity = clamp(1.0 - ((transmittance.r + transmittance.g + transmittance.b) / 3.0), 0.0, 1.0);
        float alpha = clamp(F.g + opacity, 0.05, 1.0);

        outColor = vec4(glassFinalRGB, alpha);
    } else {
        outColor = vec4(finalColor, 1.0);
    }
}