#pragma once

#include <string> 

namespace Crescendo {

    class CBaseEntity;

    class Component {
    public:
        CBaseEntity* owner = nullptr;
        bool enabled = true;

        virtual ~Component() = default;

        virtual void Start() {}
        virtual void Update(float deltaTime) {}

        virtual std::string GetName() const = 0;
        virtual void DrawInspectorUI() {}
    };
}