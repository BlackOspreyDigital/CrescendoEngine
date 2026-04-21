#include "SceneManager.hpp"
#include "IO/SceneSerializer.hpp"
#include <algorithm> 

namespace Crescendo {

void SceneManager::Initialize() {
    // Start with a blank slate
    auto defaultScene = CreateScene("Default Level");
    SetActiveScene(defaultScene);
}

std::shared_ptr<Scene> SceneManager::CreateScene(const std::string& name) {
    auto newScene = std::make_shared<Scene>();
    newScene->name = name;
    openScenes.push_back(newScene);
    return newScene;
}

// Ensure your constructor stores the renderer pointer if it doesn't already
SceneManager::SceneManager(RenderingServer* renderer) : rendererRef(renderer) {}

std::shared_ptr<Scene> SceneManager::LoadScene(const std::string& filepath) {
    auto newScene = std::make_shared<Scene>();
    
    // Extract a name from the filepath for the tab
    size_t slashPos = filepath.find_last_of("/\\");
    size_t dotPos = filepath.find_last_of('.');
    if (slashPos != std::string::npos && dotPos != std::string::npos) {
        newScene->name = filepath.substr(slashPos + 1, dotPos - slashPos - 1);
    } else {
        newScene->name = "Loaded Scene";
    }

    // FIX: SceneSerializer requires (Scene*, RenderingServer*)
    SceneSerializer serializer(newScene.get(), rendererRef); 
    if (serializer.Deserialize(filepath)) {
        openScenes.push_back(newScene);
        return newScene;
    }
    return nullptr;
}

bool SceneManager::SaveScene(std::shared_ptr<Scene> scene, const std::string& filepath) {
    if (!scene) return false;
    // FIX: Again, provide the renderer reference
    SceneSerializer serializer(scene.get(), rendererRef);
    return serializer.Serialize(filepath);
}

void SceneManager::SetActiveScene(std::shared_ptr<Scene> scene) {
    if (scene) {
        activeScene = scene;
    }
}

std::shared_ptr<Scene> SceneManager::GetActiveScene() const {
    return activeScene;
}

const std::vector<std::shared_ptr<Scene>>& SceneManager::GetOpenScenes() const {
    return openScenes;
}

void SceneManager::CloseScene(std::shared_ptr<Scene> scene) {
    auto it = std::find(openScenes.begin(), openScenes.end(), scene);
    if (it != openScenes.end()) {
        openScenes.erase(it);
        // If we closed the active scene, fall back to another one or create a new blank one
        if (activeScene == scene) {
            if (!openScenes.empty()) {
                activeScene = openScenes.front();
            } else {
                Initialize();
            }
        }
    }
}

void SceneManager::InstantiatePrefab(std::shared_ptr<Scene> prefabScene, const glm::vec3& position) {
    if (!activeScene || !prefabScene) return;

    // A simple copy routine. You will iterate over the prefab's entities
    // and duplicate them into the activeScene, applying the position offset.
    for (const auto& entity : prefabScene->entities) {
        CBaseEntity* newEnt = activeScene->CreateEntity(entity->targetName);
        newEnt->origin = entity->origin + glm::dvec3(position);
        newEnt->angles = entity->angles;
        newEnt->scale = entity->scale;
        
        // Note: You will need to write a deep copy function for components here 
        // depending on how the ECS stores component data.
    }
}

} 