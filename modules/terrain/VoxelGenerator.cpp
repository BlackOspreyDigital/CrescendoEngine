#include "VoxelGenerator.hpp"
#include "MarchingCubesTables.hpp"
#include <cstdint>
#include <glm/gtc/noise.hpp>

namespace Crescendo {
namespace Terrain {

    // The threshold where terrain becomes solid air
    const float ISO_LEVEL = 0.0f; 

    // 1. ADD 'int lod' TO THE SIGNATURE
    float VoxelGenerator::EvaluateDensity(const glm::vec3& worldPos, const VoxelSettings& settings, int lod) {
        float baseDistance = glm::length(worldPos) - settings.radius; 
        float noiseValue = 0.0f;
        float currentAmplitude = settings.amplitude;
        float currentFrequency = settings.frequency;
        float weight = 1.0f; 

        // --- LOD CULLING MAGIC ---
        // If we are far away (LOD >= 2), only calculate 2 octaves.
        // If we are in orbit (LOD >= 4), only calculate 1 octave!
        int activeOctaves = settings.octaves;
        if (lod >= 2) activeOctaves = 2;
        if (lod >= 4) activeOctaves = 1;

        for (int i = 0; i < activeOctaves; i++) {
            float v = glm::simplex(worldPos * currentFrequency);
            v = 1.0f - std::abs(v);
            v *= v;
            noiseValue += v * currentAmplitude * weight;
            currentFrequency *= 2.0f; 
            currentAmplitude *= 0.5f; 
            weight = glm::clamp(v * 2.0f, 0.0f, 1.0f);
        }

        return baseDistance - noiseValue; 
    }

    glm::vec3 VoxelGenerator::CalculateNormal(const glm::vec3& p, const VoxelSettings& settings) {
        const float h = 0.01f;

        float dx = EvaluateDensity(p + glm::vec3(h, 0, 0), settings) - EvaluateDensity(p - glm::vec3(h, 0, 0), settings);
        float dy = EvaluateDensity(p + glm::vec3(0, h, 0), settings) - EvaluateDensity(p - glm::vec3(0, h, 0), settings);
        float dz = EvaluateDensity(p + glm::vec3(0, 0, h), settings) - EvaluateDensity(p - glm::vec3(0, 0, h), settings);

        return glm::normalize(glm::vec3(dx, dy, dz));
    }

    glm::vec3 VoxelGenerator::VertexInterp(float isolevel, const glm::vec3& p1, const glm::vec3& p2, float valp1, float valp2) {
        // Smoothly interpolate the vertex position based on the density weights
        if (std::abs(isolevel - valp1) < 0.00001f) return p1;
        if (std::abs(isolevel - valp2) < 0.00001f) return p2;
        if (std::abs(valp1 - valp2) < 0.00001f) return p1;

        float mu = (isolevel - valp1) / (valp2 - valp1);
        return p1 + mu * (p2 - p1);
    }

    // 1. Update the signature here too!
    ChunkData VoxelGenerator::GenerateChunk(const glm::vec3& origin, int resolution, float size, const VoxelSettings& settings, int lod) {
        ChunkData chunk;
        float step = size / resolution;

        // Loop through the 3D grid
        for (int x = 0; x < resolution; x++) {
            for (int y = 0; y < resolution; y++) {
                for (int z = 0; z < resolution; z++) {
                    
                    // Calculate the world positions of the 8 corners of this specific cube
                    glm::vec3 pos = origin + glm::vec3(x, y, z) * step;
                    
                    glm::vec3 corners[8] = {
                        pos + glm::vec3(0, 0, 1) * step,
                        pos + glm::vec3(1, 0, 1) * step,
                        pos + glm::vec3(1, 0, 0) * step,
                        pos + glm::vec3(0, 0, 0) * step,
                        pos + glm::vec3(0, 1, 1) * step,
                        pos + glm::vec3(1, 1, 1) * step,
                        pos + glm::vec3(1, 1, 0) * step,
                        pos + glm::vec3(0, 1, 0) * step
                    };

                    // 2. Sample the density at each corner AND PASS THE LOD!
                    float values[8];
                    for (int i = 0; i < 8; i++) {
                        values[i] = EvaluateDensity(corners[i], settings, lod); 
                    }

                    // Determine the 8-bit index of the cube configuration
                    int cubeIndex = 0;
                    if (values[0] < ISO_LEVEL) cubeIndex |= 1;
                    if (values[1] < ISO_LEVEL) cubeIndex |= 2;
                    if (values[2] < ISO_LEVEL) cubeIndex |= 4;
                    if (values[3] < ISO_LEVEL) cubeIndex |= 8;
                    if (values[4] < ISO_LEVEL) cubeIndex |= 16;
                    if (values[5] < ISO_LEVEL) cubeIndex |= 32;
                    if (values[6] < ISO_LEVEL) cubeIndex |= 64;
                    if (values[7] < ISO_LEVEL) cubeIndex |= 128;

                    // If the cube is completely inside or completely outside the terrain, skip it!
                    if (edgeTable[cubeIndex] == 0) continue;

                    // Find the exact intersecting vertices on the edges
                    glm::vec3 vertList[12];
                    if (edgeTable[cubeIndex] & 1) vertList[0] = VertexInterp(ISO_LEVEL, corners[0], corners[1], values[0], values[1]);
                    if (edgeTable[cubeIndex] & 2) vertList[1] = VertexInterp(ISO_LEVEL, corners[1], corners[2], values[1], values[2]);
                    if (edgeTable[cubeIndex] & 4) vertList[2] = VertexInterp(ISO_LEVEL, corners[2], corners[3], values[2], values[3]);
                    if (edgeTable[cubeIndex] & 8) vertList[3] = VertexInterp(ISO_LEVEL, corners[3], corners[0], values[3], values[0]);
                    if (edgeTable[cubeIndex] & 16) vertList[4] = VertexInterp(ISO_LEVEL, corners[4], corners[5], values[4], values[5]);
                    if (edgeTable[cubeIndex] & 32) vertList[5] = VertexInterp(ISO_LEVEL, corners[5], corners[6], values[5], values[6]);
                    if (edgeTable[cubeIndex] & 64) vertList[6] = VertexInterp(ISO_LEVEL, corners[6], corners[7], values[6], values[7]);
                    if (edgeTable[cubeIndex] & 128) vertList[7] = VertexInterp(ISO_LEVEL, corners[7], corners[4], values[7], values[4]);
                    if (edgeTable[cubeIndex] & 256) vertList[8] = VertexInterp(ISO_LEVEL, corners[0], corners[4], values[0], values[4]);
                    if (edgeTable[cubeIndex] & 512) vertList[9] = VertexInterp(ISO_LEVEL, corners[1], corners[5], values[1], values[5]);
                    if (edgeTable[cubeIndex] & 1024) vertList[10] = VertexInterp(ISO_LEVEL, corners[2], corners[6], values[2], values[6]);
                    if (edgeTable[cubeIndex] & 2048) vertList[11] = VertexInterp(ISO_LEVEL, corners[3], corners[7], values[3], values[7]);

                    // Generate the actual triangles
                    for (int i = 0; triTable[cubeIndex][i] != -1; i += 3) {
                        Vertex v1, v2, v3;
                        v1.pos = vertList[triTable[cubeIndex][i]];
                        v2.pos = vertList[triTable[cubeIndex][i + 1]];
                        v3.pos = vertList[triTable[cubeIndex][i + 2]];

                        // Calculate face normal (cross product)
                        v1.normal = CalculateNormal(v1.pos, settings);
                        v2.normal = CalculateNormal(v2.pos, settings);
                        v3.normal = CalculateNormal(v3.pos, settings);
                        
                        // Default color/UV
                        v1.color = glm::vec3(0.4f, 0.8f, 0.4f); 
                        v2.color = glm::vec3(0.4f, 0.8f, 0.4f);
                        v3.color = glm::vec3(0.4f, 0.8f, 0.4f);

                        chunk.vertices.push_back(v1);
                        chunk.vertices.push_back(v2);
                        chunk.vertices.push_back(v3);

                        // Indices (Flipped winding order for Vulkan!)
                        uint32_t startIndex = chunk.vertices.size() - 3;
                        chunk.indices.push_back(startIndex + 2);
                        chunk.indices.push_back(startIndex + 1);
                        chunk.indices.push_back(startIndex);
                    }
                }
            }
        }

        return chunk;
    }

    void VoxelGenerator::GenerateWaterSphere(float radius, int segments, int rings, std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
        outVertices.clear();
        outIndices.clear();

        // Generate Vertices
        for (int y = 0; y <= rings; ++y) {
            float v = (float)y / (float)rings;
            float phi = v * glm::pi<float>(); // 0 to pi

            for (int x = 0; x <= segments; ++x) {
                float u = (float)x / (float)segments;
                float theta = u * glm::pi<float>() * 2.0f; // 0 to 2*PI

                // Spherical to cartesian coordinates
                float xPos = std::cos(theta) * std::sin(phi);
                float yPos = std::cos(phi);
                float zPos = std::sin(theta) * std::sin(phi);

                Vertex vertex{};
                vertex.pos = glm::vec3(xPos, yPos, zPos) * radius;
                vertex.normal = glm::normalize(glm::vec3(xPos, yPos, zPos));
                vertex.texCoord = glm::vec2(u, v);

                outVertices.push_back(vertex);
            }
        }

        // Generate Indices 
        for (int y = 0; y < rings; ++y) {
            for (int x = 0; x < segments; ++x) {
                uint32_t i0 = (y + 1) * (segments + 1) + x;
                uint32_t i1 = y * (segments + 1) + x;
                uint32_t i2 = y * (segments + 1) + x + 1;
                uint32_t i3 = (y + 1) * (segments + 1) + x + 1;

                // Two triangles per quad
                outIndices.push_back(i0); outIndices.push_back(i2); outIndices.push_back(i1);
                outIndices.push_back(i0); outIndices.push_back(i3); outIndices.push_back(i2);

            }
        }
    }

}
}