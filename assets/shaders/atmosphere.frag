#version 450

layout(location = 0) in vec3 inWorldPos;
layout(location = 0) out vec4 outColor;
// 9 Depth Mapping
layout(binding = 9) uniform sampler2D sceneDepth;

layout(push_constant) uniform AtmoPush {
    mat4 vp;                                   
    vec3 sunDirection;
    float planetRadius;     
    vec3 planetCenter; float atmosphereRadius; 
    vec3 cameraPos;    float sunIntensity;     
    vec3 rayleighCoeff; float mieCoeff;        
} push;

// Constants
const int NUM_IN_SCATTER_POINTS = 10;

vec2 raySphereIntersect(vec3 r0, vec3 rd, vec3 s0, float sr) {
    float a = dot(rd, rd);
    vec3 s0_r0 = r0 - s0;
    float b = 2.0 * dot(rd, s0_r0);
    float c = dot(s0_r0, s0_r0) - (sr * sr);
    float d = b * b - 4.0 * a * c;
    if (d < 0.0) return vec2(-1.0, -1.0);
    return vec2(
        (-b - sqrt(d)) / (2.0 * a),
        (-b + sqrt(d)) / (2.0 * a)
    );
}

void main() {
    vec3 rayDir = normalize(inWorldPos - push.cameraPos);
    vec3 sunDir = -normalize(push.sunDirection);

    // 1. Intersect Atmosphere
    vec2 atmoHit = raySphereIntersect(push.cameraPos, rayDir, push.planetCenter, push.atmosphereRadius);
    if (atmoHit.y < 0.0) {
        discard;
    }

    float d0 = max(0.0, atmoHit.x); 
    float d1 = atmoHit.y;

    // 2. Depth Buffer Ray Termination
    ivec2 texSize = textureSize(sceneDepth, 0);
    vec2 screenUV = gl_FragCoord.xy / vec2(texSize);
    float rawDepth = texture(sceneDepth, screenUV).r;
    
    // In Reversed-Z, 0.0 is the absolute void.
    // ONLY stop the ray if we hit a solid physical object!
    if (rawDepth > 0.0) {
        // Reconstruct the exact Euclidean distance using the VP matrix!
        mat4 invVP = inverse(push.vp);
        vec4 clipSpace = vec4(screenUV * 2.0 - 1.0, rawDepth, 1.0);
        vec4 worldSpace = invVP * clipSpace;
        worldSpace.xyz /= worldSpace.w;
        
        // Since camera is at (0,0,0) in RTE space, length() gives true Euclidean distance
        float trueDepthDistance = length(worldSpace.xyz);
        
        if (trueDepthDistance < atmoHit.y) {
            d1 = trueDepthDistance;
        }
    }

    // SAFETY CATCH: If the terrain is closer than the atmosphere (e.g. standing next to a mountain),
    // or if the depth buffer terminated the ray before d0, draw nothing!
    if (d1 <= d0) {
        outColor = vec4(0.0);
        return;
    }

    // 3. Calculate ray length and step size using the corrected d1
    float rayLength = d1 - d0;
    if (rayLength <= 0.0) discard;
    
    float stepSize = rayLength / float(NUM_IN_SCATTER_POINTS);
    vec3 currentPoint = push.cameraPos + rayDir * (d0 + stepSize * 0.5);

    // THE FIX: Calculate scale heights dynamically!
    // This ensures density perfectly approaches 0.0 exactly at the sphere's edge.
    float atmosphereThickness = push.atmosphereRadius - push.planetRadius;
    float rayleighScaleHeight = atmosphereThickness * 0.25; 
    float mieScaleHeight = atmosphereThickness * 0.05; 

    float opticalDepthR = 0.0;
    float opticalDepthM = 0.0;

    // THE FIX: Declare the accumulators as pure floats BEFORE the loop!
    float totalRayleigh = 0.0;
    float totalMie = 0.0;

    for (int i = 0; i < NUM_IN_SCATTER_POINTS; i++) {
        float height = length(currentPoint - push.planetCenter) - push.planetRadius;
        if (height < 0.0) height = 0.0;

        float stepR = exp(-height / rayleighScaleHeight) * stepSize;
        float stepM = exp(-height / mieScaleHeight) * stepSize;
        opticalDepthR += stepR;
        opticalDepthM += stepM;

        // THE FIX 2: Planetary Shadows!
        // Cast a ray toward the sun. If it hits the planet core, we are in the shadows!
        float lightVisibility = 1.0;
        vec2 sunPlanetHit = raySphereIntersect(currentPoint, sunDir, push.planetCenter, push.planetRadius);
        if (sunPlanetHit.x > 0.0) {
            lightVisibility = 0.0;
            // Block the light!
        }

        float sunRayLength = raySphereIntersect(currentPoint, sunDir, push.planetCenter, push.atmosphereRadius).y;
        float sunHeight = length(currentPoint + sunDir * (sunRayLength * 0.5) - push.planetCenter) - push.planetRadius;
        if (sunHeight < 0.0) sunHeight = 0.0;

        float sunOpticalDepthR = exp(-sunHeight / rayleighScaleHeight) * sunRayLength;
        float sunOpticalDepthM = exp(-sunHeight / mieScaleHeight) * sunRayLength;

        // THE FIX: Attenuation must be a pure float density inside the loop!
        // We use an average of the coefficients for the density math.
        float avgRayleigh = (push.rayleighCoeff.x + push.rayleighCoeff.y + push.rayleighCoeff.z) / 3.0;
        float avgMie = push.mieCoeff;
        float attenuation = exp(-(avgMie * (opticalDepthM + sunOpticalDepthM) + avgRayleigh * (opticalDepthR + sunOpticalDepthR)));
        totalRayleigh += attenuation * stepR * lightVisibility;
        totalMie += attenuation * stepM * lightVisibility;

        currentPoint += rayDir * stepSize;
    }
    

    float cosAngle = dot(rayDir, sunDir);
    float phaseR = 3.0 / (16.0 * 3.14159) * (1.0 + cosAngle * cosAngle);
    
    float g = 0.76;
    float phaseM = 3.0 / (8.0 * 3.14159) * ((1.0 - g * g) * (1.0 + cosAngle * cosAngle)) / ((2.0 + g * g) * pow(1.0 + g * g - 2.0 * g * cosAngle, 1.5));
    
    // THE FIX: Multiply the pure float densities by the vec3 colors here at the very end!
    vec3 scatterR = totalRayleigh * push.rayleighCoeff * phaseR;
    vec3 scatterM = totalMie * vec3(push.mieCoeff) * phaseM;
    
    // Generate the final color
    vec3 finalColor = (scatterR + scatterM) * push.sunIntensity;
    
    float alpha = clamp(length(finalColor) * 1.5, 0.0, 1.0);
    finalColor = vec3(1.0) - exp(-finalColor * 1.2);
    // Tone mapping

    outColor = vec4(finalColor, alpha);
}