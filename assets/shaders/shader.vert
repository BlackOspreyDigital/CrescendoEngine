#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;
layout(location = 4) in vec3 inTangent;
layout(location = 5) in vec3 inBitangent;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord; 
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;

layout(push_constant) uniform constants {
    mat4 renderMatrix; // MVP
    mat4 modelMatrix;  // World Space
    vec4 camPos;
    vec4 pbrParams;
    vec4 sunDir;
    vec4 sunColor;
    vec4 albedoTint;
} PushConstants;

void main() {
    // 1. Screen Position (MVP)
    gl_Position = PushConstants.renderMatrix * vec4(inPos, 1.0);
    fragPos = vec3(PushConstants.modelMatrix * vec4(inPos, 1.0));

    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.modelMatrix)));

    fragNormal    = normalize(normalMatrix * inNormal);

    fragTangent   = normalize(normalMatrix * inTangent);   
    fragBitangent = normalize(normalMatrix * inBitangent); 

    fragColor = inColor;
    fragTexCoord = inUV; 
}