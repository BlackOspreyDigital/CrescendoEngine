#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler[100];

// [FIX] ALIGNMENT MUST MATCH C++
layout(push_constant) uniform constants {
    mat4 renderMatrix;
    mat4 modelMatrix;
    vec4 camPos;
    vec4 pbrParams;
    vec4 sunDir;
    vec4 sunColor;
    float time; // <--- Add this! (Total seconds since game start)
} PushConstants;

// --- SKY COLOR LOGIC (Restored) ---
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

void main() {
    int texID = int(PushConstants.pbrParams.x); 
    float time = PushConstants.time;

    // --- CALM WATER SETTINGS ---
    float waveSpeed = 0.5;      // Lower = Slower, calmer
    float waveFrequency = 4.0;  // Lower = Larger, wider waves
    float waveStrength = 0.02;  // Lower = Subtler distortion (very important for "calm")

    // --- RIPPLE CALCULATION ---
    // We distort the UVs using Sine/Cosine based on time.
    // Wave 1: Moves diagonally
    vec2 distortion1 = vec2(
        sin(fragUV.y * waveFrequency + time * waveSpeed),
        cos(fragUV.x * waveFrequency + time * waveSpeed)
    );

    // Wave 2: Moves the opposite way to create interference (natural look)
    vec2 distortion2 = vec2(
        cos(fragUV.y * waveFrequency * 1.5 - time * waveSpeed * 0.8),
        sin(fragUV.x * waveFrequency * 1.5 - time * waveSpeed * 0.8)
    );

    // Combine them and scale down by strength
    vec2 rippleUV = fragUV + (distortion1 + distortion2) * waveStrength;

    // Sample texture with the new Rippled UVs
    vec4 textureColor = texture(texSampler[texID], rippleUV);

    // ... (rest of your lighting/reflection code) ...

    vec3 N = normalize(fragNormal);
    // V calculation now works because fragPos is in World Space
    vec3 V = normalize(PushConstants.camPos.xyz - fragPos);
    vec3 R = reflect(-V, N); 
    vec3 reflection = GetSkyColor(R);

    // --- WATER COLOR MIXING (Restored) ---
    vec3 waterBase = textureColor.rgb * vec3(0.5, 0.5, 1.0); 
    
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    fresnel = clamp(fresnel + 0.2, 0.0, 1.0);

    vec3 finalColor = mix(waterBase, reflection, fresnel * 0.6); 
    
    outColor = vec4(finalColor, 0.85); 
}