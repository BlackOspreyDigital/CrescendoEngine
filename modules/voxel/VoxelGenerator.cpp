#include "VoxelGenerator.hpp"
#include <glm/gtc/noise.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>

namespace Crescendo::Voxel {

    float SampleNoise(const glm::vec3& p, int octaves, float freq, float amp) {
        float value = 0.0f;
        float currentFreq = freq;
        float currentAmp = amp;
        for (int i = 0; i < octaves; i++) {
            float n = glm::simplex(p * currentFreq);
            value += n * currentAmp;
            currentFreq *= 2.0f;
            currentAmp *= 0.5f;
        }
        return value;
    }

    float VoxelGenerator::EvaluateDensity(const glm::vec3& worldPos, const VoxelSettings& settings) {
        float distToCenter = glm::length(worldPos);
        float baseDensity = distToCenter - settings.radius;
        float noise = SampleNoise(worldPos, settings.octaves, settings.frequency, settings.amplitude);
        return baseDensity - noise;
    }

    // Generate flat-shaded low-poly planet mesh matching oldvoxel.png style
    ChunkMesh VoxelGenerator::GeneratePlanetMesh(const VoxelSettings& settings, int resolution) {
        ChunkMesh mesh;
        int rings = std::max(8, resolution / 2);
        int sectors = std::max(8, resolution / 2);

        std::vector<glm::vec3> points((rings + 1) * (sectors + 1));

        for (int r = 0; r <= rings; r++) {
            float theta = r * glm::pi<float>() / rings;
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (int s = 0; s <= sectors; s++) {
                float phi = s * 2.0f * glm::pi<float>() / sectors;
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                glm::vec3 normal(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
                float noise = SampleNoise(normal * settings.radius, settings.octaves, settings.frequency, settings.amplitude);
                float rDist = settings.radius + noise;
                
                int idx = r * (sectors + 1) + s;
                points[idx] = normal * rDist;
            }
        }

        for (int r = 0; r < rings; r++) {
            for (int s = 0; s < sectors; s++) {
                int p0 = r * (sectors + 1) + s;
                int p1 = (r + 1) * (sectors + 1) + s;
                int p2 = (r + 1) * (sectors + 1) + (s + 1);
                int p3 = r * (sectors + 1) + (s + 1);

                glm::vec3 v0 = points[p0];
                glm::vec3 v1 = points[p1];
                glm::vec3 v2 = points[p2];
                glm::vec3 v3 = points[p3];

                // Triangle 1: p0, p2, p1 (flat shaded)
                glm::vec3 n1 = glm::normalize(glm::cross(v2 - v0, v1 - v0));
                uint32_t baseIdx1 = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({v0, glm::vec3(1.0f), n1, glm::vec2(0)});
                mesh.vertices.push_back({v2, glm::vec3(1.0f), n1, glm::vec2(0)});
                mesh.vertices.push_back({v1, glm::vec3(1.0f), n1, glm::vec2(0)});
                mesh.indices.push_back(baseIdx1);
                mesh.indices.push_back(baseIdx1 + 1);
                mesh.indices.push_back(baseIdx1 + 2);

                // Triangle 2: p0, p3, p2 (flat shaded)
                glm::vec3 n2 = glm::normalize(glm::cross(v3 - v0, v2 - v0));
                uint32_t baseIdx2 = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({v0, glm::vec3(1.0f), n2, glm::vec2(0)});
                mesh.vertices.push_back({v3, glm::vec3(1.0f), n2, glm::vec2(0)});
                mesh.vertices.push_back({v2, glm::vec3(1.0f), n2, glm::vec2(0)});
                mesh.indices.push_back(baseIdx2);
                mesh.indices.push_back(baseIdx2 + 1);
                mesh.indices.push_back(baseIdx2 + 2);
            }
        }

        mesh.hasGeometry = !mesh.vertices.empty() && !mesh.indices.empty();
        return mesh;
    }

    ChunkMesh VoxelGenerator::GenerateSphereMesh(float radius, int resolution) {
        ChunkMesh mesh;
        int rings = std::max(8, resolution / 2);
        int sectors = std::max(8, resolution / 2);

        std::vector<glm::vec3> points((rings + 1) * (sectors + 1));

        for (int r = 0; r <= rings; r++) {
            float theta = r * glm::pi<float>() / rings;
            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (int s = 0; s <= sectors; s++) {
                float phi = s * 2.0f * glm::pi<float>() / sectors;
                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                glm::vec3 normal(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
                int idx = r * (sectors + 1) + s;
                points[idx] = normal * radius;
            }
        }

        for (int r = 0; r < rings; r++) {
            for (int s = 0; s < sectors; s++) {
                int p0 = r * (sectors + 1) + s;
                int p1 = (r + 1) * (sectors + 1) + s;
                int p2 = (r + 1) * (sectors + 1) + (s + 1);
                int p3 = r * (sectors + 1) + (s + 1);

                glm::vec3 v0 = points[p0];
                glm::vec3 v1 = points[p1];
                glm::vec3 v2 = points[p2];
                glm::vec3 v3 = points[p3];

                glm::vec3 n1 = glm::normalize(glm::cross(v2 - v0, v1 - v0));
                uint32_t baseIdx1 = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({v0, glm::vec3(1.0f), n1, glm::vec2(0)});
                mesh.vertices.push_back({v2, glm::vec3(1.0f), n1, glm::vec2(0)});
                mesh.vertices.push_back({v1, glm::vec3(1.0f), n1, glm::vec2(0)});
                mesh.indices.push_back(baseIdx1);
                mesh.indices.push_back(baseIdx1 + 1);
                mesh.indices.push_back(baseIdx1 + 2);

                glm::vec3 n2 = glm::normalize(glm::cross(v3 - v0, v2 - v0));
                uint32_t baseIdx2 = static_cast<uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back({v0, glm::vec3(1.0f), n2, glm::vec2(0)});
                mesh.vertices.push_back({v3, glm::vec3(1.0f), n2, glm::vec2(0)});
                mesh.vertices.push_back({v2, glm::vec3(1.0f), n2, glm::vec2(0)});
                mesh.indices.push_back(baseIdx2);
                mesh.indices.push_back(baseIdx2 + 1);
                mesh.indices.push_back(baseIdx2 + 2);
            }
        }

        mesh.hasGeometry = !mesh.vertices.empty() && !mesh.indices.empty();
        return mesh;
    }
}
