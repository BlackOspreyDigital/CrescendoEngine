#version 450
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 fragUV;
layout(location = 1) in vec4 fragColor;
layout(location = 2) in flat float fragTexIndex;

layout(location = 0) out vec4 outColor;

// Re-using your exact global texture bank from the 3D pipeline!
layout(binding = 0) uniform sampler2D texSamplers[];

void main() {
    int id = int(fragTexIndex);
    vec4 texColor = texture(texSamplers[nonuniformEXT(id)], fragUV);
    outColor = texColor * fragColor;
}