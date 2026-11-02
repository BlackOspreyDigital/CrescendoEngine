#version 450

vec2 quadPositions[6] = vec2[](
    vec2(-0.5, -0.5), vec2( 0.5, -0.5), vec2(-0.5, 0.5),
    vec2(-0.5, 0.5), vec2( 0.5, -0.5), vec2( 0.5, 0.5)
);

// We keep the C++ struct the same, we just ignore the camera vectors now!
layout(push_constant) uniform PushConstants {
    vec3 worldPosition;
    float scale;
    vec3 cameraRight; 
    float pad1;
    vec3 cameraUp;    
    float pad2;
} pc;

// Perfectly matches Crescendo's C++ GlobalUniforms struct!
layout(set = 0, binding = 3) uniform GlobalUniformBuffer {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) out vec2 outUV;

void main() {
    vec2 localPos = quadPositions[gl_VertexIndex];

    mat4 rotView = mat4(mat3(ubo.view));

    // 1. Transform the center of the entity directly into View Space
    vec4 viewSpacePos = rotView * vec4(pc.worldPosition, 1.0);

    // 2. Add the billboard scale perfectly flat against the camera lens
    viewSpacePos.xy += localPos * pc.scale;

    // 3. Project it onto the screen
    vec4 clipPos = ubo.proj * viewSpacePos;

    gl_Position = clipPos.xyww;

    // 4. Flip the V coordinate to fix the upside-down Vulkan texture
    outUV = vec2(localPos.x + 0.5, 0.5 - localPos.y);
}