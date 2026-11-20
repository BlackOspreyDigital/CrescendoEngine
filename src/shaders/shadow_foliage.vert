#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 3) in vec2 inTexCoord;
layout(location = 6) in vec2 inTexCoord1;

layout(location = 0) out vec2 fragTexCoord;

struct FoliageGPUInstance {
    vec4 posAndYaw;
    vec4 scaleAndPhase;
    vec4 tintAndParams;
};

layout(std430, set = 0, binding = 10) readonly buffer FoliageBuffer {
    FoliageGPUInstance foliageInstances[];
};

struct PointLight {
    vec4 positionAndRadius;
    vec4 colorAndIntensity;
};

layout(set = 0, binding = 3) uniform GlobalUniforms {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 params;
    vec4 fogColor;
    vec4 fogParams;
    vec4 skyColor;    // .w = elapsed time (seconds)
    vec4 groundColor;
    vec4 pointLightParams;
    PointLight pointLights[64];
} global;

layout(push_constant) uniform ShadowFoliagePush {
    mat4 lightSpaceMatrix;
    uint firstInstance;
    int texID;
    float alphaCutoff;
    float windStrength;
    float windFrequency;
    float _pad0;
    float _pad1;
    float _pad2;
} push;

void main() {
    FoliageGPUInstance inst = foliageInstances[push.firstInstance + uint(gl_InstanceIndex)];

    vec3 s = inst.scaleAndPhase.xyz;
    float yaw = inst.posAndYaw.w;
    float cy = cos(yaw);
    float sy = sin(yaw);

    vec3 scaled = inPosition * s;
    vec3 rotated = vec3(
        scaled.x * cy - scaled.y * sy,
        scaled.x * sy + scaled.y * cy,
        scaled.z
    );

    float windWeight = (inTexCoord1.x > 0.001) ? clamp(inTexCoord1.x, 0.0, 1.0) : clamp(inPosition.z * 0.45, 0.0, 1.0);
    float t = global.skyColor.w * push.windFrequency + inst.scaleAndPhase.w;
    float gust = sin(t + rotated.x * 0.55 + rotated.y * 0.25) * 0.72 + cos(t * 1.73 + rotated.y * 0.65) * 0.28;
    float flutter = sin(t * 3.1 + inPosition.x * 3.7 + inPosition.y * 2.9) * 0.24;
    float swayAmp = push.windStrength * inst.tintAndParams.w * windWeight;

    vec2 windOffset = vec2(0.88, 0.47) * (gust * swayAmp) + vec2(-0.47, 0.88) * (flutter * swayAmp * 0.35);
    vec3 worldPos = inst.posAndYaw.xyz + rotated + vec3(windOffset, -abs(gust) * swayAmp * 0.12);

    fragTexCoord = inTexCoord;
    gl_Position = push.lightSpaceMatrix * vec4(worldPos, 1.0);
}
