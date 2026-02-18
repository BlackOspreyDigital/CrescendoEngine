#pragma once
#include <vulkan/vulkan.h>
#include "deps/vk_mem_alloc.h"
#include <stdexcept>

namespace Crescendo {

    // ========================================================================
    // RAII WRAPPER: BUFFER
    // ========================================================================
    struct VulkanBuffer {
        VkBuffer handle = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VmaAllocator allocator = nullptr;

        // 1. Default Constructor (Empty)
        VulkanBuffer() = default;

        // 2. Actual Constructor (Creates the Buffer)
        VulkanBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags flags) 
            : allocator(allocator) {
            
            VkBufferCreateInfo bufferInfo{};
            bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufferInfo.size = size;
            bufferInfo.usage = usage;
            bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo = {};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.flags = flags;

            if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &handle, &allocation, nullptr) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create RAII Buffer!");
            }
        }

        // 3. Destructor (Automatic Cleanup)
        ~VulkanBuffer() {
            destroy();
        }

        // 4. Move Constructor (Transfer Ownership)
        VulkanBuffer(VulkanBuffer&& other) noexcept {
            *this = std::move(other);
        }

        // 5. Move Assignment Operator
        VulkanBuffer& operator=(VulkanBuffer&& other) noexcept {
            if (this != &other) {
                destroy(); // Clean up our current mess first

                // Steal resources
                handle = other.handle;
                allocation = other.allocation;
                allocator = other.allocator;

                // Nullify source so it doesn't destroy them
                other.handle = VK_NULL_HANDLE;
                other.allocation = VK_NULL_HANDLE;
            }
            return *this;
        }

        // Disable Copying (Prevents Double-Free)
        VulkanBuffer(const VulkanBuffer&) = delete;
        VulkanBuffer& operator=(const VulkanBuffer&) = delete;

        // Helper to manually destroy if needed (rare)
        void destroy() {
            if (handle != VK_NULL_HANDLE && allocator != nullptr) {
                vmaDestroyBuffer(allocator, handle, allocation);
                handle = VK_NULL_HANDLE;
                allocation = VK_NULL_HANDLE;
            }
        }
    };

    // ========================================================================
    // RAII WRAPPER: IMAGE
    // ========================================================================
    struct VulkanImage {
        VkImage handle = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE; // Optional: Can store view here too!
        VmaAllocator allocator = nullptr;
        VkDevice device = VK_NULL_HANDLE;  // Needed to destroy View

        VulkanImage() = default;

        VulkanImage(VmaAllocator allocator, VkDevice device, uint32_t width, uint32_t height, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) 
            : allocator(allocator), device(device) {
            
            // A. Create Image
            VkImageCreateInfo imageInfo{};
            imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.extent.width = width;
            imageInfo.extent.height = height;
            imageInfo.extent.depth = 1;
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.format = format;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageInfo.usage = usage;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

            VmaAllocationCreateInfo allocInfo = {};
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

            if (vmaCreateImage(allocator, &imageInfo, &allocInfo, &handle, &allocation, nullptr) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create RAII Image!");
            }

            // B. Create View
            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = handle;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = aspect;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
            // CATCH THE LEAK: Manually destroy the image before throwing
            vmaDestroyImage(allocator, handle, allocation); 
            throw std::runtime_error("Failed to create RAII Image View!");
            }
        }

        ~VulkanImage() {
            destroy();
        }

        // Move Logic
        VulkanImage(VulkanImage&& other) noexcept { *this = std::move(other); }
        VulkanImage& operator=(VulkanImage&& other) noexcept {
            if (this != &other) {
                destroy();
                handle = other.handle;
                allocation = other.allocation;
                view = other.view;
                allocator = other.allocator;
                device = other.device;

                other.handle = VK_NULL_HANDLE;
                other.allocation = VK_NULL_HANDLE;
                other.view = VK_NULL_HANDLE;
            }
            return *this;
        }

        void destroy() {
            if (view != VK_NULL_HANDLE) {
                vkDestroyImageView(device, view, nullptr);
                view = VK_NULL_HANDLE;
            }
            if (handle != VK_NULL_HANDLE && allocator != nullptr) {
                vmaDestroyImage(allocator, handle, allocation);
                handle = VK_NULL_HANDLE;
                allocation = VK_NULL_HANDLE;
            }
        }
    };
}