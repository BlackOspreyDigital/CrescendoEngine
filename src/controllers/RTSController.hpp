#pragma once

#include <vector>
#include <string>
#include <glm/glm.hpp>
#include <imgui.h>
#include "servers/physics/PhysicsServer.hpp"
#include "servers/audio/AudioServer.hpp"
#include "servers/camera/Camera.hpp"
#include "servers/rendering/IRenderer.hpp"
#include "scene/Scene.hpp"

namespace Crescendo {

    enum class UnitType {
        Tank,          // Box (ground unit)
        Plane,         // Sphere (air unit)
        Engineer,      // White builder unit
        CommandMech,   // Supreme Commander Armored Command Unit (ACU)
        Factory,       // Command Center / HQ Structure
        LandFactory,   // Land Factory (builds Tanks & Engineers)
        AirFactory,    // Air Factory (builds Planes)
        Turret,        // Point Defense Tower Structure
        MassGen,       // Purple Mass Generator
        EnergyGen      // Yellow Energy Cylinder Generator
    };

    enum class Team {
        Player,
        Enemy
    };

    struct SkirmishSettings {
        int startingMass = 500;
        int startingEnergy = 500;
        int gameMode = 2; // 0 = Annihilation, 1 = HQ Assassination, 2 = Commander (SupCom FAF)
        int aiDifficulty = 1; // 0 = Easy (24s), 1 = Normal (16s), 2 = Brutal (9s)
        int initialTanks = 2; // Initial tanks per side
        int initialPlanes = 1; // Initial planes per side
    };

    enum class ProjectileType {
        TankShell,
        Rocket
    };

    struct RTSProjectile {
        bool active = false;
        CBaseEntity* entity = nullptr;
        ProjectileType type = ProjectileType::Rocket;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 targetPos = glm::vec3(0.0f);
        Team team = Team::Player;
        float damage = 35.0f;
        float life = 0.0f;
        float maxLife = 3.0f;
    };

    struct RTSExplosionSphere {
        bool active = false;
        CBaseEntity* entity = nullptr;
        glm::vec3 center = glm::vec3(0.0f);
        glm::vec3 offset = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 color = glm::vec3(1.0f, 0.9f, 0.1f);
        float age = 0.0f;
        float maxLife = 0.4f;
        float peakScale = 2.0f;
    };

    struct ResourceDepositNode {
        glm::vec3 position = glm::vec3(0.0f);
        CBaseEntity* entity = nullptr;
        bool occupied = false;
    };

    struct EngineerBuildTask {
        bool active = false;
        bool constructing = false;
        UnitType buildType = UnitType::MassGen;
        glm::vec3 buildPos = glm::vec3(0.0f);
        int engineerEntityIndex = -1;
        float progress = 0.0f;
        float totalTime = 60.0f; // 1 min (60s) for Mass Generator
        float sparkTimer = 0.0f;
    };

    struct PendingSpawn {
        UnitType type = UnitType::Tank;
        Team team = Team::Player;
        glm::vec3 pos = glm::vec3(0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
    };

    struct RTSUnit {
        int entityIndex = -1;
        CBaseEntity* entity = nullptr;
        UnitType type = UnitType::Tank;
        Team team = Team::Player;
        float health = 100.0f;
        float maxHealth = 100.0f;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 targetPosition = glm::vec3(0.0f);
        bool hasTarget = false;
        float speed = 6.0f;
        bool selected = false;
        float attackCooldown = 0.0f;
    };

    class RTSController {
    public:
        RTSController() = default;
        ~RTSController();

        void Initialize(Scene* scene, PhysicsServer* physicsServer, IRenderer* renderer, glm::vec3 spawnPos);
        void Update(float deltaTime, Scene* scene, PhysicsServer* physicsServer, AudioServer* audioServer, Camera* cam, IRenderer* renderer);
        void DrawHUD(ImVec2 viewportPos, ImVec2 viewportSize);
        void Cleanup();

        // Spawning & Orders
        void SpawnUnit(Scene* scene, PhysicsServer* physicsServer, UnitType type, Team team, glm::vec3 pos, glm::vec3 scale = glm::vec3(1.0f));
        void IssueMoveOrder(Scene* scene, glm::vec3 targetPos);
        void SelectUnitsInBox(Scene* scene, Camera* cam, glm::vec2 minScreen, glm::vec2 maxScreen);
        void SelectSingleUnit(Scene* scene, Camera* cam, glm::vec2 screenPos);
        void ClearSelection();
        glm::vec3 ScreenToGround(Camera* cam, glm::vec2 screenPos);
        bool WorldToScreen(Camera* cam, const glm::vec3& worldPos, glm::vec2& outScreenPos);

        void SpawnExplosionCluster(const glm::vec3& pos, float scaleMultiplier = 1.0f, int sphereCount = 6);
        void FireProjectile(ProjectileType type, Team team, const glm::vec3& startPos, const glm::vec3& targetPos, float damage);
        // Economy & Gamemode State
        float playerMass = 100.0f;
        float m_boostTimer = 0.0f; // 10-minute (600s) Mass Overdrive timer
        void ActivateBoost();
        float playerEnergy = 100.0f;
        float enemyMass = 100.0f;
        float enemyEnergy = 100.0f;
        int playerScore = 0;
        int enemyScore = 0;
        bool gameOver = false;
        void StartNewSkirmish();
        void DrawGameOver(ImVec2 viewportPos, ImVec2 viewportSize);
        void DrawSkirmishMenu(ImVec2 viewportPos, ImVec2 viewportSize);

        SkirmishSettings m_skirmishSettings;
        bool m_showSkirmishMenu = false;
        bool m_playerWon = false;
        float m_matchTime = 0.0f;
        IRenderer* m_renderer = nullptr;
        std::string gameMessage = "Rude RTS Initialized. Build Mass & Energy generators!";

    private:
        Scene* m_scene = nullptr;
        PhysicsServer* m_physicsServer = nullptr;
        std::vector<RTSUnit> m_units;
        int m_cubeMeshIndex = -1;
        int m_sphereMeshIndex = -1;
        int m_cylinderMeshIndex = -1;
        int m_ringMeshIndex = -1;
        std::vector<CBaseEntity*> m_selectionRingEntities;
        
        // RTS Camera
        glm::vec3 m_cameraTarget = glm::vec3(-40.0f, 0.0f, 0.0f);
        float m_cameraZoom = 70.0f;
        float m_cameraPitch = 55.0f;
        float m_cameraYaw = 0.0f;

        // Viewport & Projection
        ImVec2 m_viewportPos = ImVec2(0.0f, 0.0f);
        ImVec2 m_viewportSize = ImVec2(1280.0f, 720.0f);
        Camera* m_activeCam = nullptr;
        bool m_wasRightMouseDown = false;

        // Rally Points
        glm::vec3 m_playerRallyPos = glm::vec3(-50.0f, 0.0f, 0.0f);
        glm::vec3 m_enemyRallyPos = glm::vec3(50.0f, 0.0f, 0.0f);
        CBaseEntity* m_playerRallyEntity = nullptr;
        CBaseEntity* m_enemyRallyEntity = nullptr;

        // Selection Box
        bool m_isDraggingBox = false;
        glm::vec2 m_dragStartScreen = glm::vec2(0.0f);
        glm::vec2 m_dragEndScreen = glm::vec2(0.0f);

        // AI & Economy Timers
        float m_aiTimer = 0.0f;
        float m_resourceTimer = 0.0f;
        float m_enemyEngineerTimer = 0.0f;

        // Right-Click Drag Line Formation
        bool m_isDraggingFormation = false;
        glm::vec2 m_formationStartScreen = glm::vec2(0.0f);
        glm::vec2 m_formationEndScreen = glm::vec2(0.0f);
        glm::vec3 m_formationStartWorld = glm::vec3(0.0f);
        glm::vec3 m_formationEndWorld = glm::vec3(0.0f);

        // Combat: Projectiles & Explosions
        std::vector<RTSProjectile> m_projectiles;
        std::vector<RTSExplosionSphere> m_explosionSpheres;
        std::vector<CBaseEntity*> m_deadUnitEntities;

        // Resource Deposit Nodes & Construction
        std::vector<ResourceDepositNode> m_resourceNodes;
        bool m_isPlacingBuilding = false;
        UnitType m_placingBuildingType = UnitType::LandFactory;
        CBaseEntity* m_buildGhostEntity = nullptr;
        CBaseEntity* m_buildBeamEntity = nullptr;
        EngineerBuildTask m_engineerTask;
        std::vector<PendingSpawn> m_pendingSpawns;
    };

}
