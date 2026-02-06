#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler[100];

// FIX: Must match C++ (128 Bytes)
layout(push_constant) uniform constants {
    mat4 renderMatrix;
    // mat4 normalMatrix; <--- DELETE THIS LINE
    vec4 camPos;
    vec4 pbrParams; 
    vec4 sunDir;    
    vec4 sunColor;  
} PushConstants;

const float PI = 3.14159265359;

// --- SKY FUNCTION ---
vec3 GetSkyColor(vec3 viewDir) {
    vec3 topColor = vec3(0.2, 0.4, 0.8);     
    vec3 horizonColor = vec3(0.6, 0.7, 0.9); 
    vec3 groundColor = vec3(0.1, 0.1, 0.1);  
    
    vec3 sunDir = normalize(vec3(5.0, 1.0, 1.0)); 
    if (length(PushConstants.sunDir.xyz) > 0.1) {
        sunDir = normalize(PushConstants.sunDir.xyz);
    }
    
    float sunSize = 0.998; 
    float t = viewDir.z; 
    
    vec3 skyColor;
    if (t > 0.0) skyColor = mix(horizonColor, topColor, pow(t, 0.5)); 
    else skyColor = mix(horizonColor, groundColor, pow(abs(t), 0.5)); 

    float sunDot = dot(viewDir, sunDir);
    if (sunDot > sunSize) skyColor = vec3(1.0, 1.0, 0.8) * 2.0; 
    else skyColor += vec3(1.0, 0.8, 0.5) * pow(max(sunDot, 0.0), 100.0) * 0.5;
    
    return skyColor;
}

// ... PBR Functions ...
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness*roughness; float a2 = a*a; float NdotH = max(dot(N, H), 0.0); float NdotH2 = NdotH*NdotH;
    return a2 / (PI * ((NdotH2 * (a2 - 1.0) + 1.0) * (NdotH2 * (a2 - 1.0) + 1.0)));
}
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0); float k = (r*r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    int texID = int(PushConstants.pbrParams.x);
    float roughness = PushConstants.pbrParams.y;
    float metallic = PushConstants.pbrParams.z;
    
    vec4 albedoSample = texture(texSampler[texID], fragUV);
    if (albedoSample.a < 0.5) discard;
    vec3 albedo = albedoSample.rgb * fragColor; 

    vec3 N = normalize(fragNormal);
    vec3 V = normalize(PushConstants.camPos.xyz - fragPos);
    
    vec3 L = normalize(vec3(10.0, 20.0, 10.0));
    vec3 lightColor = vec3(1.0);
    if (PushConstants.sunDir.w > 0.0) {
        L = normalize(PushConstants.sunDir.xyz);
        lightColor = PushConstants.sunColor.rgb * PushConstants.sunDir.w;
    }

    vec3 H = normalize(V + L);
    vec3 F0 = vec3(0.04); 
    F0 = mix(F0, albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);   
    float G   = GeometrySmith(N, V, L, roughness);      
    vec3 F    = FresnelSchlick(max(dot(H, V), 0.0), F0);
        
    vec3 numerator = NDF * G * F; 
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular = numerator / denominator;
    
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic; 
      
    float NdotL = max(dot(N, L), 0.0);                
    vec3 Lo = (kD * albedo / PI + specular) * NdotL * lightColor; 

    vec3 R = reflect(-V, N); 
    vec3 skyReflection = GetSkyColor(R);
    
    vec3 ambient = vec3(0.03) * albedo;
    vec3 finalColor = ambient + Lo;
    
    finalColor = mix(finalColor, skyReflection, metallic * (1.0 - roughness));

    finalColor = finalColor / (finalColor + vec3(1.0)); 
    finalColor = pow(finalColor, vec3(1.0/2.2));  

    outColor = vec4(finalColor, albedoSample.a);
}