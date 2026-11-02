#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec4 inColor;
layout(location = 3) in float inTexIndex;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out vec4 fragColor;
layout(location = 2) out flat float fragTexIndex;

layout(push_constant) uniform Push { 
    mat4 orthoProj; 
} push;

void main() {
    gl_Position = push.orthoProj * vec4(inPos, 0.0, 1.0);
    fragUV = inUV;
    fragColor = inColor;
    fragTexIndex = inTexIndex;
}