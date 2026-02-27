#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;
layout(location = 3) in vec3 inColor;      // <--- Added to match your C++ Vertex struct!
layout(location = 4) in vec3 inTangent;
layout(location = 5) in vec3 inBitangent;
layout(location = 6) in vec2 inLightmapUV; // <--- Shifted down to location 6

layout(push_constant) uniform PushConstants {
    mat4 modelMatrix;
} push;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;

void main() {
    // 1. Calculate world space position
    vec4 worldPos = push.modelMatrix * vec4(inPosition, 1.0);
    fragWorldPos = worldPos.xyz;

    // 2. Calculate world space normal
    mat3 normalMatrix = transpose(inverse(mat3(push.modelMatrix)));
    fragNormal = normalize(normalMatrix * inNormal);

    // 3. Flatten the model to the screen using the lightmap UV
    vec2 screenPos = inLightmapUV * 2.0 - 1.0;
    gl_Position = vec4(screenPos, 0.0, 1.0);
}