#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragUV;
layout(location = 3) in vec3 fragPos;

layout(location = 0) out vec4 outColor;

// FIX: Must match C++ "MeshPushConstants" exactly (128 Bytes)
layout(push_constant) uniform PushConstants {
    mat4 renderMatrix;
    // mat4 normalMatrix; <--- REMOVED (Safety Fix)
    vec4 camPos;
    vec4 pbrParams;    // We use .x for textureID if needed
    vec4 sunDir;
    vec4 sunColor;
} push;

const float GRID_SIZE = 1.0; 
const float AXIS_WIDTH = 0.05; 

float getGridLine(vec2 coord, float spacing) {
    vec2 derivative = fwidth(coord);
    vec2 grid = abs(fract(coord / spacing - 0.5) - 0.5) / derivative;
    float line = min(grid.x, grid.y);
    return 1.0 - min(line, 1.0);
}

float getAxisLine(float coord, float derivative, float width) {
    float axisDist = abs(coord) / derivative;
    return 1.0 - step(width, axisDist);
}

void main() {
    // 1. Setup Coordinates (Z-Up World = Grid on XY Plane)
    vec2 coord = fragPos.xy; 
    vec2 derivative = fwidth(coord);

    // 2. Draw Grid
    float gridFactor = getGridLine(coord, GRID_SIZE);
    float gridAlpha = gridFactor * 0.3; 
    
    vec3 finalColor = vec3(0.4); // Grey Lines

    // 3. Draw Axis Lines
    // X Axis (Red) runs where Y is near 0
    float xAxis = getAxisLine(coord.y, derivative.y, AXIS_WIDTH); 
    // Y Axis (Green) runs where X is near 0
    float yAxis = getAxisLine(coord.x, derivative.x, AXIS_WIDTH); 

    if (xAxis > 0.0) {
        finalColor = mix(finalColor, vec3(0.8, 0.1, 0.1), xAxis); 
        gridAlpha = max(gridAlpha, xAxis);
    }
    if (yAxis > 0.0) {
        finalColor = mix(finalColor, vec3(0.1, 0.8, 0.1), yAxis); 
        gridAlpha = max(gridAlpha, yAxis);
    }

    // 4. Distance Fade (Hides the "horizon shimmer")
    float dist = distance(push.camPos.xy, fragPos.xy);
    float fade = 1.0 - smoothstep(10.0, 100.0, dist); 
    
    gridAlpha *= fade;

    // 5. Final Output
    if(gridAlpha < 0.01) discard;
    outColor = vec4(finalColor, gridAlpha);
}