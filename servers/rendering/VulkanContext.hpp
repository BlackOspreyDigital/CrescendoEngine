#include <vulkan/vulkan.h>
#include <vector>

namespace Crescendo {
    class VulkanContext {
    public:
        void Init(SDL_Window* window);
        void Cleanup();

    private:
        VkInstance instance;
        VkPhysicalDevice phsycialDevice = VK_NULL_HANDLE;
        VKDevice device;
        VkSurfaceKHR surface;
        // tons to add later
    };
}