#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(binding = 0) uniform sampler2D texSampler[100];

layout(push_constant) uniform ShadowFoliagePush {
    mat4 lightSpaceMatrix;
    uint firstInstance;
    int texID;
    float alphaCutoff;
    float windStrength;
    float windFrequency;
    float _pad0;
    float _pad1;
    float _pad2;
} push;

void main() {
    if (push.texID > 0) {
        if (texture(texSampler[push.texID], fragTexCoord).a < push.alphaCutoff) {
            discard;
        }
    }
}
