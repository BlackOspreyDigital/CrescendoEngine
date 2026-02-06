#pragma once
#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <iostream>
#include <string>

#include "scene/BaseEntity.hpp"
#include "scene/CarController.hpp"

namespace Crescendo {

    class ScriptSystem {
    public:
        sol::state lua;
        
        std::unordered_map<std::string, sol::protected_function> scriptCache;

        void Initialize() {
            // 1. Open standard Lua libraries
            lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::package, sol::lib::string, sol::lib::table);

            // 2. Bind GLM Vector3
            // This lets Lua understand: entity.origin = Vec3.new(0, 10, 0)
            lua.new_usertype<glm::vec3>("Vec3",
                // Constructors
                sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
                // Data members
                "x", &glm::vec3::x,
                "y", &glm::vec3::y,
                "z", &glm::vec3::z,
                // Operator overloads (optional but nice)
                sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
                sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
                sol::meta_function::multiplication, [](const glm::vec3& a, float f) { return a * f; }
            );

            // 3. Bind CBaseEntity
            // Expose the properties you want scripts to control
            lua.new_usertype<CBaseEntity>("Entity",
                "origin", &CBaseEntity::origin,
                "angles", &CBaseEntity::angles,
                "scale",  &CBaseEntity::scale,
                "visible",&CBaseEntity::visible,
                "SetScript", &CBaseEntity::SetScript
            );

            // 4. Bind CarController (Legacy/Specialized support)
            lua.new_usertype<CarController>("CarController",
                "engineTorque", &CarController::engineTorque,
                "brakeForce",   &CarController::brakeForce,
                "SetInput",     &CarController::SetDriverInput, 
                "GetSpeed",     &CarController::GetSpeedKmH
            );

            std::cout << "[ScriptSystem] Lua bindings initialized." << std::endl;

            // 5. Bind SYSTEM
            auto input = lua.create_named_table("Input");

            input.set_function("IsKeyDown", [](int scancode) -> bool {
                const Uint8* state = SDL_GetKeyboardState(NULL);
                if (scancode >= 0 && scancode < SDL_NUM_SCANCODES) {
                    return state[scancode] != 0;
                }
                return false;
            });

            // 6. constants to memorize numbers in lua
            lua["KEY_W"] = SDL_SCANCODE_W;
            lua["KEY_S"] = SDL_SCANCODE_S;
            lua["KEY_A"] = SDL_SCANCODE_A;
            lua["KEY_D"] = SDL_SCANCODE_D;
            lua["KEY_SPACE"] = SDL_SCANCODE_SPACE;
            lua["KEY_SHIFT"] = SDL_SCANCODE_LSHIFT;
        }

        // Helper: Loads a file, compiles it, and stores it in cache
        void LoadScript(const std::string& path) {
            if (scriptCache.find(path) != scriptCache.end()) return;

            sol::load_result script = lua.load_file(path);
            if (!script.valid()) {
                sol::error err = script;
                std::cerr << "[Lua Load Error] " << path << ": " << err.what() << std::endl;
                return;
            }
            
            // Convert load result to a protected function and store it
            scriptCache[path] = script;
        }

        // The Magic Function: Runs a script on a generic entity
        void RunEntityScript(CBaseEntity* entity, float dt) {
            if (!entity || !entity->hasScript) return;

            // Ensure loaded
            if (scriptCache.find(entity->scriptPath) == scriptCache.end()) {
                LoadScript(entity->scriptPath);
            }

            // Get the chunk
            auto& scriptChunk = scriptCache[entity->scriptPath];
            if (scriptChunk.valid()) {
                // Execute the script chunk. 
                // We expect the script to RETURN a function that takes (self, dt).
                // Example Lua: return function(self, dt) ... end
                
                sol::protected_function_result result = scriptChunk();
                if (result.valid()) {
                    sol::function updateFunc = result;
                    // Call the returned function with our Entity pointer
                    auto runResult = updateFunc(entity, dt);
                    if (!runResult.valid()) {
                        sol::error err = runResult;
                        std::cerr << "[Lua Runtime Error] " << entity->scriptPath << ": " << err.what() << std::endl;
                    }
                }
            }
        }

        // Keep the Car Updater for your main vehicle
        void UpdateCar(CarController& car, float dt, bool w, bool s, bool a, bool d) {
            // This looks for a global "Update" function in the global state
            // (Used for single-instance scripts like the player car)
            sol::function updateFunc = lua["Update"];
            if (updateFunc.valid()) {
                updateFunc(car, dt, w, s, a, d);
            }
        }
    };
}