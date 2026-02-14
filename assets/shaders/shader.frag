#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;
layout(location = 6) in flat int inEntityIndex;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler[100];
layout(binding = 1) uniform sampler2D skyTexture;

// --- SSBO (Material Data) ---
struct EntityData {
    mat4 modelMatrix;
    vec4 sphereBounds;
    vec4 albedoTint;
    vec4 pbrParams;
    vec4 volumeParams;
    vec4 volumeColor;
};
layout(std430, set = 0, binding = 2) readonly buffer ObjectBuffer { 
    EntityData entities[];
};

// --- GLOBAL UNIFORMS (Binding 3) ---
layout(set = 0, binding = 3) uniform GlobalUniforms {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 params; // x=time
} global;

// --- TINY PUSH CONSTANT ---
layout(push_constant) uniform Constants {
    uint entityIndex;
} PushConsts;

void main() {
    // 1. Fetch Material (Same as before)
    EntityData ent = entities[inEntityIndex];
    int texID = int(ent.albedoTint.w);
    vec3 baseColor = ent.albedoTint.rgb;
    
    // 2. Texture Sampling (Same as before)
    vec4 albedoSample = texture(texSampler[texID], fragTexCoord);
    vec3 albedo = albedoSample.rgb * baseColor * fragColor;

    // 3. Normal Mapping (Same as before)
    vec3 N = normalize(fragNormal);
    float normalStr = ent.pbrParams.w;
    if (normalStr > 0.0) {
        // ... (Normal map code) ...
    }

    // 4. Lighting & PBR
    vec3 V = normalize(global.cameraPos.xyz - fragPos);
    vec3 L = normalize(vec3(0.5, 1.0, 0.5));
    float sunIntensity = 1.0;
    
    if (length(global.sunDirection.xyz) > 0.01) {
        L = normalize(global.sunDirection.xyz);
        sunIntensity = global.sunDirection.w;
    }
    vec3 H = normalize(V + L); // Half vector for specular

    // [FIX] READ PBR PARAMS FROM SSBO
    // pbrParams: x=Roughness, y=Metallic, z=Emission
    float roughness = ent.pbrParams.x;
    float metallic  = ent.pbrParams.y;
    float emission  = ent.pbrParams.z;

    // Diffuse Calculation
    float NdotL = max(dot(N, L), 0.0);
    
    // Specular Calculation (Blinn-Phong approximation for now)
    // Higher roughness = Lower shininess
    float shininess = (1.0 - roughness) * 64.0; 
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, max(shininess, 1.0));
    
    // Simple Metallic logic: 
    // Metals have dark diffuse and colored specular. 
    // Non-metals have colored diffuse and white specular.
    vec3 kS = vec3(spec); // Specular intensity
    vec3 kD = vec3(1.0) - kS; // Conservation of energy
    kD *= 1.0 - metallic; // Metals have no diffuse

    // Combine Light
    vec3 directLight = (kD * albedo / 3.14159 + kS) * sunIntensity * NdotL * global.sunColor.rgb;
    vec3 ambient = albedo * 0.03;
    vec3 emissive = albedo * emission; // Use albedo as emission color for now

    // Final Color
    // [FIX] Glass Logic from previous step
    float transmission = ent.volumeParams.x; 
    float alpha = 1.0 - transmission;
    
    outColor = vec4(directLight + ambient + emissive, alpha);
}