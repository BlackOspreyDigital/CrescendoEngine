#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(location = 0) out vec3 outWorldPos;

layout(push_constant) uniform AtmoPush {
    mat4 vp;                                   
    vec3 sunDirection; float planetRadius;     
    vec3 planetCenter; float atmosphereRadius; 
    vec3 cameraPos;    float sunIntensity;     
    vec3 rayleighCoeff; float mieCoeff;        
} push;

void main() {
    // THE FIX: Normalize the baked mesh back to a perfect 1.0 sphere, 
    // then multiply it by the dynamic UI slider! 
    vec3 dynamicPos = normalize(inPosition) * push.atmosphereRadius;
    
    // Add the planet center so it perfectly follows the voxels!
    outWorldPos = push.planetCenter + dynamicPos;
    
    gl_Position = push.vp * vec4(outWorldPos, 1.0);
}