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
    // normalMatrix deleted
    vec4 camPos;
    vec4 pbrParams; // w = TIME
    vec4 sunDir;
    vec4 sunColor;
} PushConstants;

void main() {
    vec3 pos = inPos;
    float time = PushConstants.pbrParams.w; // Defined ONCE here

    // --- WAVE LOGIC ---
    float waveHeight = 0.5;
    float waveFreq = 0.5;
    float waveSpeed = 1.0;
    
    pos.z += sin(pos.x * waveFreq + time * waveSpeed) * waveHeight;
    pos.z += cos(pos.y * waveFreq + time * waveSpeed) * waveHeight * 0.5; 

    gl_Position = PushConstants.renderMatrix * vec4(pos, 1.0);
    
    // Normal Matrix
    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.renderMatrix)));
    
    fragNormal = normalize(normalMatrix * inNormal);
    
    // SCROLLING UVs
    float scrollSpeed = 0.01;
    fragUV = (inTexCoord * 4.0) + vec2(time * scrollSpeed, time * scrollSpeed);
    
    fragPos = vec3(PushConstants.renderMatrix * vec4(pos, 1.0));
    fragColor = vec3(0.0, 0.2, 0.8);
}