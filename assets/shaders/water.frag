#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

// Binding 0: Texture Array (Normal Maps / Foam)
layout(binding = 0) uniform sampler2D texSampler[100];

// Binding 1: THE REAL HDR SKY
layout(binding = 1) uniform sampler2D skyTexture; 

layout(push_constant) uniform constants {
    mat4 renderMatrix;
    mat4 modelMatrix;
    vec4 camPos;
    vec4 pbrParams; // x=TexID, y=Roughness, z=Metallic, w=Time
    vec4 sunDir;
    vec4 sunColor;
    vec4 albedoTint; // w is used for Time
} PushConstants;

// Reuse the exact same mapping function from sky.frag
vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    uv *= vec2(0.1591, 0.3183); 
    uv += 0.5;
    return uv;
}

void main() {
    int texID = int(PushConstants.pbrParams.x); 
    
    // Get time from albedoTint.w
    float time = PushConstants.albedoTint.w; 

    // --- SLIDER CONTROLLED SETTINGS ---
    // Use the UI Roughness slider to control how "choppy" the waves are
    float roughness = clamp(PushConstants.pbrParams.y, 0.0, 1.0);
    
    float waveSpeed = 0.5;      
    float waveFrequency = 4.0; 
    float waveStrength = 0.05 * roughness; // Slider controls distortion strength!

    // --- RIPPLE CALCULATION (UV Distortion) ---
    vec2 distortion1 = vec2(
        sin(fragUV.y * waveFrequency + time * waveSpeed),
        cos(fragUV.x * waveFrequency + time * waveSpeed)
    );

    vec2 distortion2 = vec2(
        cos(fragUV.y * waveFrequency * 1.5 - time * waveSpeed * 0.8),
        sin(fragUV.x * waveFrequency * 1.5 - time * waveSpeed * 0.8)
    );

    vec2 rippleUV = fragUV + (distortion1 + distortion2) * waveStrength;

    // Sample the Normal Map (or Base Texture) using rippled UVs
    vec4 textureColor = texture(texSampler[texID], rippleUV);

    // --- CALCULATE REFLECTION ---
    // 1. Get Normal from geometry
    vec3 N = normalize(fragNormal);
    
    // 2. View Vector (Camera to Water)
    vec3 V = normalize(fragPos - PushConstants.camPos.xyz);
    
    // 3. Reflect V off N
    // We add the ripple distortion to the Normal to make reflections wiggle
    vec3 perturbedNormal = normalize(N + vec3(distortion1.x, distortion1.y, 0.0) * roughness);
    vec3 R = reflect(V, perturbedNormal); 

    // 4. Sample the REAL Sky Texture
    vec2 skyUV = SampleSphericalMap(normalize(R));
    vec3 reflection = texture(skyTexture, skyUV).rgb;

    // --- FRESNEL & MIXING ---
    // Water absorbs light, so we tint the base color blue-ish
    vec3 waterBase = textureColor.rgb * vec3(0.0, 0.2, 0.4); 
    
    // Fresnel: Water is reflective at angles, transparent looking down
    float fresnel = pow(1.0 - max(dot(normalize(-V), perturbedNormal), 0.0), 4.0);
    fresnel = clamp(fresnel + 0.1, 0.0, 1.0); // Base reflectivity

    // Final Mix: Base Water Color + Sky Reflection based on Fresnel
    vec3 finalColor = mix(waterBase, reflection, fresnel); 
    
    outColor = vec4(finalColor, 0.9); 
}