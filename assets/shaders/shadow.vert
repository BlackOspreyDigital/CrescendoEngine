#version 450

layout(location = 0) in vec3 inPos;

struct EntityData {
    mat4 model;
    vec4 sphereBounds;
    vec4 albedoTint;
    vec4 pbrParams;
    vec4 volumeParams;
    vec4 volumeColor;
};

// SSBO Set 0 Binding 2
layout(std140, set = 0, binding = 2) readonly buffer ObjectBuffer {
    EntityData objects[];
} objectBuffer;

layout(push_constant) uniform ShadowPushConsts {
    mat4 lightVP;
    uint entityIndex;
} push;

void main() {
    mat4 model = objectBuffer.objects[push.entityIndex].model;
    gl_Position = push.lightVP * model * vec4(inPos, 1.0);
}