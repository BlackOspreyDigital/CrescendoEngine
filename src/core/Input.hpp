#ifndef INPUT_HPP
#define INPUT_HPP

#include <SDL2/SDL.h>

namespace Crescendo {
    class Input {
    public:
        static void Update();
        static bool IsKeyDown(SDL_Scancode key);
        static bool IsMouseButtonDown(int button);

        static int mouseX, mouseY;
        static int mouseRelX, mouseRelY;
        static int scrollY;

    private:
        static const Uint8* keyboardState;
        static Uint32 mouseState;
    };
}

#endif