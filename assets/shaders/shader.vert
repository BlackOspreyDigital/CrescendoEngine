#version 450
// SSBO Implementation

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec3 inTangent;
layout(location = 5) in vec3 inBitangent;
// [NEW] Second UV Channel
layout(location = 6) in vec2 inTexCoord1; 

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;
layout(location = 6) out flat int outEntityIndex;
// [NEW] Pass UV1 to Fragment
layout(location = 7) out vec2 fragTexCoord1; 

// --- SSBO STRUCT ---
struct EntityData {
    mat4 modelMatrix;
    vec4 sphereBounds;
    vec4 albedoTint;
    vec4 pbrParams;
    vec4 volumeParams;
    vec4 volumeColor;
};

// --- BINDING 2: The Object Buffer ---
layout(std430, set = 0, binding = 2) readonly buffer ObjectBuffer { 
    EntityData entities[];
};

// --- GLOBAL UNIFORMS (Binding 3) ---
layout(set = 0, binding = 3) uniform GlobalUniforms {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 params; // x=time, y=dt, z=width, w=height
} global;

// --- TINY PUSH CONSTANT ---
layout(push_constant) uniform Constants {
    uint entityIndex;
} PushConsts;

void main() {
    // 1. Fetch Entity Data using the Index
    uint id = PushConsts.entityIndex;
    mat4 model = entities[id].modelMatrix;
    
    // 2. Standard Transform
    vec4 worldPos = model * vec4(inPosition, 1.0);
    gl_Position = global.viewProj * worldPos;

    // 3. Outputs
    fragPos = worldPos.xyz;
    fragTexCoord = inTexCoord;
    fragTexCoord1 = inTexCoord1; // [NEW] Pass through
    fragColor = inColor; 
    
    // 4. Normal Matrix (Inverse Transpose for non-uniform scaling)
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    fragNormal = normalize(normalMatrix * inNormal);
    fragTangent = normalize(normalMatrix * inTangent);
    fragBitangent = normalize(normalMatrix * inBitangent);

    // 5. Pass ID to Fragment Shader
    outEntityIndex = int(id);
}