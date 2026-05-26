#version 450

layout(location = 0) in vec3 inPosition;

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

layout(std430, set = 0, binding = 2) readonly buffer ObjectBuffer { 
    EntityData entities[];
};

layout(push_constant) uniform Constants {
    mat4 lightVP;
    uint entityIndex;
} PushConsts;

// --- HELPER: Build Matrix on GPU ---
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

    // 3. Output to shadow map
    gl_Position = PushConsts.lightVP * model * vec4(inPosition, 1.0);
}