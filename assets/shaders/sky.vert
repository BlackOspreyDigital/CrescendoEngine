#version 450

layout(location = 0) out vec3 outUVW;

struct PointLight {
    vec4 positionAndRadius;
    vec4 colorAndIntensity;
};

layout(set = 0, binding = 3) uniform GlobalUniforms {
    mat4 viewProj;
    mat4 view;
    mat4 proj;
    mat4 lightSpaceMatrices[4];
    vec4 cascadeSplits;
    vec4 cameraPos;
    vec4 sunDirection;
    vec4 sunColor;
    vec4 params;
    vec4 fogColor;
    vec4 fogParams;
    vec4 skyColor;
    vec4 groundColor;
    vec4 pointLightParams;
    PointLight pointLights[16];
} global;

void main() {
    // 1. MAGIC: Mathematically generate a full-screen triangle using the Vertex ID!
    vec2 uv = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vec2 inPos = uv * 2.0 - 1.0; // Maps uv (0 to 2) down to clip space (-1 to 1)

    // 2. Remove translation from the view matrix so the sky stays infinitely far away
    mat4 viewRot = mat4(mat3(global.view));
    mat4 invViewProj = inverse(global.proj * viewRot);
    
    // 3. Unproject the 2D screen quad into a 3D ray
    vec4 target = invViewProj * vec4(inPos, 1.0, 1.0);
    outUVW = target.xyz / target.w;
    
    // 4. Force depth to exactly 1.0 so it renders strictly behind everything
    gl_Position = vec4(inPos, 1.0, 1.0); 
}