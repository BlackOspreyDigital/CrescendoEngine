#include "KtxLoader.hpp"
#include "servers/rendering/RenderingServer.hpp"
#include <iostream>
#include <vector>
#include <algorithm>
#include <deps/gli/gli/gli.hpp>

// Include the Khronos library
#include <ktx.h>
#include <ktxvulkan.h>

namespace Crescendo {

    int KtxLoader::loadTexture(RenderingServer* renderer, const std::string& path) {
        
        // 1. Check if the RenderingServer already loaded it
        if (renderer->cache.textures.find(path) != renderer->cache.textures.end()) {
            return renderer->cache.textures[path];
        }

        // Ensure we don't overflow the texture bank
        int newID = static_cast<int>(renderer->textureMap.size()) + 1;
        if (newID >= 100) { // MAX_TEXTURES
            std::cerr << "[KTX] Texture bank full! Cannot load: " << path << std::endl;
            return 0; 
        }

        std::cout << "[KTX] Loading hardware-compressed texture: " << path << std::endl;

        // 2. Load the KTX2 file from disk
        ktxTexture2* kTexture;
        KTX_error_code result = ktxTexture2_CreateFromNamedFile(path.c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &kTexture);
        if (result != KTX_SUCCESS) {
            std::cerr << "[KTX Error] Failed to open file." << std::endl;
            return 0;
        }

        // 3. Transcode Basis Universal to PC Hardware Format (BC7)
        if (ktxTexture2_NeedsTranscoding(kTexture)) {
            result = ktxTexture2_TranscodeBasis(kTexture, KTX_TTF_BC7_RGBA, 0);
            if (result != KTX_SUCCESS) {
                std::cerr << "[KTX Error] Failed to transcode texture to BC7." << std::endl;
                ktxTexture_Destroy(ktxTexture(kTexture));
                return 0;
            }
        }

        // 4. Extract texture metadata
        VkFormat format = (VkFormat)kTexture->vkFormat;
        uint32_t width = kTexture->baseWidth;
        uint32_t height = kTexture->baseHeight;
        uint32_t mipLevels = kTexture->numLevels;

        // 5. Create VMA Image (Matches your engine's RAII wrapper!)
        TextureResource resource{};
        
        VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = format;
        imageInfo.extent = {width, height, 1};
        imageInfo.mipLevels = mipLevels;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateImage(renderer->allocator, &imageInfo, &allocInfo, &resource.image.handle, &resource.image.allocation, nullptr) != VK_SUCCESS) {
            std::cerr << "[KTX Error] VMA Failed to allocate image." << std::endl;
            ktxTexture_Destroy(ktxTexture(kTexture));
            return 0;
        }
        resource.image.allocator = renderer->allocator;
        resource.image.device = renderer->device;

        // 6. Push data to Staging Buffer
        VkDeviceSize imageSize = ktxTexture_GetDataSize(ktxTexture(kTexture));
        VulkanBuffer stagingBuffer(renderer->allocator, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        
        void* data;
        vmaMapMemory(renderer->allocator, stagingBuffer.allocation, &data);
        memcpy(data, ktxTexture_GetData(ktxTexture(kTexture)), imageSize);
        vmaUnmapMemory(renderer->allocator, stagingBuffer.allocation);

        // 7. Calculate Mip-Map Copy Regions
        std::vector<VkBufferImageCopy> bufferCopyRegions;
        for (uint32_t i = 0; i < mipLevels; i++) {
            ktx_size_t offset;
            ktxTexture_GetImageOffset(ktxTexture(kTexture), i, 0, 0, &offset);
            
            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = i;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            
            // Halve the resolution for each mip level
            region.imageExtent.width = std::max(1u, width >> i);
            region.imageExtent.height = std::max(1u, height >> i);
            region.imageExtent.depth = 1;
            
            bufferCopyRegions.push_back(region);
        }

        // 8. Execute GPU Copy Commands
        VkCommandBuffer cmd = renderer->beginSingleTimeCommands();
        renderer->transitionImageLayout(cmd, resource.image.handle, format, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, mipLevels, 1);
        vkCmdCopyBufferToImage(cmd, stagingBuffer.handle, resource.image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());
        renderer->transitionImageLayout(cmd, resource.image.handle, format, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, mipLevels, 1);
        renderer->endSingleTimeCommands(cmd);

        // 9. Create Image View
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = resource.image.handle;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = mipLevels;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(renderer->device, &viewInfo, nullptr, &resource.image.view);

        // Clean up the Khronos object
        ktxTexture_Destroy(ktxTexture(kTexture));

        // 10. Update the Engine's Texture Bank!
        if (newID < renderer->textureBank.size()) {
            renderer->textureBank[newID] = std::move(resource);
        }

        renderer->cache.textures[path] = newID;
        renderer->textureMap[path] = newID;

        // 11. Push the new hardware-compressed texture to the Vulkan Descriptor Set!
        if (!renderer->descriptorSets.empty()) {
            for (size_t i = 0; i < renderer->descriptorSets.size(); i++) {
                VkDescriptorImageInfo imageInfoDesc{};
                imageInfoDesc.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfoDesc.imageView = renderer->textureBank[newID].image.view;
                imageInfoDesc.sampler = renderer->textureSampler;

                VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                descriptorWrite.dstSet = renderer->descriptorSets[i];
                descriptorWrite.dstBinding = 0;
                descriptorWrite.dstArrayElement = newID;
                descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                descriptorWrite.descriptorCount = 1;
                descriptorWrite.pImageInfo = &imageInfoDesc;

                vkUpdateDescriptorSets(renderer->device, 1, &descriptorWrite, 0, nullptr);
            }
        }

        return newID;
    }

    void RawCubemapData::free() {
        if (internalPtr) {
            // Delete the heap-allocated GLI texture to free the RAM
            delete static_cast<gli::texture_cube*>(internalPtr);
            internalPtr = nullptr;
            pixels = nullptr;
        }
    }

    RawCubemapData KtxLoader::loadCubemap(const std::string& path) {
        RawCubemapData data;

        gli::texture rawTex = gli::load(path);
        if (rawTex.empty() || rawTex.target() != gli::TARGET_CUBE) {
            std::cerr << "[KtxLoader] Invalid or missing cubemap file: " << path << std::endl;
            return data;
        }

        // Heap allocate the texture so it doesn't get destroyed when this function returns 
        gli::texture_cube* tex = new gli::texture_cube(rawTex);

        data.internalPtr = tex;
        data.pixels      = tex->data();
        data.totalSize   = tex->size();
        data.width       = static_cast<uint32_t>(tex->extent().x);
        data.height      = static_cast<uint32_t>(tex->extent().y);
        data.mipLevels   = static_cast<uint32_t>(tex->levels());

        std::cout << "[KtxLoader] Parsed Cubemap: " << path << " (mips: " << data.mipLevels << ")" << std::endl;
        return data;
    }
}