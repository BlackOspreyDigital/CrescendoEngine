#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inColor; 
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec3 inTangent;
layout(location = 5) in vec3 inBitangent;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragUV;
layout(location = 3) out vec3 fragPos;
layout(location = 6) out flat int outEntityIndex;

// --- [UPDATED] SSBO STRUCT ---
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
    vec4 params; // x=time
} global;

// --- TINY PUSH CONSTANT ---
layout(push_constant) uniform Constants {
    uint entityIndex;
} PushConsts;

// --- [ADDED] HELPER: Build Matrix on GPU ---
mat4 buildMatrix(vec3 p, vec3 r, vec3 s) {
    float cx = cos(r.x); float sx = sin(r.x);
    float cy = cos(r.y); float sy = sin(r.y);
    float cz = cos(r.z); float sz = sin(r.z);

    mat3 rotMat;
    rotMat[0] = vec3(cy*cz, cy*sz, -sy);
    rotMat[1] = vec3(cz*sx*sy - cx*sz, cx*cz + sx*sy*sz, cy*sx);
    rotMat[2] = vec3(cx*cz*sy + sx*sz, -cz*sx + cx*sy*sz, cx*cy);

    mat4 m = mat4(1.0);
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
    
    float time = global.params.x;

    vec3 pos = inPos;

    // --- Wave Logic ---
    float waveHeight = 0.05; 
    float waveFreq = 0.5;    
    float waveSpeed = 0.5;

    float waveX = pos.x * waveFreq + time * waveSpeed;
    float waveY = pos.y * waveFreq + time * waveSpeed;

    pos.z += sin(waveX) * waveHeight;
    pos.z += cos(waveY) * waveHeight * 0.5;

    // --- Outputs ---
    gl_Position = global.viewProj * model * vec4(pos, 1.0);

    fragPos = vec3(model * vec4(pos, 1.0));
    
    // UV Scrolling
    float uvScrollSpeed = waveSpeed * 0.1;
    fragUV = (inTexCoord * 4.0) + vec2(time * uvScrollSpeed);

    // Calc Normals
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    fragNormal = normalize(normalMatrix * inNormal); 
    
    outEntityIndex = int(id);
}