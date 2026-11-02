#version 450

layout(location = 0) in vec2 inUV;

// Bind your new Osprey speaker texture here!
layout(set = 1, binding = 0) uniform sampler2D texSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(texSampler, inUV);
    
    // Throw away fully transparent pixels so they don't write to the depth buffer
    if (texColor.a < 0.1) {
        discard; 
    }
    
    outColor = texColor;
}