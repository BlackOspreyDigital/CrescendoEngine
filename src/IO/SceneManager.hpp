#pragma once 

#include <vector>
#include <string>
#include <memory>

#include "scene/Scene.hpp"

namespace Crescendo {

class IRenderer;

class SceneManager {
public:

    SceneManager(IRenderer* renderer);
    
    ~SceneManager() = default;
    
    void Initialize();

    std::shared_ptr<Scene> CreateScene(const std::string& name);

    std::shared_ptr<Scene> LoadScene(const std::string& filepath);

    bool SaveScene(std::shared_ptr<Scene> scene, const std::string& filePath);

    void SetActiveScene(std::shared_ptr<Scene> scene);

    std::shared_ptr<Scene> GetActiveScene() const;

    const std::vector<std::shared_ptr<Scene>>& GetOpenScenes() const;

    void CloseScene(std::shared_ptr<Scene> scene);

    void InstantiatePrefab(std::shared_ptr<Scene> prefabScene, const glm::vec3& position);

    IRenderer* GetRenderer() const { return rendererRef; }
    

private:
    IRenderer* rendererRef = nullptr;
    std::vector<std::shared_ptr<Scene>> openScenes;
    std::shared_ptr<Scene> activeScene = nullptr;
};
}