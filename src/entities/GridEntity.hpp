#pragma once
#include "src/scene/BaseEntity.hpp"
#include <iostream>

class CGridEntity : public CBaseEntity {
public:
    void Spawn() override {
              
        this->className = "prop_grid";
        this->targetName = "EditorGrid";
        this->origin = {0.0f, 0.0f, 0.0f};
        
        std::cout << "[Entity] Grid spawned at (0,0,0)" << std::endl;
    }

    void Think(float deltaTime) override {
        
    }
};