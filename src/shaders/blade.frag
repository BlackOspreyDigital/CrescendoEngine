#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragTint;
// fragSelectedFactor is still passed in but we won't use it for lighting
layout(location = 2) in float fragSelectedFactor; 
layout(location = 3) flat in int fragLabelID;
layout(location = 4) in vec4 fragIconUVBounds;

layout(location = 0) out vec4 outColor;

layout(binding = 1) uniform sampler2D spriteAtlas; 
layout(binding = 2) uniform sampler2D backgroundTex;

void main() {
    // 1. Flip UVs and sample the base card
    vec2 correctUV = vec2(fragUV.x, 1.0 - fragUV.y);
    vec4 bgSample = texture(backgroundTex, correctUV);
    
    // 2. Alpha mask based on JPG luma (The "Cutout" logic)
    float luma = dot(bgSample.rgb, vec3(0.299, 0.587, 0.114));
    float alphaMask = smoothstep(0.01, 0.05, luma);
    
    // 3. Icon Compositing
    // We center the icon on the card, scaling it by 0.55 to fit the inset
    vec2 iconUVSpace = (correctUV - 0.5) / 0.55 + 0.5; 
    
    vec3 finalColor = bgSample.rgb;
    
    if (iconUVSpace.x > 0.0 && iconUVSpace.x < 1.0 && 
        iconUVSpace.y > 0.0 && iconUVSpace.y < 1.0) {
        
        vec2 atlasUV = mix(fragIconUVBounds.xy, fragIconUVBounds.zw, iconUVSpace);
        vec4 iconColor = texture(spriteAtlas, atlasUV);
        
        // Pure alpha composition, no glowing or tinting
        finalColor = mix(finalColor, iconColor.rgb, iconColor.a);
    }

    outColor = vec4(finalColor, alphaMask * fragTint.a);
}