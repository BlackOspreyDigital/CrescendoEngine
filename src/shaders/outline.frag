#version 450

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal; 

void main() {
    // Cranked up to 15.0 so it triggers the Bloom threshold!
    outColor = vec4(15.0, 8.0, 0.0, 1.0); 
    outNormal = vec4(0.0);
}