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
        
        // Cache stores the "Update" function returned by the script, not just the file chunk
        std::unordered_map<std::string, sol::protected_function> scriptCache;

        void Initialize() {
            // 1. Open standard Lua libraries (Included 'os' to fix the crash)
            lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::package, sol::lib::string, sol::lib::table, sol::lib::os);

            // 2. Bind GLM Vector3
            lua.new_usertype<glm::vec3>("Vec3",
                sol::constructors<glm::vec3(), glm::vec3(float, float, float)>(),
                "x", &glm::vec3::x,
                "y", &glm::vec3::y,
                "z", &glm::vec3::z,
                sol::meta_function::addition, [](const glm::vec3& a, const glm::vec3& b) { return a + b; },
                sol::meta_function::subtraction, [](const glm::vec3& a, const glm::vec3& b) { return a - b; },
                sol::meta_function::multiplication, [](const glm::vec3& a, float f) { return a * f; }
            );

            // 3. Bind Entity (Crucial for 'this.origin' to work)
            lua.new_usertype<CBaseEntity>("Entity",
                "origin", &CBaseEntity::origin,
                "angles", &CBaseEntity::angles,
                "scale", &CBaseEntity::scale,
                "hasScript", &CBaseEntity::hasScript
            );
            
            std::cout << "[ScriptSystem] Lua bindings initialized." << std::endl;
        }

        void LoadScript(const std::string& path) {
            // Load the file
            sol::load_result loadRes = lua.load_file(path);
            if (!loadRes.valid()) {
                sol::error err = loadRes;
                std::cerr << "Failed to load script file: " << path << "\n" << err.what() << std::endl;
                return;
            }

            // Execute the file ONCE. 
            // We expect the script to return a function: "return function(this, dt) ... end"
            sol::protected_function scriptFile = loadRes;
            sol::protected_function_result result = scriptFile();
            
            if (result.valid()) {
                // The result is the update function we want to call every frame
                sol::protected_function updateFunc = result;
                scriptCache[path] = updateFunc;
            } else {
                sol::error err = result;
                std::cerr << "Script did not return a valid function: " << path << "\n" << err.what() << std::endl;
            }
        }

        void RunEntityScript(CBaseEntity* entity, float dt) {
            if (!entity || entity->scriptPath.empty()) return;

            // Check if we have the update function cached
            if (scriptCache.find(entity->scriptPath) == scriptCache.end()) {
                LoadScript(entity->scriptPath);
            }

            // Retrieve the cached function
            if (scriptCache.find(entity->scriptPath) != scriptCache.end()) {
                sol::protected_function& updateFunc = scriptCache[entity->scriptPath];
                
                // Call it with (this, dt)
                auto result = updateFunc(entity, dt);
                
                if (!result.valid()) {
                    sol::error err = result;
                    std::cerr << "[Lua Runtime Error] " << entity->scriptPath << ": " << err.what() << std::endl;
                    
                    // Disable script to prevent console spam / recurring crashes
                    entity->hasScript = false;
                }
            }
        }

        // Keep the Car Updater for your main vehicle
        void UpdateCar(CarController& car, float dt, bool w, bool s, bool a, bool d) {
            sol::function updateFunc = lua["Update"];
            if (updateFunc.valid()) {
                updateFunc(car, dt, w, s, a, d);
            }
        }
    };
}