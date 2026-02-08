#pragma once
#include <string>
#include <vector>
#include "deps/json/json.hpp"
#include "scene/Scene.hpp"

using json = nlohmann::json;

namespace Crescendo {
    class Serializer {
    public:
        // save current state to JSON file
        static void SaveProject(Scene* scene, const std::string& path);

        // clear scene and load new entities from JSON file
        static void LoadProject(Scene* scene, const std::string& path);
    };
}