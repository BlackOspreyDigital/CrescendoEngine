#version 450

// Engine Standard Vertex Inputs
layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inUV;

// Outputs to Fragment Shader
layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragTint;
layout(location = 2) out float fragSelectedFactor;
layout(location = 3) flat out int fragLabelID;
layout(location = 4) out vec4 fragIconUVBounds; // <--- This satisfies the Validation Layer!

// Must match C++ BladeRenderData exactly (std140 padding rules)
struct BladeRenderData {
    vec3 position;        // Offset 0
    float rotationY;      // Offset 12
    vec4 colorTint;       // Offset 16
    float scale;          // Offset 32
    float selectedFactor; // Offset 36
    int baseMaterialID;   // Offset 40
    float padding;        // Offset 44
    vec4 iconUVBounds;    // Offset 48
};

// Binding 0 is our SSBO
layout(std140, binding = 0) readonly buffer InstanceBuffer {
    BladeRenderData blades[];
} inst;

// Our Static UI Camera
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
} push;

void main() {
    BladeRenderData data = inst.blades[gl_InstanceIndex];

    // 1. Pass data down to the Fragment Shader
    fragUV = inUV;
    fragTint = data.colorTint;
    fragSelectedFactor = data.selectedFactor;
    fragLabelID = gl_InstanceIndex;
    fragIconUVBounds = data.iconUVBounds; // Pass the Python-generated UVs!

    // 2. Base Size of a Blade (Widescreen 16:9 ratio!)
    // 420x240 gives us an 840x480 pixel card. 
    vec3 scaledPos = inPosition * vec3(420.0, 240.0, 1.0); 

    // 3. Apply scale and rotation (Kept flat for the 360 look)
    mat4 scaleMat = mat4(data.scale);
    scaleMat[3][3] = 1.0;
    
    float c = cos(data.rotationY);
    float s = sin(data.rotationY);
    mat4 rotY = mat4(
         c, 0, s, 0,
         0, 1, 0, 0,
        -s, 0, c, 0,
         0, 0, 0, 1
    );

    // 4. Calculate Final Screen Position
    vec4 worldPos = vec4(data.position, 1.0) + (rotY * scaleMat * vec4(scaledPos, 1.0));
    gl_Position = push.viewProj * worldPos;
}