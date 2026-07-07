#pragma once
#include <SDL2/SDL.h> // Or <SDL2/SDL.h> depending on your setup

namespace Crescendo {

    class Input {
    public:
        static void Update();
        
        // Continuous press (for WASD walking, automatic fire)
        static bool IsKeyDown(SDL_Scancode key);
        
        // Single-frame press (for UI navigation, jumping, toggling menus)
        static bool GetKeyDown(SDL_Scancode key);
        
        static bool IsMouseButtonDown(int button);

        static int mouseX;
        static int mouseY;
        static int mouseRelX;
        static int mouseRelY;
        static int scrollY;

    private:
        static const Uint8* keyboardState;
        
        // Buffer to remember what the keyboard looked like on the previous frame
        static Uint8 previousKeyboardState[SDL_NUM_SCANCODES];
        static Uint32 mouseState;
    };
}