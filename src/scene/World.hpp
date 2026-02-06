#pragma once
#include <vector>
#include "scene/GameObject.hpp"

namespace Crescendo {
    class World {
    public:
        std::vector<GameObject> objects;
        
        // Helper to spawn things easily
        GameObject& createObject(const std::string& name, int meshID) {
            objects.emplace_back(name, meshID);
            return objects.back();
        }

        void removeObject(int index) {
            if (index >= 0 && static_cast<size_t>(index) < objects.size()) {
                objects.erase(objects.begin() + index);
                
            }
        }
    };
}