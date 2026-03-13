#include "VoxelGenerator.hpp"
#include "MarchingCubesTables.hpp"
#include <glm/gtc/noise.hpp>

namespace Crescendo {
namespace Terrain {

    // The threshold where terrain becomes solid air
    const float ISO_LEVEL = 0.0f; 

    float VoxelGenerator::EvaluateDensity(const glm::vec3& worldPos, const VoxelSettings& settings) {
        // 1. The Base Sphere (now using settings!)
        float baseDistance = glm::length(worldPos) - settings.radius; 

        // 2. Fractional Brownian Motion (fBM) Variables
        float noiseValue = 0.0f;
        glm::vec3 samplePos = worldPos;
        float currentAmplitude = settings.amplitude;
        float currentFrequency = settings.frequency;

        // 3. Layer the noise!
        for (int i = 0; i < settings.octaves; i++) {
            noiseValue += glm::simplex(samplePos * currentFrequency) * currentAmplitude;
            currentAmplitude *= 0.5f; 
            currentFrequency *= 2.0f; 
        }

        // 4. Subtract the noise from the sphere's surface
        return baseDistance - noiseValue; 
    }

    glm::vec3 VoxelGenerator::VertexInterp(float isolevel, const glm::vec3& p1, const glm::vec3& p2, float valp1, float valp2) {
        // Smoothly interpolate the vertex position based on the density weights
        if (std::abs(isolevel - valp1) < 0.00001f) return p1;
        if (std::abs(isolevel - valp2) < 0.00001f) return p2;
        if (std::abs(valp1 - valp2) < 0.00001f) return p1;

        float mu = (isolevel - valp1) / (valp2 - valp1);
        return p1 + mu * (p2 - p1);
    }

    ChunkData VoxelGenerator::GenerateChunk(const glm::vec3& origin, int resolution, float size, const VoxelSettings& settings) {
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

                    // Sample the density at each corner
                    float values[8];
                    for (int i = 0; i < 8; i++) {
                        values[i] = EvaluateDensity(corners[i], settings); // <-- Add settings here!
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
                        glm::vec3 normal = glm::normalize(glm::cross(v2.pos - v1.pos, v3.pos - v1.pos));
                        v1.normal = normal; v2.normal = normal; v3.normal = normal;
                        
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
}
}