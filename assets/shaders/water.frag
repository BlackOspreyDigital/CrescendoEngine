#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform sampler2D texSampler[100];

layout(push_constant) uniform constants {
    mat4 renderMatrix;
    vec4 camPos;
    vec4 pbrParams; // x = textureID, w = time
    vec4 sunDir;
    vec4 sunColor;
} PushConstants;

void main() {
    // 1. Sample Texture
    int texID = int(PushConstants.pbrParams.x);
    vec4 texColor = texture(texSampler[texID], fragUV);
    
    // 2. Mix with Blue Tint
    vec3 waterColor = texColor.rgb * vec3(0.0, 0.4, 0.8); // Deep Blue Tint
    
    // 3. Simple Specular (Sun Reflection)
    vec3 viewDir = normalize(PushConstants.camPos.xyz - fragPos);
    vec3 lightDir = normalize(vec3(5.0, 10.0, 5.0)); // Fake sun
    if (length(PushConstants.sunDir.xyz) > 0.0) lightDir = normalize(PushConstants.sunDir.xyz);

    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normalize(fragNormal), halfDir), 0.0), 32.0);
    
    // 4. Output with Alpha (Transparency)
    // We assume the pipeline has blending ENABLED (which we did earlier)
    vec3 finalColor = waterColor + (spec * vec3(1.0));
    outColor = vec4(finalColor, 0.7); // 0.7 Alpha for transparency
}