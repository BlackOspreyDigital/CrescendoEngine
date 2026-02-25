#version 450

layout (location = 0) in vec3 inUVW;
layout (location = 0) out vec4 outColor;

layout (binding = 1) uniform sampler2D skyTexture; 

layout(set = 0, binding = 3) uniform GlobalUniforms{
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 params;
    vec4 fogColor;
    vec4 fogParams;
    vec4 skyColor;
    vec4 groundColor;
    vec4 lightSpaceMatrices[4];
    vec4 cascadeSplits;
} global;

vec2 EquirectangularUV(vec3 v) {
    // Z is now the vertical axis (asin), and Y/X form the horizontal plane (atan)
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z)); 
    uv *= vec2(0.1591, 0.3183);
    uv += 0.5;
    return uv;
}

void main() {
    vec3 viewDir = normalize(inUVW);
    int skyType = int(global.params.y);

    if (skyType == 0) {
        // --- 0: SOLID COLOR 
        outColor = vec4(global.skyColor.rgb, 1.0);
    }
    else if (skyType == 1) {
        // --- 1: PROCEDURAL SKY 
        // vertical gradiuent
        float upBlend = smoothstep(-0.2, 0.4, viewDir.z);
        vec3 skyGrad = mix(global.groundColor.rgb, global.skyColor.rgb, upBlend);

        // --- 2: DRAW SUN DISC
        float sunDot = dot(viewDir, normalize(global.sunDirection.xyz));
        float sunDisc = smoothstep(0.998, 0.999, sunDot); // creates a hard/soft edge
        vec3 sunGlow = global.sunColor.rgb * global.sunDirection.w * sunDisc;

        outColor = vec4(skyGrad + sunGlow, 1.0);
    }
    else {
        // --- 2: HDR MAP ---
        vec3 hdrColor = texture(skyTexture, EquirectangularUV(viewDir)).rgb;
        outColor = vec4(hdrColor, 1.0);
    }
}