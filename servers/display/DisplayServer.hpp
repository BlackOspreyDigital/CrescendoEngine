#pragma once
#include <SDL2/SDL.h>
#include <vulkan/vulkan.h>
#include <string>

namespace Crescendo {
    class DisplayServer {
    public:
        DisplayServer() : window(nullptr) {}
        ~DisplayServer() { shutdown(); } // Now it will find shutdown()
        
        bool initialize(const std::string& title, int w, int h);
        void poll_events(bool& running); // Match the .cpp signature
        void shutdown(); // Declare this clearly

        VkResult create_window_surface(VkInstance instance, VkSurfaceKHR* surface);
        SDL_Window* get_window() const { return window; }
        void get_window_size(int* w, int* h);

    private:
        SDL_Window* window;
    };
}