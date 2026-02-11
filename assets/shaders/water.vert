#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor; 
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragPos;

layout(push_constant) uniform constants {
    mat4 renderMatrix; 
    mat4 modelMatrix;  // [MATCHING C++] Added to align with your new struct!
    vec4 camPos;
    vec4 pbrParams;    // w = Time
    vec4 sunDir;
    vec4 sunColor;
} PushConstants;

void main() {
    vec3 pos = inPos;
    float time = PushConstants.pbrParams.w;

    // --- 1. CALM WATER SETTINGS ---
    // Lower height significantly (e.g., 0.05 instead of 0.5) for "calm" water.
    // Lower speed (e.g., 0.5 instead of 2.0) for a gentle drift.
    float waveHeight = 0.05; 
    float waveFreq = 0.5;    
    float waveSpeed = 0.5;   
    
    // Calculate Wave Phases
    float waveX = pos.x * waveFreq + time * waveSpeed;
    float waveY = pos.y * waveFreq + time * waveSpeed;

    // Apply Displacement (Z-Up)
    pos.z += sin(waveX) * waveHeight;
    pos.z += cos(waveY) * waveHeight * 0.5; 

    // --- 2. MATCH UV SCROLLING ---
    // Instead of hardcoding 0.1, we define a ratio relative to waveSpeed.
    // "0.1" here is the "drag" factor. Water textures usually move slower than the physical wave.
    float uvScrollSpeed = waveSpeed * 0.1; 
    
    // Apply the scrolling. 
    // We add 'uvScrollSpeed' to the time calculation.
    fragUV = (inTexCoord * 4.0) + vec2(time * uvScrollSpeed);

    // --- 3. OUTPUTS ---
    gl_Position = PushConstants.renderMatrix * vec4(pos, 1.0);
    fragPos = vec3(PushConstants.modelMatrix * vec4(pos, 1.0));

    // Recalculate Normals for the new calmer waves
    float dHdx = waveFreq * waveHeight * cos(waveX);
    float dHdy = waveFreq * waveHeight * 0.5 * -sin(waveY);
    vec3 localWaveNormal = normalize(vec3(-dHdx, -dHdy, 1.0));

    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.modelMatrix)));
    fragNormal = normalize(normalMatrix * localWaveNormal);
    
    fragColor = vec3(0.0, 0.2, 0.8);
}