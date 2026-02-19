#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragPos;
layout(location = 6) in flat int inEntityIndex;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormalRoughness; // [NEW] G-Buffer Output

layout(binding = 0) uniform sampler2D texSampler[100];
layout(binding = 1) uniform sampler2D skyTexture;

// --- SSBO ---
struct EntityData {
    vec4 pos;
    vec4 rot;
    vec4 scale;
    vec4 sphereBounds;
    vec4 albedoTint;
    vec4 pbrParams;    // x=Roughness, y=Metallic, z=Emission, w=NormalStrength
    vec4 volumeParams; // x=Transmission, y=Thickness, z=AttDist, w=IOR
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

layout(push_constant) uniform Constants {
    uint entityIndex;
} PushConsts;

vec2 SampleSphericalMap(vec3 v) {
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    uv *= vec2(0.1591, 0.3183); 
    uv += 0.5;
    return uv;
}

void main() {
    EntityData ent = entities[inEntityIndex];
    int texID = int(ent.albedoTint.w);
    float roughness = ent.pbrParams.x;
    float time = global.params.x;

    // --- Distortion ---
    vec2 distortion = vec2(sin(fragUV.y * 10.0 + time), cos(fragUV.x * 10.0 + time)) * 0.01;
    vec2 rippleUV = fragUV + distortion;

    vec4 textureColor = texture(texSampler[texID], rippleUV);

    // --- Reflection ---
    vec3 N = normalize(fragNormal);
    vec3 V = normalize(fragPos - global.cameraPos.xyz); // Use Global Camera
    vec3 R = reflect(V, N); 

    vec2 skyUV = SampleSphericalMap(normalize(R));
    vec3 reflection = texture(skyTexture, skyUV).rgb;

    vec3 finalColor = mix(textureColor.rgb, reflection, 0.5);
    outColor = vec4(finalColor, 0.8);
    outNormalRoughness = vec4(normalize(N), roughness);
}