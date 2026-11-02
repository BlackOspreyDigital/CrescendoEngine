#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec3 fragNormal;
layout(location = 3) in vec3 fragPos;
layout(location = 6) in flat int inEntityIndex;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

void main() {
    // Center UV at (0, 0)
    vec2 p = fragTexCoord - vec2(0.5);
    float d = length(p);

    // Procedural ring boundaries
    float innerR = 0.36;
    float outerR = 0.49;
    float edgeWidth = 0.015;

    float alpha = smoothstep(innerR - edgeWidth, innerR, d) *
                  (1.0 - smoothstep(outerR, outerR + edgeWidth, d));

    if (alpha < 0.05) {
        discard;
    }

    // High emissive glow for HDR bloom pass
    vec3 ringColor = vec3(0.0, 1.0, 0.6) * 5.0;
    outColor = vec4(ringColor, alpha);
    outNormal = vec4(fragNormal, 1.0);
}
