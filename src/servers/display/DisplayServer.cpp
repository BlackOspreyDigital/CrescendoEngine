#include "servers/display/DisplayServer.hpp"
#ifndef __EMSCRIPTEN__
#include <SDL2/SDL_vulkan.h>
#endif
#include "imgui_impl_sdl2.h"
#include <iostream>

namespace Crescendo {

    bool DisplayServer::initialize(const std::string& title, int width, int height) {
        // 1. MUST BE BEFORE SDL_Init TO AFFECT SDL3/WAYLAND COMPAT LAYER
        SDL_SetHint("SDL_VIDEO_WAYLAND_ALLOW_TEARING", "0");
        
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) < 0) {
            std::cerr << "DisplayServer: SDL_Init Failed: " << SDL_GetError() << std::endl;
            return false;
        }

        // Define base flags
        uint32_t window_flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN;

        // Add Vulkan flag ONLY if we are NOT on the web
#ifndef __EMSCRIPTEN__
        window_flags |= SDL_WINDOW_VULKAN;
#endif
        
        window = SDL_CreateWindow(
            title.c_str(),
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width, height,
            window_flags
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
        // NEW: Only forward to ImGui if the context and backend exist
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui_ImplSDL2_ProcessEvent(&event);
        }
        
        if (event.type == SDL_QUIT) {
            running = false;
        }
    }    
}

    VkResult DisplayServer::create_window_surface(VkInstance instance, VkSurfaceKHR* surface) {
#ifndef __EMSCRIPTEN__
        if (!SDL_Vulkan_CreateSurface(window, instance, surface)) {
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        return VK_SUCCESS;
#else
        // This function shouldn't be called in the WebGPU backend
        return VK_SUCCESS; 
#endif
    }

    void DisplayServer::get_window_size(int* w, int* h) {
#ifndef __EMSCRIPTEN__
        SDL_Vulkan_GetDrawableSize(window, w, h);
#else
        SDL_GetWindowSize(window, w, h);
#endif
    }

    void DisplayServer::shutdown() {
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        SDL_Quit();
    }
}