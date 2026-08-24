#pragma once
#include <SDL2/SDL.h>

namespace Crescendo {

    class Input {
    public:
        static void Update();
        
        static bool IsKeyDown(SDL_Scancode key);
        static bool GetKeyDown(SDL_Scancode key);
        static bool IsMouseButtonDown(int button);

        // --- MOUSE CAPTURE CONTROL ---
        static void LockMouse();
        static void UnlockMouse();

        static int mouseX;
        static int mouseY;
        static int mouseRelX;
        static int mouseRelY;
        static int scrollY;

    private:
        static const Uint8* keyboardState;
        static Uint8 previousKeyboardState[SDL_NUM_SCANCODES];
        static Uint32 mouseState;
    };
}