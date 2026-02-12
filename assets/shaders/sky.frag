#version 450

layout (location = 0) in vec3 inViewDir;
layout (location = 0) out vec4 outColor;

layout (binding = 1) uniform sampler2D skyTexture; 

vec2 SampleSphericalMap(vec3 v) {
    // Z-UP FIX:
    // 1. Use v.z for latitude (asin) instead of v.y
    // 2. Use v.y, v.x for longitude (atan)
    vec2 uv = vec2(atan(v.y, v.x), asin(v.z));
    
    uv *= vec2(0.1591, 0.3183); // inv(2*PI), inv(PI)
    uv += 0.5;
    
    // Optional: If the sky looks upside down, uncomment this:
    uv.y = 1.0 - uv.y;
    
    return uv;
}

void main() {
    vec3 viewDir = normalize(inViewDir);
    vec2 uv = SampleSphericalMap(viewDir);
    
    vec3 color = texture(skyTexture, uv).rgb;
    
    // Optional: Tone mapping specifically for the sky
    // color = color / (color + vec3(1.0)); 
    
    outColor = vec4(color, 1.0);
}