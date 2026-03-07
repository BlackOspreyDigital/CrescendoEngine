#pragma once
#include <string>
#include <vector>
#include <memory>
#include "Component.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include "servers/camera/Camera.hpp"

namespace Crescendo {

class CBaseEntity;

class CBaseEntity {
public:
    
    int index = -1;
    std::string className;
    std::string targetName;

    std::string scriptPath = "";
    bool hasScript = false;

    void SetScript(const std::string& path) {
        scriptPath = path;
        hasScript = !path.empty();
    }

    glm::ivec3 sector = {0, 0, 0};
    glm::vec3 origin = {0.0f, 0.0f, 0.0f};
    glm::vec3 angles = {0.0f, 0.0f, 0.0f};
    glm::vec3 scale  = {1.0f, 1.0f, 1.0f};

    glm::vec3 savedOrigin = glm::vec3(0.0f);
    glm::vec3 savedAngles = glm::vec3(0.0f);
    glm::vec3 savedScale = glm::vec3(1.0f);

    std::vector<Crescendo::Camera> cameras;
    
    int activeCameraIndex = -1; 
    // Helper to add a camera and make it active
    void AddCamera(const Crescendo::Camera& cam) {
        cameras.push_back(cam);
        if (activeCameraIndex == -1) activeCameraIndex = 0;
    }
    
    Crescendo::Camera* GetActiveCamera() {
        if (activeCameraIndex >= 0 && activeCameraIndex < static_cast<int>(cameras.size())) {
            return &cameras[activeCameraIndex];
        }
        return nullptr;
    }

    CBaseEntity* moveParent = nullptr;
    std::vector<CBaseEntity*> children;
    
    int modelIndex = -1;
    int textureID = 0;
    int normalTextureID = 0;
    int ormTextureID = 0;    // NEW: Occlusion, Roughness, Metallic Map
    bool visible = true;
    std::string modelPath = "";
    std::string assetPath = "";

    // [BSDF DEFAULTS]
    float roughness = 0.0f;
    float metallic = 0.0f;
    float emission = 0.0f;
    float normalStrength = 0.0f; // default to 0 to assume no normal map
    float transmission = 0.0f; // 0.0 = Opaque, 1.0 = glass
    float thickness = 0.0f; // average thickness
    float attenuationDistance = 1.0f; // Distance at which color is fully absorbed
    float ior = 1.5f;
    glm::vec3 attenuationColor = {1.0f, 1.0f, 1.0f}; // The color of the glass
    glm::vec3 albedoColor = {1.0f, 1.0f, 1.0f};   
    // --- Advanced PBR Sliders ---
    float clearcoat = 0.0f;
    float clearcoatRoughness = 0.03f;
    float sheen = 0.0f;
    float specularWeight = 0.5f;

    static constexpr float SECTOR_SIZE = 1024.0f;

    virtual ~CBaseEntity() {}
    
    // think system

    virtual void Spawn() {}
    virtual void Think(float deltaTime) {}

    // --- THE COMPONENT SYSTEM ---
    std::vector<std::unique_ptr<Component>> components;

    template<typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        T* ptr = comp.get();
        comp->owner = this;
        components.push_back(std::move(comp));
        return ptr;
    }

    template<typename T>
    T* GetComponent() {
        for (auto& comp : components) {
            T* ptr = dynamic_cast<T*>(comp.get());
            if (ptr) return ptr;
        }
        return nullptr;
    }

    template<typename T>
    bool HasComponent() {
        return GetComponent<T>() != nullptr;
    }

    glm::vec3 GetRenderPosition(glm::ivec3 cameraSector, glm::vec3 cameraOrigin) {
        glm::vec3 sectorDiff = glm::vec3(sector - cameraSector);
        return (sectorDiff * SECTOR_SIZE) + (origin - cameraOrigin);
    }
};
}