#include "core/Input.hpp"

namespace Crescendo {

    // These lines allocate memory for vars
    const Uint8* Input::keyboardState = nullptr;
    Uint32 Input::mouseState = 0;

    int Input::mouseX = 0;
    int Input::mouseY = 0;
    int Input::mouseRelX = 0;
    int Input::mouseRelY = 0;
    int Input::scrollY = 0;

    void Input::Update() {
        keyboardState = SDL_GetKeyboardState(NULL);
        
        // [FIX] Get Relative Mouse State for Camera Look
        // This function retrieves the motion since the last check
        mouseState = SDL_GetRelativeMouseState(&mouseRelX, &mouseRelY);
        
        // If you need absolute position too:
        // SDL_GetMouseState(&mouseX, &mouseY);
    }

    bool Input::IsKeyDown(SDL_Scancode key) {
        return keyboardState && keyboardState[key];
    }

    bool Input::IsMouseButtonDown(int button) {
        return (mouseState & SDL_BUTTON(button));
    }

}