#include "core/Input.hpp"
#include <cstring> // Required for memcpy
#include <imgui.h>
namespace Crescendo {

    // Allocate memory for static variables
    const Uint8* Input::keyboardState = nullptr;
    Uint8 Input::previousKeyboardState[SDL_NUM_SCANCODES] = {0};
    Uint32 Input::mouseState = 0;

    int Input::mouseX = 0;
    int Input::mouseY = 0;
    int Input::mouseRelX = 0;
    int Input::mouseRelY = 0;
    int Input::scrollY = 0;

    void Input::Update() {
        // 1. If we already have a keyboard state from the last frame, 
        // copy it into our previousState buffer before updating!
        if (keyboardState != nullptr) {
            std::memcpy(previousKeyboardState, keyboardState, SDL_NUM_SCANCODES);
        }

        // 2. Grab the latest keyboard state from SDL
        keyboardState = SDL_GetKeyboardState(NULL);
        
        // 3. Retrieve mouse position and relative motion
        SDL_GetMouseState(&mouseX, &mouseY);
        mouseState = SDL_GetRelativeMouseState(&mouseRelX, &mouseRelY);
        if (ImGui::GetCurrentContext() != nullptr) {
            scrollY = (int)ImGui::GetIO().MouseWheel;
        }
    }
    bool Input::IsKeyDown(SDL_Scancode key) {
        return keyboardState && keyboardState[key];
    }

    // True ONLY on the exact frame the key goes from up (0) to down (1)
    bool Input::GetKeyDown(SDL_Scancode key) {
        if (!keyboardState) return false;
        return keyboardState[key] && !previousKeyboardState[key];
    }

    bool Input::IsMouseButtonDown(int button) {
        return (mouseState & SDL_BUTTON(button));
    }

    void Input::LockMouse() {
        SDL_SetRelativeMouseMode(SDL_TRUE);
    }
    
    void Input::UnlockMouse() {
        SDL_SetRelativeMouseMode(SDL_FALSE);
    }
}