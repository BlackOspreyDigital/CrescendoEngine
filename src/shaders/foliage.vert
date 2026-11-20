#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec3 inNormal;
layout(location = 3) in vec2 inTexCoord;
layout(location = 4) in vec3 inTangent;
layout(location = 5) in vec3 inBitangent;
layout(location = 6) in vec2 inTexCoord1;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;
layout(location = 2) out vec3 fragNormal;
layout(location = 3) out vec3 fragPos;
layout(location = 4) out vec3 fragTangent;
layout(location = 5) out vec3 fragBitangent;
layout(location = 6) out float fragSubsurface;

struct FoliageGPUInstance {
    vec4 posAndYaw;     // xyz = camera-relative render pos, w = yaw radians
    vec4 scaleAndPhase; // xyz = scale, w = wind phase
    vec4 tintAndParams; // rgb = tint, w = sway/subsurface multiplier
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

layout(push_constant) uniform FoliagePush {
    uint firstInstance;
    int texID;
    int normalTexID;
    float roughness;
    float subsurface;
    float alphaCutoff;
    float windStrength;
    float windFrequency;
} push;

void main() {
    FoliageGPUInstance inst = foliageInstances[push.firstInstance + uint(gl_InstanceIndex)];

    vec3 s = inst.scaleAndPhase.xyz;
    float yaw = inst.posAndYaw.w;
    float cy = cos(yaw);
    float sy = sin(yaw);

    // Local scale then Z-up yaw rotation
    vec3 scaled = inPosition * s;
    vec3 rotated = vec3(
        scaled.x * cy - scaled.y * sy,
        scaled.x * sy + scaled.y * cy,
        scaled.z
    );

    // Wind sway: uses TEXCOORD_1.x when authored by artist/generator, else height-based fallback
    float windWeight = (inTexCoord1.x > 0.001) ? clamp(inTexCoord1.x, 0.0, 1.0) : clamp(inPosition.z * 0.45, 0.0, 1.0);
    float t = global.skyColor.w * push.windFrequency + inst.scaleAndPhase.w;
    float gust = sin(t + rotated.x * 0.55 + rotated.y * 0.25) * 0.72 + cos(t * 1.73 + rotated.y * 0.65) * 0.28;
    float flutter = sin(t * 3.1 + inPosition.x * 3.7 + inPosition.y * 2.9) * 0.24;
    float swayAmp = push.windStrength * inst.tintAndParams.w * windWeight;

    vec2 windOffset = vec2(0.88, 0.47) * (gust * swayAmp) + vec2(-0.47, 0.88) * (flutter * swayAmp * 0.35);
    vec3 worldPos = inst.posAndYaw.xyz + rotated + vec3(windOffset, -abs(gust) * swayAmp * 0.12);

    gl_Position = global.viewProj * vec4(worldPos, 1.0);

    vec3 invS = 1.0 / max(abs(s), vec3(0.0001));
    vec3 nScaled = inNormal * invS;
    vec3 nRot = vec3(nScaled.x * cy - nScaled.y * sy, nScaled.x * sy + nScaled.y * cy, nScaled.z);

    vec3 tRot = vec3(inTangent.x * cy - inTangent.y * sy, inTangent.x * sy + inTangent.y * cy, inTangent.z);
    vec3 bRot = vec3(inBitangent.x * cy - inBitangent.y * sy, inBitangent.x * sy + inBitangent.y * cy, inBitangent.z);

    fragPos = worldPos;
    fragColor = inColor * inst.tintAndParams.rgb;
    fragTexCoord = inTexCoord;
    fragNormal = normalize(nRot);
    fragTangent = normalize(tRot);
    fragBitangent = normalize(bRot);
    fragSubsurface = push.subsurface * inst.tintAndParams.a;
}
