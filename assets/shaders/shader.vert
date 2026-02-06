#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inUV;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragPos;

// FIX: Struct now matches C++ and Fragment Shader exactly
layout(push_constant) uniform constants {
    mat4 renderMatrix;
    vec4 camPos;
    vec4 pbrParams;
    vec4 sunDir;
    vec4 sunColor;
} PushConstants; 

void main() {
    gl_Position = PushConstants.renderMatrix * vec4(inPos, 1.0);

    // Calculate Normal Matrix manually
    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.renderMatrix)));

    fragColor = inColor;
    fragNormal = normalize(normalMatrix * inNormal);
    fragUV = inUV;
    fragPos = vec3(PushConstants.renderMatrix * vec4(inPos, 1.0)); 
}