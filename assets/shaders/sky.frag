#version 450

layout(location = 0) in vec2 inUV; 
layout(location = 0) out vec4 outColor;

layout(push_constant) uniform EnvironmentData {
    mat4 invViewProj;
    vec4 sunDirection; // xyz = dir, w = intensity
    vec4 zenithColor;
    vec4 horizonColor;
} push;

void main() {
    // Reconstruct 3D view direction
    vec4 target = push.invViewProj * vec4(inUV * 2.0 - 1.0, 1.0, 1.0);
    vec3 viewDir = normalize(target.xyz / target.w);
    vec3 sunDir = normalize(-push.sunDirection.xyz);

    // Dynamic Gradient (Driven by ImGui!)
    float horizonBlend = max(viewDir.z, 0.0);
    vec3 skyColor = mix(push.horizonColor.rgb, push.zenithColor.rgb, horizonBlend);

    // Procedural Sun
    float sunDot = dot(viewDir, sunDir);
    float sunDisc = pow(max(0.0, sunDot), 2000.0);
    float sunHalo = pow(max(0.0, sunDot), 250.0) * 0.6;

    // Dynamic Intensity (Driven by ImGui!)
    vec3 sunGlow = vec3(1.0, 0.95, 0.8) * (sunDisc + sunHalo) * push.sunDirection.w;

    outColor = vec4(skyColor + sunGlow, 1.0);
}