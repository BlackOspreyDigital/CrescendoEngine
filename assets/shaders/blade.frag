#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;
layout(location = 2) in float fragSelectedFactor;
layout(location = 3) flat in int fragLabelID;

layout(location = 0) out vec4 outColor;

// 2D Signed Distance Field for a rounded box
float sdRoundRect(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + vec2(r);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
}

void main() {
    // 1. Center UV coordinates to range [-1.0 to 1.0]
    vec2 p = fragUV * 2.0 - 1.0;
    
    // 2. The "Hourglass" Contour
    // Bends the X coordinates inward depending on the Y height
    float pinch = 1.0 - 0.12 * cos(p.y * 1.570796); 
    p.x /= pinch;

    // 3. Define the SDF shapes
    vec2 bladeSize = vec2(0.85, 0.95);
    float cornerRadius = 0.12;
    
    // Distance to the outer edge, and distance to the inner recessed panel
    float dOuter = sdRoundRect(p, bladeSize, cornerRadius);
    float dInner = sdRoundRect(p, bladeSize - vec2(0.06, 0.1), cornerRadius * 0.5);

    // 4. Color Palette & Dynamic Glowing
    vec3 panelColor     = vec3(0.12, 0.13, 0.14); // Dark matte metallic gray
    vec3 inactiveGlow   = vec3(0.3, 0.3, 0.3);    // Subtle silver edge when in background
    vec3 activeGlow     = vec3(0.1, 1.0, 0.5);    // Neon Green when selected

    // Interpolate the rim color based on the C++ spring physics
    vec3 currentGlow = mix(inactiveGlow, activeGlow, fragSelectedFactor);

    // 5. Anti-Aliasing and Masking via Smoothstep
    // Instead of jagged pixels, smoothstep creates a perfect, resolution-independent edge
    float alphaMask = 1.0 - smoothstep(0.0, 0.015, dOuter);      // Master opacity mask
    float innerMask = 1.0 - smoothstep(0.0, 0.015, dInner);      // The dark center panel
    float innerBevel= smoothstep(0.0, 0.04, dInner);             // Soft ambient shadow inside the groove
    
    // 6. Layer the colors: Base Rim -> Overlaid with Dark Panel -> Darkened by Bevel
    vec3 finalColor = mix(currentGlow, panelColor, innerMask);
    finalColor *= mix(0.4, 1.0, innerBevel); // Fake Ambient Occlusion

    // If you push values > 1.0, Crescendo's bloom_bright.frag will naturally catch it!
    finalColor += currentGlow * (fragSelectedFactor * 0.5); 

    // 7. Output (Apply global fade alpha from your C++ state)
    // NOTE: This requires alpha blending enabled in your Vulkan Pipeline!
    outColor = vec4(finalColor, alphaMask * fragTint.a);
}