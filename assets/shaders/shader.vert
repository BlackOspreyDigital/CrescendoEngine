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

// --- UPDATED SSBO STRUCT ---
struct EntityData {
    vec4 pos;
    vec4 rot;
    vec4 scale;
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

// --- HELPER: Build Matrix on GPU ---
mat4 buildMatrix(vec3 p, vec3 r, vec3 s) {
    // Precompute Trig for rotation (Euler angles in radians)
    float cx = cos(r.x); float sx = sin(r.x);
    float cy = cos(r.y); float sy = sin(r.y);
    float cz = cos(r.z); float sz = sin(r.z);

    // Rotation Matrix (Z * Y * X order to match GLM)
    mat3 rotMat;
    rotMat[0] = vec3(cy*cz, cy*sz, -sy);
    rotMat[1] = vec3(cz*sx*sy - cx*sz, cx*cz + sx*sy*sz, cy*sx);
    rotMat[2] = vec3(cx*cz*sy + sx*sz, -cz*sx + cx*sy*sz, cx*cy);

    // Combine Scale -> Rotation -> Translation
    mat4 m = mat4(1.0);
    
    // Columns (Vectors)
    m[0] = vec4(rotMat[0] * s.x, 0.0);
    m[1] = vec4(rotMat[1] * s.y, 0.0);
    m[2] = vec4(rotMat[2] * s.z, 0.0);
    m[3] = vec4(p, 1.0);

    return m;
}

void main() {
    uint id = PushConsts.entityIndex;
    
    // 1. Fetch raw pos/rot/scale from SSBO
    vec3 p = entities[id].pos.xyz;
    vec3 r = entities[id].rot.xyz;
    vec3 s = entities[id].scale.xyz;

    // 2. Build Matrix on the fly
    mat4 model = buildMatrix(p, r, s);
    
    // 3. Standard Transform
    vec4 worldPos = model * vec4(inPosition, 1.0);
    gl_Position = global.viewProj * worldPos;

    // 4. Outputs
    fragPos = worldPos.xyz;
    fragTexCoord = inTexCoord;
    fragTexCoord1 = inTexCoord1;
    fragColor = inColor; 
    
    // 5. Normal Matrix (Inverse Transpose for non-uniform scaling)
    mat3 normalMatrix = transpose(inverse(mat3(model)));

    // 6. Standard Lighting Vectors
    fragNormal = normalize(normalMatrix * inNormal);
    fragTangent = normalize(normalMatrix * inTangent);
    fragBitangent = normalize(normalMatrix * inBitangent);

    // 7. Pass ID to Fragment Shader
    outEntityIndex = int(id);
}