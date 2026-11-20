#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 4) in vec3 fragTangent;
layout(location = 5) in vec3 fragBitangent;
layout(location = 6) in float fragSubsurface;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

layout(binding = 0) uniform sampler2D texSampler[100];
layout(binding = 1) uniform samplerCube skyTexture;
layout(binding = 4) uniform sampler2DArrayShadow shadowMap;

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
    vec4 skyColor;
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

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness) *
           GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float SamplePCF(vec3 coords, int cascadeIndex) {
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0).xy);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadow += texture(shadowMap, vec4(coords.xy + vec2(x, y) * texelSize, float(cascadeIndex), coords.z));
        }
    }
    return shadow / 9.0;
}

float CalculateShadow(vec3 worldPos, vec3 normal, float viewDepth) {
    vec3 bias = normal * 0.025;
    int cascadeIndex = -1;
    vec3 projCoords = vec3(0.0);

    for (int i = 0; i < 4; i++) {
        vec4 fragPosLightSpace = global.lightSpaceMatrices[i] * vec4(worldPos + bias, 1.0);
        vec3 coords = fragPosLightSpace.xyz / fragPosLightSpace.w;
        coords.xy = coords.xy * 0.5 + 0.5;
        if (coords.x > 0.01 && coords.x < 0.99 &&
            coords.y > 0.01 && coords.y < 0.99 &&
            coords.z > 0.0 && coords.z < 1.0) {
            cascadeIndex = i;
            projCoords = coords;
            break;
        }
    }

    if (cascadeIndex == -1 || projCoords.z > 1.0) return 1.0;

    float shadow = SamplePCF(projCoords, cascadeIndex);
    float currentSplitDist = global.cascadeSplits[cascadeIndex];
    float blendBand = 5.0;
    if (cascadeIndex < 3 && viewDepth > (currentSplitDist - blendBand)) {
        vec4 nextLightSpace = global.lightSpaceMatrices[cascadeIndex + 1] * vec4(worldPos + bias, 1.0);
        vec3 nextCoords = nextLightSpace.xyz / nextLightSpace.w;
        nextCoords.xy = nextCoords.xy * 0.5 + 0.5;
        float nextShadow = SamplePCF(nextCoords, cascadeIndex + 1);
        float blendFactor = smoothstep(currentSplitDist - blendBand, currentSplitDist, viewDepth);
        shadow = mix(shadow, nextShadow, blendFactor);
    }

    float maxShadowDist = global.cascadeSplits[3];
    float fadeFactor = smoothstep(maxShadowDist - 15.0, maxShadowDist, viewDepth);
    return mix(shadow, 1.0, fadeFactor);
}

vec3 ApplyFog(vec3 rgb, float dist, vec3 posCamRel) {
    float fogDensity = global.fogColor.a;
    if (fogDensity <= 0.00001) return rgb;

    float fogFalloff = max(global.fogParams.x, 0.001);
    float maxOpacity = clamp(global.fogParams.y, 0.0, 1.0);
    float fogStart   = max(global.fogParams.z, 0.0);
    float fogFloor   = global.fogParams.w;

    vec3 worldPos = posCamRel + global.cameraPos.xyz;
    float rayLen = max(dist, 0.001);
    vec3 v = posCamRel / rayLen;
    float cz = max(global.cameraPos.z - fogFloor, 0.0);
    float vz = v.z;

    float tau = 0.0;
    if (abs(vz) > 0.001) {
        float t0 = min(fogStart, rayLen);
        float intStart = exp(-fogFalloff * (cz + t0 * vz));
        float intEnd   = exp(-fogFalloff * (cz + rayLen * vz));
        tau = max((fogDensity / (fogFalloff * vz)) * (intStart - intEnd), 0.0);
    } else {
        tau = fogDensity * exp(-fogFalloff * cz) * max(dist - fogStart, 0.0);
    }

    float fowRadius = global.pointLightParams.w;
    float visionClear = 0.0;
    if (fowRadius > 1.0) {
        float distToBase = length(worldPos.xy - global.pointLightParams.yz);
        visionClear = max(visionClear, smoothstep(fowRadius, fowRadius * 0.70, distToBase));
    }

    int unitVisionCount = int(global.pointLightParams.x);
    for (int i = 0; i < unitVisionCount && i < 64; ++i) {
        float uRadius = global.pointLights[i].positionAndRadius.w;
        if (uRadius > 1.0) {
            float distToUnit = length(worldPos - global.pointLights[i].positionAndRadius.xyz);
            if (distToUnit < uRadius) {
                visionClear = max(visionClear, smoothstep(uRadius, uRadius * 0.65, distToUnit));
                if (visionClear >= 0.999) break;
            }
        }
    }

    float tauEffective = tau * (1.0 - clamp(visionClear, 0.0, 1.0));
    float transmittance = clamp(exp(-tauEffective), 1.0 - maxOpacity, 1.0);

    vec3 sunDir = normalize(global.sunDirection.xyz);
    float sunScattering = max(dot(-v, sunDir), 0.0);
    vec3 inscatterColor = mix(global.fogColor.rgb, global.sunColor.rgb, pow(sunScattering, 8.0) * 0.35);
    return mix(inscatterColor, rgb, transmittance);
}

void main() {
    vec4 albedoSample = (push.texID > 0) ? texture(texSampler[push.texID], fragTexCoord) : vec4(1.0);
    if (albedoSample.a < push.alphaCutoff) {
        discard;
    }

    vec3 albedo = albedoSample.rgb * fragColor;
    vec3 N = normalize(fragNormal);
    if (!gl_FrontFacing) {
        N = -N;
    }

    if (push.normalTexID > 0) {
        vec3 T = normalize(fragTangent);
        vec3 B = normalize(fragBitangent);
        vec3 mapNormal = texture(texSampler[push.normalTexID], fragTexCoord).rgb * 2.0 - 1.0;
        if (length(mapNormal) > 0.001) {
            mapNormal = normalize(mapNormal);
            T = normalize(T - dot(T, N) * N);
            N = normalize(mat3(T, B, N) * mapNormal);
        }
    }

    float roughness = clamp(push.roughness, 0.08, 1.0);
    outNormal = vec4(N, roughness);

    vec3 V = normalize(-fragPos);
    vec3 L = (length(global.sunDirection.xyz) > 0.01) ? normalize(-global.sunDirection.xyz) : vec3(0.0, 0.0, 1.0);
    vec3 H = normalize(V + L);
    vec3 F0 = vec3(0.04);

    float skyWeight = N.z * 0.5 + 0.5;
    vec3 hemiAmbient = mix(global.groundColor.rgb, global.skyColor.rgb, skyWeight) * albedo * 0.18;

    float viewDist = length(fragPos);
    float shadowFactor = CalculateShadow(fragPos, N, viewDist);

    vec3 directLight = vec3(0.0);
    if (length(global.sunDirection.xyz) > 0.01) {
        float NdotL = max(dot(N, L), 0.0);
        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3 F    = fresnelSchlick(max(dot(H, V), 0.0), F0);
        float denom = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
        vec3 specular = (NDF * G * F) / denom;
        vec3 kD = vec3(1.0) - F;

        vec3 radiance = global.sunColor.rgb * global.sunDirection.w;
        directLight = (kD * albedo / PI + specular) * radiance * NdotL * shadowFactor;

        // Foliage subsurface translucency + warm backlit wrap
        float backLight = pow(clamp(dot(V, -L), 0.0, 1.0), 3.0) * 0.65 + clamp(dot(-N, L), 0.0, 1.0) * 0.35;
        directLight += albedo * radiance * backLight * fragSubsurface * (0.25 + 0.75 * shadowFactor);
    }

    vec3 finalOutput = ApplyFog(directLight + hemiAmbient, viewDist, fragPos);
    float outAlpha = (push.texID > 0) ? clamp((albedoSample.a - push.alphaCutoff) / max(1.0 - push.alphaCutoff, 0.001) + 0.5, 0.0, 1.0) : 1.0;
    outColor = vec4(finalOutput, outAlpha);
}
