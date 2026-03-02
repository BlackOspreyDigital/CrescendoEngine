#pragma once

#include <vulkan/vulkan.h>
#include <fstream>
#include <vector>
#include <stdexcept>
#include <string>

namespace Crescendo {

    // Helper to read a compiled shader (.spv) into a binary byte array
    inline std::vector<char> ReadFile(const std::string& filename) {
        // Start reading at the end of the file (ate) in binary mode
        std::ifstream file(filename, std::ios::ate | std::ios::binary);

        if (!file.is_open()) {
            throw std::runtime_error("[VulkanUtils] Failed to open file: " + filename);
        }

        size_t fileSize = (size_t)file.tellg();
        std::vector<char> buffer(fileSize);

        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();

        return buffer;
    }

    // Helper to turn that byte array into a Vulkan Shader Module
    inline VkShaderModule CreateShaderModule(VkDevice device, const std::string& filename) {
        std::vector<char> code = ReadFile(filename);

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

        VkShaderModule shaderModule;
        if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
            throw std::runtime_error("[VulkanUtils] Failed to create shader module from: " + filename);
        }

        return shaderModule;
    }

}