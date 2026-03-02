#pragma once

#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>


namespace Crescendo {
    class VulkanContext {
    public:
        void Init(SDL_Window* window);
        void Cleanup();

    private:
        VkInstance instance;
        VkPhysicalDevice phsycialDevice = VK_NULL_HANDLE;
        VkDevice device;
        VkSurfaceKHR surface;
        
    };
}