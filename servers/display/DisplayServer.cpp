#include "servers/display/DisplayServer.hpp"
#include <SDL2/SDL_vulkan.h>
#include "imgui_impl_sdl2.h"
#include <iostream>

namespace Crescendo {

    bool DisplayServer::initialize(const std::string& title, int width, int height) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
            std::cerr << "DisplayServer: SDL_Init Failed: " << SDL_GetError() << std::endl;
            return false;
        }

        window = SDL_CreateWindow(
            title.c_str(),
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height,
            SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
        );

        if (!window) {
            std::cerr << "DisplayServer: failed to create window: " << SDL_GetError() << std::endl;
            return false;
        }

        return true;
    }

    void DisplayServer::poll_events(bool& running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // Forward events to Imgui
            ImGui_ImplSDL2_ProcessEvent(&event);
            
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                // Future: trigger swapchain recreation here
            }
        }    
    }

    VkResult DisplayServer::create_window_surface(VkInstance instance, VkSurfaceKHR* surface) {
        if (!SDL_Vulkan_CreateSurface(window, instance, surface)) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        return VK_SUCCESS;
    }

    void DisplayServer::get_window_size(int* w, int* h) {
        SDL_Vulkan_GetDrawableSize(window, w, h);
    }

    void DisplayServer::shutdown() {
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
    }
}