#include "SymbolServer.hpp"
#include <iostream>
#include "servers/rendering/vulkan/VulkanUtils.hpp"

namespace Crescendo {

    bool SymbolServer::Initialize(VkDevice device, VkRenderPass renderPass, VkDescriptorSetLayout globalSetLayout, VkDescriptorSetLayout textureSetLayout) {
        
        // 1. PUSH CONSTANTS
        VkPushConstantRange pushConstantRange{};
        pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pushConstantRange.offset = 0;
        pushConstantRange.size = sizeof(SymbolPushConstant);

        // 2. PIPELINE LAYOUT (Now expects 2 descriptor sets!)
        std::vector<VkDescriptorSetLayout> descriptorSetLayouts = {globalSetLayout, textureSetLayout};

        VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
        pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size()); 
        pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data(); 
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

        if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
            std::cerr << "[SymbolServer] Failed to create pipeline layout!" << std::endl;
            return false;
        }

        // 3. SHADERS (Make sure these paths match!)
        VkShaderModule vertShaderModule = CreateShaderModule(device, "assets/shaders/billboard.vert.spv");
        VkShaderModule fragShaderModule = CreateShaderModule(device, "assets/shaders/billboard.frag.spv");

        VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
        vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertShaderStageInfo.module = vertShaderModule;
        vertShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
        fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragShaderStageInfo.module = fragShaderModule;
        fragShaderStageInfo.pName = "main";

        VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

        // --- THE AAA TRICK: VERTEXLESS RENDERING ---
        VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
        vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInputInfo.vertexBindingDescriptionCount = 0; // ZERO BUFFERS!
        vertexInputInfo.pVertexBindingDescriptions = nullptr;
        vertexInputInfo.vertexAttributeDescriptionCount = 0; // ZERO ATTRIBUTES!
        vertexInputInfo.pVertexAttributeDescriptions = nullptr;
        // -------------------------------------------

        VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
        inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        inputAssembly.primitiveRestartEnable = VK_FALSE;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo rasterizer{};
        rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasterizer.depthClampEnable = VK_FALSE;
        rasterizer.rasterizerDiscardEnable = VK_FALSE;
        rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
        rasterizer.lineWidth = 1.0f;
        rasterizer.cullMode = VK_CULL_MODE_NONE; // Don't cull, we always face the camera
        rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rasterizer.depthBiasEnable = VK_FALSE;

        VkPipelineMultisampleStateCreateInfo multisampling{};
        multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        multisampling.sampleShadingEnable = VK_FALSE;
        multisampling.rasterizationSamples = VK_SAMPLE_COUNT_4_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = VK_TRUE;
        depthStencil.depthWriteEnable = VK_FALSE; // VERY IMPORTANT for transparent billboards!
        depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

        // --- UPDATE THIS BLOCK ---
        VkPipelineColorBlendAttachmentState colorBlendAttachments[2] = {};
        
        // Attachment 0: Scene Color
        colorBlendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        colorBlendAttachments[0].blendEnable = VK_TRUE; 
        colorBlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        colorBlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        colorBlendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD;
        colorBlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        colorBlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        colorBlendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD;

        // Attachment 1: Normal G-Buffer (Clone Attachment 0 to satisfy Vulkan)
        colorBlendAttachments[1] = colorBlendAttachments[0]; 

        VkPipelineColorBlendStateCreateInfo colorBlending{};
        colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        colorBlending.logicOpEnable = VK_FALSE;
        colorBlending.attachmentCount = 2; // WE NEED 2 TO MATCH THE RENDER PASS!
        colorBlending.pAttachments = colorBlendAttachments;
        

        std::vector<VkDynamicState> dynamicStates = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamicState{};
        dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamicState.pDynamicStates = dynamicStates.data();

        VkGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipelineInfo.stageCount = 2;
        pipelineInfo.pStages = shaderStages;
        pipelineInfo.pVertexInputState = &vertexInputInfo;
        pipelineInfo.pInputAssemblyState = &inputAssembly;
        pipelineInfo.pViewportState = &viewportState;
        pipelineInfo.pRasterizationState = &rasterizer;
        pipelineInfo.pMultisampleState = &multisampling;
        pipelineInfo.pDepthStencilState = &depthStencil;
        pipelineInfo.pColorBlendState = &colorBlending;
        pipelineInfo.pDynamicState = &dynamicState;
        pipelineInfo.layout = m_pipelineLayout;
        pipelineInfo.renderPass = renderPass;
        pipelineInfo.subpass = 0; // Adjust if your engine uses multiple subpasses!

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline) != VK_SUCCESS) {
            std::cerr << "[SymbolServer] Failed to create graphics pipeline!" << std::endl;
            return false;
        }

        vkDestroyShaderModule(device, fragShaderModule, nullptr);
        vkDestroyShaderModule(device, vertShaderModule, nullptr);

        return true;
    }

    void SymbolServer::Cleanup(VkDevice device) {
        if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
        if (m_pipelineLayout) vkDestroyPipelineLayout(device, m_pipelineLayout, nullptr);
    }

    void SymbolServer::SubmitSymbol(const glm::vec3& position, float scale) {
        m_queuedSymbols.push_back(position);
    }

    void SymbolServer::ClearSymbols() {
        m_queuedSymbols.clear();
    }
   
    void SymbolServer::DrawSymbols(VkCommandBuffer cmd, const glm::vec3& camRight, const glm::vec3& camUp, VkDescriptorSet globalSet, VkDescriptorSet textureSet) {
        
        if (m_queuedSymbols.empty()) return;

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);

        // --- THE FIX: BIND BOTH SETS ---
        // Set 0 = Global Camera Matrices
        // Set 1 = Speaker Texture
        std::vector<VkDescriptorSet> descriptorSets = {globalSet, textureSet};
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 2, descriptorSets.data(), 0, nullptr);
        
        for (const auto& pos : m_queuedSymbols) {
            SymbolPushConstant pc{};
            pc.worldPosition = pos;
            pc.scale = 2.0f; // Let's bump this to 2.0f temporarily so it is HUGE and impossible to miss
            pc.cameraRight = camRight;
            pc.cameraUp = camUp;

            vkCmdPushConstants(cmd, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(SymbolPushConstant), &pc);
            vkCmdDraw(cmd, 6, 1, 0, 0); 
        }
    }
}