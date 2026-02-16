#pragma once
#include <vulkan/vulkan_core.h>
// [FIX] Prevent redefinition warnings if GLM is already enabled elsewhere
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp> 
#include <array>
#include <vector>

struct Vertex {
    glm::vec3 pos;
    glm::vec3 color;
    glm::vec3 normal;
    glm::vec2 texCoord;
    glm::vec2 texCoord1; // Second UV Channel
    glm::vec3 tangent;
    glm::vec3 bitangent;

    // 1. Equality Operator (Needed for deduplication) 
    bool operator ==(const Vertex& other) const {
        return pos == other.pos &&
        color == other.color &&
        normal == other.normal &&
        texCoord == other.texCoord &&
        texCoord1 == other.texCoord1; // do this for the other channel that require separate uvs
    }

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 7> getAttributeDescriptions() {
        // Check your Vertex.hpp getAttributeDescriptions() implementation:
    std::array<VkVertexInputAttributeDescription, 7> attributeDescriptions{};
        
        // Position (Location 0)
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        // Color (Location 1) - THIS WAS LIKELY MISSING OR SWAPPED
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, color);

        // Normal (Location 2)
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, normal);

        // UV (Location 3)
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[3].offset = offsetof(Vertex, texCoord);

        // Tangent (Location 4)
        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[4].offset = offsetof(Vertex, tangent);

        // Bitangent
        attributeDescriptions[5].binding = 0;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[5].offset = offsetof(Vertex, bitangent);

        // UV1 (Location 6)
        attributeDescriptions[6].binding = 0;
        attributeDescriptions[6].location = 6;
        attributeDescriptions[6].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[6].offset = offsetof(Vertex, texCoord1);
        
        return attributeDescriptions;
        }
    };

// 2. Hash Function (Put this OUTSIDE the struct, in the std namespace)
namespace std {
    template<> struct hash<Vertex> {
        size_t operator()(Vertex const& vertex) const {
            return ((hash<glm::vec3>()(vertex.pos) ^
                   (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
                   (hash<glm::vec3>()(vertex.normal) << 1) ^
                   (hash<glm::vec2>()(vertex.texCoord) << 1) ^
                   (hash<glm::vec2>()(vertex.texCoord1) << 1) ^
                   (hash<glm::vec3>()(vertex.tangent) << 1) ^    
                   (hash<glm::vec3>()(vertex.bitangent) << 1); 
        }
    };
}