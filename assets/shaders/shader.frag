#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler[100];

// Match C++ MeshPushConstants (Exactly 128 Bytes)
layout(push_constant) uniform constants {
    mat4 renderMatrix;
    vec4 camPos;
    vec4 pbrParams; // x:texID, y:roughness, z:metallic, w:emission
    vec4 sunDir;    // xyz:dir, w:intensity
    vec4 sunColor;
    vec4 albedoTint;// [NEW]
} PushConstants;

const float PI = 3.14159265359;

// --- IBL: Procedural Sky Sampling ---
vec3 GetSkyColor(vec3 viewDir) {
    vec3 topColor = vec3(0.2, 0.4, 0.8);
    vec3 horizonColor = vec3(0.6, 0.7, 0.9);
    vec3 groundColor = vec3(0.1, 0.1, 0.1);
    float t = viewDir.z;
    if (t > 0.0) return mix(horizonColor, topColor, pow(t, 0.5));
    return mix(horizonColor, groundColor, pow(abs(t), 0.5));
}

// --- PBR MATH ---
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}

vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    // 1. Unpack Material Data
    int texID = int(PushConstants.pbrParams.x);
    float roughness = clamp(PushConstants.pbrParams.y, 0.05, 1.0);
    float metallic = PushConstants.pbrParams.z;
    float emission = PushConstants.pbrParams.w;

    vec4 albedoSample = texture(texSampler[texID], fragTexCoord);
    if (albedoSample.a < 0.5) discard;

    // [FIX] Apply the Albedo Tint from the Inspector here
    // Texture * Vertex Color * Inspector Color
    vec3 albedo = albedoSample.rgb * fragColor * PushConstants.albedoTint.rgb;

    // 2. Vectors (World Space)
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(PushConstants.camPos.xyz - fragPos);
    vec3 R = reflect(-V, N);
    
    // F0 (Surface reflectivity at grazing angle)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // 3. Direct Lighting (Cook-Torrance BRDF)
    vec3 L = normalize(PushConstants.sunDir.xyz);
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    vec3 specular = (NDF * G * F) / (4.0 * NdotV * NdotL + 0.0001);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    
    // Direct Light uses the tinted albedo
    vec3 directLight = (kD * albedo / PI + specular) * (PushConstants.sunColor.rgb * PushConstants.sunDir.w) * NdotL;

    // 4. Ambient & IBL (Procedural Environment)
    vec3 skyReflection = GetSkyColor(R);
    vec3 skyDiffuse = GetSkyColor(N) * 0.1; 
    
    vec3 kS_IBL = FresnelSchlick(NdotV, F0);
    vec3 iblSpecular = skyReflection * kS_IBL * (1.0 - roughness);
    
    // Ambient Diffuse also uses the tinted albedo
    vec3 iblDiffuse = skyDiffuse * albedo * (1.0 - metallic);
    
    vec3 ambient = (iblDiffuse + iblSpecular);

    // 5. Emission & Composite
    // Emission is now scaled by the tinted albedo for consistency
    vec3 emissionTerm = albedo * emission;
    vec3 finalColor = directLight + ambient + emissionTerm;

    // 6. HDR Tone Mapping & Gamma Correction
    finalColor = finalColor / (finalColor + vec3(1.0));
    finalColor = pow(finalColor, vec3(1.0/2.2));

    outColor = vec4(finalColor, albedoSample.a);
}