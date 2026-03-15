#version 450

layout (location = 0) in vec3 inUVW;
layout (location = 0) out vec4 outColor;

// Our new 6-sided Cubemap!
layout (binding = 1) uniform samplerCube skybox; 

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
    PointLight pointLights[16];
} global;

void main() {
    // This is the 3D ray bouncing off the camera into the world
    vec3 viewDir = normalize(inUVW);
    int skyType = int(global.params.y);

    if (skyType == 0) {
        // --- 0: SOLID COLOR ---
        outColor = vec4(global.skyColor.rgb, 1.0);
    }
    else if (skyType == 1) {
        // --- 1: PROCEDURAL SKY ---
        float upBlend = smoothstep(-0.2, 0.4, viewDir.z);
        vec3 skyGrad = mix(global.groundColor.rgb, global.skyColor.rgb, upBlend);

        float sunDot = dot(viewDir, normalize(global.sunDirection.xyz));
        float sunDisc = smoothstep(0.998, 0.999, sunDot); 
        vec3 sunGlow = global.sunColor.rgb * global.sunDirection.w * sunDisc;
        
        outColor = vec4(skyGrad + sunGlow, 1.0);
    }
    else {
        // --- 2: HDR CUBEMAP IBL ---
        // The swizzle is removed. Because you invert the projection in C++, 
        // viewDir is already correctly mapped for the Cubemap!
        vec3 hdrColor = texture(skybox, viewDir).rgb;
        outColor = vec4(hdrColor, 1.0);
    }
}