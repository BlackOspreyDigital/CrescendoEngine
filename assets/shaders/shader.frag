#version 450

// --- INPUTS (Must match shader.vert) ---
layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord; // Match Vert (Vec2)
layout(location = 2) in vec3 fragNormal;   // Match Vert (Vec3)
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler[100];

// --- PUSH CONSTANTS ---
layout(push_constant) uniform constants {
    mat4 renderMatrix; 
    mat4 modelMatrix;  // Matches C++ padding
    vec4 camPos;
    vec4 pbrParams;    
    vec4 sunDir;    
    vec4 sunColor;
    vec4 albedoTint;
} PushConstants;

const float PI = 3.14159265359;

// --- PBR FUNCTIONS ---
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

vec3 GetSkyColor(vec3 viewDir) {
    vec3 topColor = vec3(0.2, 0.4, 0.8);
    vec3 horizonColor = vec3(0.6, 0.7, 0.9);
    vec3 groundColor = vec3(0.1, 0.1, 0.1);
    float t = viewDir.z;
    if (t > 0.0) return mix(horizonColor, topColor, pow(t, 0.5));
    return mix(horizonColor, groundColor, pow(abs(t), 0.5));
}

void main() {
    int texID = int(PushConstants.pbrParams.x);
    float roughness = PushConstants.pbrParams.y;
    float metallic = PushConstants.pbrParams.z;
    float emissionStrength = PushConstants.pbrParams.w;

    vec4 albedoSample = texture(texSampler[texID], fragTexCoord);
    if (albedoSample.a < 0.5) discard;

    vec3 albedo = albedoSample.rgb * PushConstants.albedoTint.rgb * fragColor;

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(PushConstants.camPos.xyz - fragPos);
    vec3 L = normalize(vec3(5.0, 10.0, 5.0)); 
    float sunIntensity = 1.0;
    if (length(PushConstants.sunDir.xyz) > 0.1) {
        L = normalize(PushConstants.sunDir.xyz);
        sunIntensity = PushConstants.sunDir.w;
    }

    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

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

    vec3 kS_IBL = FresnelSchlick(NdotV, F0);
    vec3 kD_IBL = 1.0 - kS_IBL;
    kD_IBL *= (1.0 - metallic);
    
    vec3 skyDiffuse = GetSkyColor(N) * 0.3;
    vec3 iblDiffuse = kD_IBL * skyDiffuse * albedo;
    
    vec3 emission = albedo * emissionStrength;

    outColor = vec4(directLight + iblDiffuse + emission, 1.0);
}