#include "CanvasRenderer.hpp"
#include <fstream>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Crescendo {

    CanvasRenderer::CanvasRenderer() {}
    CanvasRenderer::~CanvasRenderer() {}

    std::vector<char> CanvasRenderer::readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("failed to open file: " + filename);
        size_t fileSize = (size_t) file.tellg();
        std::vector<char> buffer(fileSize);
        file.seekg(0);
        file.read(buffer.data(), fileSize);
        file.close();
        return buffer;
    }

    VkShaderModule CanvasRenderer::createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        createInfo.codeSize = code.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule shaderModule;
        vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
        return shaderModule;
    }

    void CanvasRenderer::Initialize(VkDevice logicalDevice, VmaAllocator vmaAllocator, VkRenderPass renderPass, VkDescriptorSetLayout descriptorLayout) {
        this->device = logicalDevice;
        this->allocator = vmaAllocator;

        // 1. Allocate massive dynamic buffers
        VkDeviceSize vSize = sizeof(Vertex2D) * MAX_VERTICES;
        VkDeviceSize iSize = sizeof(uint32_t) * MAX_INDICES;

        vertexBuffer = VulkanBuffer(allocator, vSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        indexBuffer = VulkanBuffer(allocator, iSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);

        vmaMapMemory(allocator, vertexBuffer.allocation, &vertexMapped);
        vmaMapMemory(allocator, indexBuffer.allocation, &indexMapped);

        // 2. Pipeline Layout (Push constant for the Ortho Matrix)
        VkPushConstantRange pushConstant{};
        pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstant.offset = 0;
        pushConstant.size = sizeof(glm::mat4);

        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &descriptorLayout; 
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushConstant;

        vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout);

        // 3. Pipeline Setup 
        auto vertCode = readFile("assets/shaders/canvas.vert.spv");
        auto fragCode = readFile("assets/shaders/canvas.frag.spv");

        VkShaderModule vertModule = createShaderModule(vertCode);
        VkShaderModule fragModule = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo stages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vertModule, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragModule, "main", nullptr}
        };

        auto bindingDesc = Vertex2D::getBindingDescription();
        auto attrDesc = Vertex2D::getAttributeDescriptions();

        VkPipelineVertexInputStateCreateInfo vertexInputInfo{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertexInputInfo.vertexBindingDescriptionCount = 1;
        vertexInputInfo.pVertexBindingDescriptions = &bindingDesc;
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDesc.size());
        vertexInputInfo.pVertexAttributeDescriptions = attrDesc.data();

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.cullMode = VK_CULL_MODE_NONE; 
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo multisampling{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthStencil.depthTestEnable = VK_FALSE;
        depthStencil.depthWriteEnable = VK_FALSE;

        // True Alpha Blending
        VkPipelineColorBlendAttachmentState colorBlend{};
        colorBlend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlend.blendEnable = VK_TRUE;
        colorBlend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlend.colorBlendOp = VK_BLEND_OP_ADD;
        colorBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlend.alphaBlendOp = VK_BLEND_OP_ADD;

        VkPipelineColorBlendStateCreateInfo colorBlending{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        colorBlending.attachmentCount = 1;
        colorBlending.pAttachments = &colorBlend;

        std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamicState{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = stages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = pipelineLayout;
        pipelineInfo.renderPass = renderPass; 

        vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);

        vkDestroyShaderModule(device, fragModule, nullptr);
        vkDestroyShaderModule(device, vertModule, nullptr);
    }

    void CanvasRenderer::Shutdown() {
        if (device) {
            if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
            if (pipelineLayout) vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            
            vertexBuffer.destroy();
            indexBuffer.destroy();
        }
    }

    void CanvasRenderer::BeginFrame() {
        batchedVertices.clear();
        batchedIndices.clear();
    }

    void CanvasRenderer::DrawImage(const glm::vec2& pos, const glm::vec2& size, const glm::vec2& uv0, const glm::vec2& uv1, float textureID, const glm::vec4& color) {
        if (batchedVertices.size() + 4 >= MAX_VERTICES || batchedIndices.size() + 6 >= MAX_INDICES) {
            return; 
        }

        uint32_t i = static_cast<uint32_t>(batchedVertices.size());

        batchedVertices.push_back({ {pos.x, pos.y},                   {uv0.x, uv0.y}, color, textureID });
        batchedVertices.push_back({ {pos.x + size.x, pos.y},          {uv1.x, uv0.y}, color, textureID });
        batchedVertices.push_back({ {pos.x + size.x, pos.y + size.y}, {uv1.x, uv1.y}, color, textureID });
        batchedVertices.push_back({ {pos.x, pos.y + size.y},          {uv0.x, uv1.y}, color, textureID });

        batchedIndices.insert(batchedIndices.end(), { i, i+1, i+2, i+2, i+3, i });
    }

    void CanvasRenderer::DrawRect(const glm::vec2& pos, const glm::vec2& size, const glm::vec4& color) {
        DrawImage(pos, size, {0, 0}, {1, 1}, 0.0f, color);
    }

    void CanvasRenderer::Render(VkCommandBuffer cmd, const glm::vec2& screenSize, VkDescriptorSet globalDescriptorSet) {
        if (batchedIndices.empty()) return;

        memcpy(vertexMapped, batchedVertices.data(), batchedVertices.size() * sizeof(Vertex2D));
        memcpy(indexMapped, batchedIndices.data(), batchedIndices.size() * sizeof(uint32_t));

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkViewport viewport{0.0f, 0.0f, screenSize.x, screenSize.y, 0.0f, 1.0f};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        
        VkRect2D scissor{{0, 0}, {(uint32_t)screenSize.x, (uint32_t)screenSize.y}};
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        glm::mat4 proj = glm::ortho(0.0f, screenSize.x, 0.0f, screenSize.y, -1.0f, 1.0f);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &proj);

        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &globalDescriptorSet, 0, nullptr);

        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.handle, offsets);
        vkCmdBindIndexBuffer(cmd, indexBuffer.handle, 0, VK_INDEX_TYPE_UINT32);

        vkCmdDrawIndexed(cmd, static_cast<uint32_t>(batchedIndices.size()), 1, 0, 0, 0);
    }

} // namespace Crescendo