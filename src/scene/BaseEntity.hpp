#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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

    CBaseEntity* moveParent = nullptr;
    std::vector<CBaseEntity*> children;

    int modelIndex = -1;
    int textureID = 0;
    bool visible = true;

    float roughness = 0.5f;
    float metallic = 0.0f;
    glm::vec3 albedoColor = {1.0f, 1.0f, 1.0f};

    static constexpr float SECTOR_SIZE = 1024.0f;

    virtual ~CBaseEntity() {}
    
    // think system

    virtual void Spawn() {}
    virtual void Think(float deltaTime) {}

    glm::vec3 GetRenderPosition(glm::ivec3 cameraSector, glm::vec3 cameraOrigin) {
        glm::vec3 sectorDiff = glm::vec3(sector - cameraSector);
        return (sectorDiff * SECTOR_SIZE) + (origin - cameraOrigin);
    }
};