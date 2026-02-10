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
    mat4 modelMatrix;  // [WAS MISSING]
    vec4 camPos;
    vec4 pbrParams;
    vec4 sunDir;
    vec4 sunColor;
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
    vec4 textureColor = texture(texSampler[texID], fragUV);

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