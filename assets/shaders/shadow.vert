#version 450

layout(location = 0) in vec3 inPosition;

// Access the SSBO to get model matrices
layout(set = 0, binding = 2) readonly buffer EntityData {
    mat4 modelMatrices[];
} entities;

// Push constants for this specific cascade pass
layout(push_constant) uniform PushConsts {
    mat4 lightVP;      // Offset 0
    uint entityIndex;  // Offset 64
} push;

void main() {
    mat4 model = entities.modelMatrices[push.entityIndex];
    gl_Position = push.lightVP * model * vec4(inPosition, 1.0);
}