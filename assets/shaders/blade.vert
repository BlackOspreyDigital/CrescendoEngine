#version 450

// Standard 2D Quad Inputs
layout(location = 0) in vec3 inPosition; // Just a flat quad centered at 0,0
layout(location = 1) in vec2 inUV;       // 0.0 to 1.0

// Matches your 48-byte BladeRenderData exactly
struct BladeInstance {
    vec3 position;        // Offset 0
    float rotationY;      // Offset 12
    vec4 colorTint;       // Offset 16
    float scale;          // Offset 32
    float selectedFactor; // Offset 36
    int baseMaterialID;   // Offset 40
    int labelAtlasID;     // Offset 44
};

// SSBO holding your 10 blades
layout(std140, set = 0, binding = 0) readonly buffer InstanceBuffer {
    BladeInstance instances[];
};

// Camera / Viewport Projection
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} pc;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out float fragSelectedFactor;
layout(location = 3) flat out int fragLabelID;

void main() {
    BladeInstance inst = instances[gl_InstanceIndex];
    
    // 1. Calculate Y-Axis Rotation Matrix
    float c = cos(inst.rotationY);
    float s = sin(inst.rotationY);
    mat3 rotY = mat3(
        c,   0.0, s,
        0.0, 1.0, 0.0,
       -s,   0.0, c
    );
    
    // 2. Apply Scale -> Rotate -> Translate
    // This physically moves the flat quad through the 2.5D carousel space
    vec3 localPos = rotY * (inPosition * inst.scale);
    vec3 worldPos = localPos + inst.position;
    
    gl_Position = pc.viewProj * vec4(worldPos, 1.0);
    
    // Pass data down to the SDF fragment shader
    fragUV = inUV;
    fragTint = inst.colorTint;
    fragSelectedFactor = inst.selectedFactor;
    fragLabelID = inst.labelAtlasID;
}