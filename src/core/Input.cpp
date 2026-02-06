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
        // SDL_GetKeyboardState returns a pointer to an internal SDL array
        keyboardState = SDL_GetKeyboardState(NULL);
        // SDL_GetMouseState updates X/Y returns bitmask for buttons
        mouseState = SDL_GetMouseState(&mouseX, &mouseY);
    }

    bool Input::IsKeyDown(SDL_Scancode key) {
        return keyboardState && keyboardState[key];
    }

    bool Input::IsMouseButtonDown(int button) {
        return (mouseState & SDL_BUTTON(button));
    }

}