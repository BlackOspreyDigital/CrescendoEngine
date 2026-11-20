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
#include "controllers/RTSFactions.hpp"
#include "controllers/RTSAIPlanner.hpp"

namespace Crescendo {

    enum class UnitType {
        Tank,          // Box (ground unit)
        Plane,         // Sphere (air unit)
        Engineer,      // White builder unit
        CommandMech,   // Supreme Commander Armored Command Unit (ACU)
        Factory,       // Command Center / HQ Structure
        LandFactory,   // Land Factory (builds Tanks & Engineers)
        AirFactory,    // Air Factory (builds Planes)
        Turret,        // Point Defense Tower (Ground Cannon)
        AATurret,      // T1 Anti-Air Defense Turret (Anti-Air Flak)
        MassGen,       // Purple Mass Generator
        EnergyGen,     // Yellow Energy Cylinder Generator
        ShieldGen,     // Mobile Shield Generator / Force-Field Dome
        MassStorage,   // Reinforced Mass Storage Silo (+5000 M cap)
        EnergyStorage, // High-Voltage Energy Storage Bank (+15000 E cap)
        NukeSilo,      // Strategic Missile Defense / Nuke Silo
        SupportMech    // T3 Combat Engineer Support Mech
    };

    enum class AIPersonality {
        Normal = 0, // Balanced expansion, mixed army, tactical push
        Rush = 1,   // Aggressive early combat spam, direct assault, low thresholds
        Turtle = 2  // Defensive choke fortification, Point Defense Turrets & Force-Field Shields
    };

    enum class Team {
        Player,
        Enemy
    };

    struct SkirmishSettings {
        int startingMass = 500;
        int startingEnergy = 500;
        int gameMode = 2; // 0 = Annihilation, 1 = HQ Assassination, 2 = Commander (SupCom FAF)
        int aiDifficulty = 1; // Judgment quality, not production speed
        int initialTanks = 2; // Initial tanks per side
        int initialPlanes = 1; // Initial planes per side
        int noRushMinutes = 0; // 0 = Off, 5, 10, 15, 20 Min
        bool enableFogOfWar = true; // Fog of War on/off
        int aiPersonality = 0; // 0 = Normal, 1 = Rush, 2 = Turtle
        uint32_t aiSeed = 1; // Reproducible doctrine choices
        FactionId playerFaction = FactionId::Vanguard;
        FactionId enemyFaction = FactionId::Syndicate;
    };

    enum class ProjectileType {
        TankShell,
        Rocket,
        AAFlak,
        StrategicNuke,
        EMPPulse
    };

    struct RTSProjectile {
        bool active = false;
        CBaseEntity* entity = nullptr;
        ProjectileType type = ProjectileType::Rocket;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 targetPos = glm::vec3(0.0f);
        Team team = Team::Player;
        FactionId faction = FactionId::Vanguard;
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

    enum class ResourceNodeType {
        Mass,
        Energy
    };

    struct ResourceDepositNode {
        glm::vec3 position = glm::vec3(0.0f);
        CBaseEntity* entity = nullptr;
        bool occupied = false;
        int reservedBy = -1;
        ResourceNodeType type = ResourceNodeType::Mass;
    };

    struct RTSTree {
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
        float rotation = 0.0f;
        int variant = 0;
        float windPhase = 0.0f;
        glm::vec3 tint = glm::vec3(1.0f);
    };
    struct EngineerBuildTask {
        Team team = Team::Player;
        bool constructing = false;
        UnitType buildType = UnitType::MassGen;
        glm::vec3 buildPos = glm::vec3(0.0f);
        int engineerEntityIndex = -1;
        float progress = 0.0f;
        float totalTime = 55.0f;
        float sparkTimer = 0.0f;
        int massCost = 0;
        int energyCost = 0;
        int nodeIndex = -1;
    };

    struct PendingSpawn {
        UnitType type = UnitType::Tank;
        Team team = Team::Player;
        FactionId faction = FactionId::Vanguard;
        glm::vec3 pos = glm::vec3(0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
    };
    struct FactoryBuildOrder {
        UnitType type = UnitType::Tank;
        Team team = Team::Player;
        FactionId faction = FactionId::Vanguard;
        float buildTime = 24.0f;
        float progress = 0.0f;
        int massCost = 80;
        int energyCost = 60;
    };

    struct FactoryQueue {
        int factoryEntityIndex = -1;
        Team team = Team::Player;
        FactionId faction = FactionId::Vanguard;
        std::vector<FactoryBuildOrder> orders;
        float sparkTimer = 0.0f;
    };


    struct RTSUnit {
        int entityIndex = -1;
        CBaseEntity* entity = nullptr;
        UnitType type = UnitType::Tank;
        Team team = Team::Player;
        FactionId faction = FactionId::Vanguard;
        float health = 100.0f;
        float maxHealth = 100.0f;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 targetPosition = glm::vec3(0.0f);
        bool hasTarget = false;
        float speed = 6.0f;
        bool selected = false;
        float attackCooldown = 0.0f;
        bool inCombat = false;
        bool fleeing = false;
        bool scoutedByPlayer = false;
        AISquadRole aiRole = AISquadRole::Unassigned;
        glm::vec3 baseScale = glm::vec3(1.0f);

        // Force-Field Shield Properties
        float shieldHealth = 0.0f;
        float maxShieldHealth = 0.0f;
        float shieldRadius = 0.0f;
        float shieldRechargeTimer = 0.0f; // 120s (2 minutes) to recharge from collapse
        bool isShieldActive = false;
        CBaseEntity* shieldOrbEntity = nullptr;
        CBaseEntity* doorEntity = nullptr;
        float doorCloseProgress = 0.0f; // 0.0 = open (under map), 1.0 = closed (sealing bay during build)
        float doorReleaseTimer = 0.0f;  // Hold door open while newly built unit exits
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

        // Faction System Helpers
        FactionId GetTeamFaction(Team team) const;
        const FactionDef& GetTeamFactionDef(Team team) const;
        glm::vec3 GetTeamDisplayColor(Team team, bool accent = false) const;
        glm::vec3 GetTeamShieldColor(Team team) const;
        void GetUnitBuildCost(UnitType type, FactionId faction, int& outMass, int& outEnergy, float& outBuildTime) const;
        std::string GetMatchupLabel() const;
        // Economy & Gamemode State
        float playerMass = 100.0f;
        float m_boostTimer = 0.0f; // Explicit 90-second player Mass Overdrive
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
        void DrawMinimap(ImVec2 viewportPos, ImVec2 viewportSize);
        void DrawBuildPanel(ImVec2 viewportPos, ImVec2 viewportSize);
        void DrawSelectedUnitsBar(ImVec2 viewportPos, ImVec2 viewportSize);
        void DrawSettingsMenu(ImVec2 viewportPos, ImVec2 viewportSize);
        void DrawViewportOverlays(ImVec2 viewportPos, ImVec2 viewportSize, ImDrawList* customDrawList = nullptr);
        bool IsScreenPosOverHUD(glm::vec2 pos) const;
        void DrawUnitHealthBars(Camera* cam, ImDrawList* drawList = nullptr);
        void DrawGroupSelectionHulls(Camera* cam, ImDrawList* drawList = nullptr);
        bool m_overlaysDrawnThisFrame = false;
        void QueueFactoryUnit(UnitType type, Team team, int specificFactoryIndex = -1);
        float m_noRushTimer = 0.0f;
        bool m_showSettingsMenu = false;
        float m_masterVolume = 0.65f;
        float m_sfxVolume = 0.8f;
        int m_selectedResolution = 0;

        // Procedural Terrain & Forge Mode (Map Sculptor)
        static constexpr int TERRAIN_GRID_RES = 128;
        static constexpr float TERRAIN_MAP_SIZE = 1000.0f;
        std::vector<float> m_heightmap;
        std::vector<Vertex> m_terrainVertices;
        std::vector<uint32_t> m_terrainIndices;
        int m_terrainMeshIndex = -1;
        CBaseEntity* m_terrainEntity = nullptr;

        bool m_inForgeMode = false;
        int m_forgeToolMode = 0; // 0=Sculpt Terrain, 1=Stamp Mass Node, 2=Stamp Power Node, 3=Delete Node
        int m_forgeBrushType = 0; // 0=Raise, 1=Lower, 2=Smooth, 3=Flatten, 4=Plateau/Terrace, 5=Ramp, 6=Noise/Roughen
        float m_forgeBrushRadius = 45.0f;
        float m_forgeBrushStrength = 1.2f;
        float m_forgeFlattenHeight = 20.0f;
        float m_forgePlateauStep = 8.0f;

        // Map Sculptor Undo / Redo
        std::vector<std::vector<float>> m_sculptUndoStack;
        std::vector<std::vector<float>> m_sculptRedoStack;
        bool m_isSculptStrokeActive = false;
        void PushSculptUndoState();
        void SculptUndo();
        void SculptRedo();
        // Trees & Instanced Foliage Management
        std::vector<size_t> m_treeBatchIndices;
        std::vector<RTSTree> m_trees;
        bool m_foliageDirty = false;
        void SyncFoliageBatches();
        void SpawnTree(const glm::vec3& pos, float scale = 1.0f, float rot = 0.0f, int variant = -1);
        void ClearAllTrees();
        void UpdateTreesZ();
        void ScatterProceduralForest(int density = 150);

        // Battlefield Water Simulation
        bool m_enableWater = false;
        float m_waterHeight = 0.0f;
        int m_waterMeshIndex = -1;
        CBaseEntity* m_waterEntity = nullptr;
        void UpdateWaterPlane();

        // Custom Map Save & Load
        char m_customMapName[64] = "custom_battleground";
        int m_selectedCustomMapIndex = 0;
        std::vector<std::string> m_availableCustomMaps;
        void RefreshAvailableCustomMaps();
        bool SaveCustomMap(const std::string& mapName);
        bool LoadCustomMap(const std::string& mapName);

        void UpdateResourceNodesZ();
        void SpawnResourceNode(glm::vec3 worldPos, ResourceNodeType type);
        void ResetDefaultResourceNodes();
        float GetTerrainHeight(float worldX, float worldY) const;
        glm::vec3 GetTerrainNormal(float worldX, float worldY) const;
        void BuildProceduralTerrain(IRenderer* renderer, Scene* scene);
        void RecomputeTerrainNormals(int gx0 = 0, int gx1 = TERRAIN_GRID_RES, int gy0 = 0, int gy1 = TERRAIN_GRID_RES);
        void ApplyTerrainBrush(glm::vec3 worldPos, float radius, float strength, int brushType, float targetHeight = 0.0f);
        void GenerateProceduralTerrain(int preset);
        void DrawForgeUI(ImVec2 viewportPos, ImVec2 viewportSize);
        SkirmishSettings m_skirmishSettings;
        bool m_showSkirmishMenu = true;
        bool m_inMatch = false;
        bool m_playerWon = false;
        float m_matchTime = 0.0f;
        IRenderer* m_renderer = nullptr;
        bool m_showDebugTelemetry = false;
        std::string gameMessage = "Rude RTS Initialized. Build Mass & Energy generators!";
    private:
        Scene* m_scene = nullptr;
        PhysicsServer* m_physicsServer = nullptr;
        std::vector<RTSUnit> m_units;
        int m_cubeMeshIndex = -1;
        int m_sphereMeshIndex = -1;
        int m_cylinderMeshIndex = -1;
        int m_ringMeshIndex = -1;
        int m_acuMeshIndex = -1;
        int m_tankMeshIndex = -1;
        int m_gunshipMeshIndex = -1;
        int m_landFactoryMeshIndex = -1;
        int m_landFactoryDoorMeshIndex = -1;
        int m_airFactoryMeshIndex = -1;
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
        bool m_wasLeftMouseDown = false;

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
        struct AIContact {
            int entityIndex = -1;
            UnitType type = UnitType::Tank;
            glm::vec3 position = glm::vec3(0.0f);
            float lastSeen = 0.0f;
            float power = 0.0f;
            bool visible = false;
        };
        RTSAIPlanner m_aiPlanner;
        AIWorldState m_aiWorld;
        std::vector<AIContact> m_aiContacts;
        glm::vec3 m_aiHome = glm::vec3(0.0f);
        glm::vec3 m_aiForward = glm::vec3(-1.0f, 0.0f, 0.0f);
        glm::vec3 m_aiObjective = glm::vec3(0.0f);
        glm::vec3 m_aiStaging = glm::vec3(0.0f);
        glm::vec3 m_aiExpansion = glm::vec3(0.0f);
        glm::vec3 m_aiChoke = glm::vec3(0.0f);
        glm::vec3 m_aiThreatPosition = glm::vec3(0.0f);
        float m_aiStableEconomy = 0.0f;
        float m_aiLastAttack = -1000.0f;
        float m_aiThreatSince = -1.0f;
        bool m_aiRaidActive = false;
        bool m_aiHasObjective = false;
        bool m_aiHasChoke = false;
        void ResetAI();
        void UpdateAI(float deltaTime);
        bool AIPositionSafe(const glm::vec3& position) const;
        void AssignAISquads(const AIDecision& decision);
        void ExecuteAIProduction(const AIDecision& decision);
        void ExecuteAIConstruction(const AIDecision& decision);
        void ExecuteAIOrders(const AIDecision& decision);
        bool TryAIConstruction(UnitType type, RTSUnit& builder, const glm::vec3& anchor);
        float m_resourceTimer = 0.0f;

        // Tactical Minimap
        bool m_showMinimap = true;
        bool m_showTerritories = true; // Minimap Territory Domination Overlay toggle
        glm::vec3 m_minimapPingPos = glm::vec3(0.0f);
        float m_minimapPingTimer = 0.0f;

        // Graphical Build Menu Panel
        int m_buildPanelTab = 0; // 0=ALL, 1=UNITS, 2=BASE, 3=ECON
        std::vector<int> m_controlGroups[10];
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
        std::vector<CBaseEntity*> m_shieldOrbEntities;

        // Resource Deposit Nodes & Construction
        std::vector<ResourceDepositNode> m_resourceNodes;
        bool m_isPlacingBuilding = false;
        UnitType m_placingBuildingType = UnitType::LandFactory;
        CBaseEntity* m_buildGhostEntity = nullptr;
        CBaseEntity* m_buildBeamEntity = nullptr;
        std::vector<EngineerBuildTask> m_constructionTasks;
        bool TryStartConstruction(UnitType type, Team team, int builderEntityIndex, glm::vec3 position);
        bool HasConstructionTask(int builderEntityIndex) const;
        void CancelConstruction(int builderEntityIndex);
        void UpdateConstruction(float deltaTime, AudioServer* audioServer);
        bool IsConstructionSiteValid(UnitType type, const glm::vec3& position, int builderEntityIndex) const;
        std::vector<PendingSpawn> m_pendingSpawns;
        std::vector<FactoryQueue> m_factoryQueues;
    };

}
