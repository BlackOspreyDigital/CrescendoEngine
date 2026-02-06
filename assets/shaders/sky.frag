#version 450

layout (location = 0) in vec3 inViewDir;
layout (location = 0) out vec4 outColor;

void main() 
{
    vec3 viewDir = normalize(inViewDir);
    
    vec3 topColor = vec3(0.2, 0.4, 0.8);    // Deep Blue
    vec3 horizonColor = vec3(0.6, 0.7, 0.9); // Light Blue
    vec3 groundColor = vec3(0.1, 0.1, 0.1);  // Dark Grey
    vec3 sunDir = normalize(vec3(5.0, 1.0, 1.0)); // dir sun
    float sunSize = 0.998; // Higher = Smaller Sun

    float t = viewDir.z; // Z is Up in our engine
    
    vec3 skyColor;
    if (t > 0.0) {
        // Mix Horizon -> Top
        skyColor = mix(horizonColor, topColor, pow(t, 0.5));
    } else {
        // Mix Horizon -> Ground
        skyColor = mix(horizonColor, groundColor, pow(abs(t), 0.5));
    }

    // 2. Draw Sun
    float sunDot = dot(viewDir, sunDir);
    if (sunDot > sunSize) {
        // Simple Sun Disk
        skyColor = vec3(1.0, 1.0, 0.8) * 2.0; // Bright Yellow
    } else {
        // Sun Glow/Bloom
        float glow = pow(max(sunDot, 0.0), 100.0);
        skyColor += vec3(1.0, 0.8, 0.5) * glow * 0.5;
    }

    outColor = vec4(skyColor, 1.0);
}