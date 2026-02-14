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

// --- SSBO ---
struct EntityData {
    mat4 modelMatrix;
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

void main() {
    uint id = PushConsts.entityIndex;
    mat4 model = entities[id].modelMatrix; // Fetch Matrix
    float time = global.params.x;          // Fetch Time

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
    fragNormal = normalize(normalMatrix * inNormal); // Simplified normal for now
    
    outEntityIndex = int(id);
}