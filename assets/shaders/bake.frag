#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;

// We output to two textures at once (MRT)
layout(location = 0) out vec4 outWorldPosition;
layout(location = 1) out vec4 outWorldNormal;

void main() {
    // write the raw 3D data directly to the image
    outWorldPosition = vec4(fragWorldPos, 1.0);
    outWorldNormal = vec4(fragNormal, 1.0);
}