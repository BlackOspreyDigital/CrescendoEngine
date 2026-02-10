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

    // --- Wave Logic ---
    float waveHeight = 0.5;
    float waveFreq = 0.5;
    float waveSpeed = 2.0;
    
    float waveX = pos.x * waveFreq + time * waveSpeed;
    float waveY = pos.y * waveFreq + time * waveSpeed;

    pos.z += sin(waveX) * waveHeight;
    pos.z += cos(waveY) * waveHeight * 0.5; 

    // 1. Screen Position
    gl_Position = PushConstants.renderMatrix * vec4(pos, 1.0);

    // 2. World Position (Use Model Matrix!)
    fragPos = vec3(PushConstants.modelMatrix * vec4(pos, 1.0));

    // 3. Wave Normals (The "Slope" Fix)
    float dHdx = waveFreq * waveHeight * cos(waveX);
    float dHdy = waveFreq * waveHeight * 0.5 * -sin(waveY);
    vec3 localWaveNormal = normalize(vec3(-dHdx, -dHdy, 1.0));

    // Transform Normal to World Space (using Model Matrix)
    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.modelMatrix)));
    fragNormal = normalize(normalMatrix * localWaveNormal);
    
    fragUV = (inTexCoord * 4.0) + vec2(time * 0.1);
    fragColor = vec3(0.0, 0.2, 0.8);
}