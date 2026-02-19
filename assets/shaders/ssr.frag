#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outReflection;

// Our G-Buffer and Screen Inputs
layout(binding = 0) uniform sampler2D sceneColorTex;
layout(binding = 1) uniform sampler2D normalRoughnessTex;
layout(binding = 2) uniform sampler2D depthTex;

// We need these to convert between Screen Space and View Space
layout(push_constant) uniform PushConsts {
    mat4 proj;
    mat4 invProj;
    mat4 view;
    mat4 invView;
} params;

// --- Helper: Reconstruct View-Space Position from Depth ---
vec3 reconstructViewPos(vec2 uv, float depth) {
    vec4 clipSpace = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewSpace = params.invProj * clipSpace;
    return viewSpace.xyz / viewSpace.w;
}

void main() {
    // 1. Sample G-Buffer
    vec4 normRough = texture(normalRoughnessTex, fragUV);
    vec3 worldNormal = normRough.xyz;
    float roughness = normRough.a;

    // If it's too rough, don't waste GPU cycles raymarching!
    if (roughness > 0.6 || length(worldNormal) < 0.1) {
        outReflection = vec4(0.0);
        return;
    }

    float depth = texture(depthTex, fragUV).r;
    if (depth == 1.0) { // Skybox (no reflection)
        outReflection = vec4(0.0);
        return;
    }

    // 2. Setup View Space Ray
    vec3 viewPos = reconstructViewPos(fragUV, depth);
    vec3 viewNormal = mat3(params.view) * worldNormal; // Convert World Normal to View Normal
    
    vec3 viewDir = normalize(viewPos);
    vec3 reflectDir = normalize(reflect(viewDir, viewNormal));

    // 3. Ray Marching Setup
    float maxDistance = 20.0;
    float resolution  = 0.3; // Step size
    int   steps       = 50;  // Max linear steps
    int   binarySteps = 10;  // Max refinement steps

    vec3 rayPos = viewPos;
    vec3 rayDir = reflectDir * resolution;

    vec4 hitColor = vec4(0.0);
    bool hit = false;

    // 4. Linear Search (March forward until we go BEHIND an object)
    for (int i = 0; i < steps; i++) {
        rayPos += rayDir;
        
        // Project ray back to screen space to check depth
        vec4 projPos = params.proj * vec4(rayPos, 1.0);
        projPos.xyz /= projPos.w;
        vec2 sampleUV = projPos.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sampledDepth = texture(depthTex, sampleUV).r;
        vec3 sampledViewPos = reconstructViewPos(sampleUV, sampledDepth);

        // Check if our ray is behind the geometry, but not TOO far behind (thickness check)
        float depthDiff = rayPos.z - sampledViewPos.z;
        if (depthDiff > 0.0 && depthDiff < 1.5) {
            hit = true;
            break;
        }
    }

    // 5. Binary Search Refinement (Zoom in exactly on the hit point to prevent jagged edges)
    if (hit) {
        for (int i = 0; i < binarySteps; i++) {
            rayDir *= 0.5;
            
            vec4 projPos = params.proj * vec4(rayPos, 1.0);
            projPos.xyz /= projPos.w;
            vec2 sampleUV = projPos.xy * 0.5 + 0.5;
            
            float sampledDepth = texture(depthTex, sampleUV).r;
            vec3 sampledViewPos = reconstructViewPos(sampleUV, sampledDepth);
            
            float depthDiff = rayPos.z - sampledViewPos.z;
            if (depthDiff > 0.0) {
                rayPos -= rayDir; // Too far, step back
            } else {
                rayPos += rayDir; // Not far enough, step forward
            }
        }

        // 6. Final Screen Space Sample
        vec4 projPos = params.proj * vec4(rayPos, 1.0);
        projPos.xyz /= projPos.w;
        vec2 finalUV = projPos.xy * 0.5 + 0.5;

        // Fade out at the edges of the screen
        vec2 edgeFade = smoothstep(0.0, 0.1, finalUV) * smoothstep(1.0, 0.9, finalUV);
        float totalFade = edgeFade.x * edgeFade.y;

        // Fade based on roughness
        float roughFade = 1.0 - (roughness / 0.6);

        hitColor = vec4(texture(sceneColorTex, finalUV).rgb, totalFade * roughFade);
    }

    outReflection = hitColor;
}