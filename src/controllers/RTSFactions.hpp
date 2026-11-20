#pragma once

#include <array>
#include <algorithm>
#include <glm/glm.hpp>
#include <imgui.h>

namespace Crescendo {

    enum class FactionId : int {
        Vanguard = 0,
        Syndicate = 1,
        Aegis = 2,
        Zephyr = 3,
        Obsidian = 4,
        Solaris = 5,
        Count = 6
    };

    static constexpr int RUDE_FACTION_COUNT = static_cast<int>(FactionId::Count);

    struct FactionModifiers {
        float unitHealthMult = 1.0f;
        float structureHealthMult = 1.0f;
        float shieldHealthMult = 1.0f;
        float weaponDamageMult = 1.0f;
        float airDamageMult = 1.0f;
        float unitSpeedMult = 1.0f;
        float buildSpeedMult = 1.0f;
        float unitCostMult = 1.0f;
        float massIncomeMult = 1.0f;
        float energyIncomeMult = 1.0f;
        float visionRangeMult = 1.0f;
        int bonusStartingTanks = 0;
        int bonusStartingPlanes = 0;
        int bonusStartingEngineers = 0;
        int bonusStartingTurrets = 0;
        int bonusStartingMass = 0;
        int bonusStartingEnergy = 0;
        int preferredPersonality = 0; // 0 = Normal, 1 = Rush, 2 = Turtle
    };

    struct FactionDef {
        FactionId id;
        const char* name;
        const char* shortCode;
        const char* archetype;
        const char* perkSummary;
        glm::vec3 primaryColor;
        glm::vec3 accentColor;
        glm::vec3 shieldColor;
        glm::vec3 projectileColor;
        ImVec4 uiColor;
        FactionModifiers mods;
    };

    inline const std::array<FactionDef, RUDE_FACTION_COUNT>& GetRudeFactions() {
        static const std::array<FactionDef, RUDE_FACTION_COUNT> kFactions = {{
            {
                FactionId::Vanguard,
                "Vanguard Coalition",
                "VGD",
                "Balanced Expeditionary",
                "+8% Unit & Structure HP | +5% Build Speed | Balanced All-Rounder",
                glm::vec3(0.18f, 0.55f, 0.95f),
                glm::vec3(0.28f, 0.82f, 1.00f),
                glm::vec3(0.35f, 0.78f, 1.00f),
                glm::vec3(0.45f, 0.85f, 1.00f),
                ImVec4(0.24f, 0.68f, 1.00f, 1.0f),
                FactionModifiers{
                    1.08f, // unitHealthMult
                    1.08f, // structureHealthMult
                    1.00f, // shieldHealthMult
                    1.00f, // weaponDamageMult
                    1.00f, // airDamageMult
                    1.00f, // unitSpeedMult
                    1.05f, // buildSpeedMult
                    1.00f, // unitCostMult
                    1.00f, // massIncomeMult
                    1.00f, // energyIncomeMult
                    1.00f, // visionRangeMult
                    0, 0, 0, 0, 0, 0,
                    0 // Normal
                }
            },
            {
                FactionId::Syndicate,
                "Crimson Syndicate",
                "SYN",
                "Heavy Shock Assault",
                "+18% Weapon Dmg | +12% Unit HP | +1 Tank | +8% Unit Cost | -6% Speed",
                glm::vec3(0.94f, 0.20f, 0.16f),
                glm::vec3(1.00f, 0.45f, 0.15f),
                glm::vec3(1.00f, 0.35f, 0.25f),
                glm::vec3(1.00f, 0.50f, 0.18f),
                ImVec4(0.96f, 0.30f, 0.24f, 1.0f),
                FactionModifiers{
                    1.12f, // unitHealthMult
                    1.00f, // structureHealthMult
                    1.00f, // shieldHealthMult
                    1.18f, // weaponDamageMult
                    1.00f, // airDamageMult
                    0.94f, // unitSpeedMult
                    1.00f, // buildSpeedMult
                    1.08f, // unitCostMult
                    1.00f, // massIncomeMult
                    1.00f, // energyIncomeMult
                    1.00f, // visionRangeMult
                    1, 0, 0, 0, 0, 0,
                    1 // Rush
                }
            },
            {
                FactionId::Aegis,
                "Aegis Directorate",
                "AEG",
                "Bastion Shieldworks",
                "+35% Shield HP | +25% Structure HP | +15% Energy | +1 Turret | -8% Speed",
                glm::vec3(0.95f, 0.75f, 0.14f),
                glm::vec3(1.00f, 0.88f, 0.38f),
                glm::vec3(1.00f, 0.84f, 0.28f),
                glm::vec3(1.00f, 0.90f, 0.30f),
                ImVec4(0.98f, 0.80f, 0.22f, 1.0f),
                FactionModifiers{
                    1.00f, // unitHealthMult
                    1.25f, // structureHealthMult
                    1.35f, // shieldHealthMult
                    1.00f, // weaponDamageMult
                    1.00f, // airDamageMult
                    0.92f, // unitSpeedMult
                    1.00f, // buildSpeedMult
                    1.00f, // unitCostMult
                    1.00f, // massIncomeMult
                    1.15f, // energyIncomeMult
                    1.00f, // visionRangeMult
                    0, 0, 0, 1, 0, 0,
                    2 // Turtle
                }
            },
            {
                FactionId::Zephyr,
                "Zephyr Collective",
                "ZEP",
                "Air & Rapid Maneuver",
                "+22% Speed | +20% Air Dmg | +15% Vision | +1 Plane | -10% Unit HP",
                glm::vec3(0.12f, 0.84f, 0.58f),
                glm::vec3(0.35f, 0.96f, 0.78f),
                glm::vec3(0.25f, 0.95f, 0.72f),
                glm::vec3(0.30f, 1.00f, 0.78f),
                ImVec4(0.20f, 0.90f, 0.64f, 1.0f),
                FactionModifiers{
                    0.90f, // unitHealthMult
                    0.95f, // structureHealthMult
                    1.00f, // shieldHealthMult
                    1.00f, // weaponDamageMult
                    1.20f, // airDamageMult
                    1.22f, // unitSpeedMult
                    1.10f, // buildSpeedMult
                    1.00f, // unitCostMult
                    1.00f, // massIncomeMult
                    1.00f, // energyIncomeMult
                    1.15f, // visionRangeMult
                    0, 1, 0, 0, 0, 0,
                    1 // Rush
                }
            },
            {
                FactionId::Obsidian,
                "Obsidian Pact",
                "OBS",
                "Mass Swarm Industry",
                "+25% Mass | -15% Unit Cost | +18% Build Speed | +1 Engineer | -8% Dmg",
                glm::vec3(0.68f, 0.26f, 0.96f),
                glm::vec3(0.88f, 0.46f, 1.00f),
                glm::vec3(0.76f, 0.40f, 1.00f),
                glm::vec3(0.84f, 0.45f, 1.00f),
                ImVec4(0.76f, 0.40f, 1.00f, 1.0f),
                FactionModifiers{
                    0.96f, // unitHealthMult
                    1.00f, // structureHealthMult
                    1.00f, // shieldHealthMult
                    0.92f, // weaponDamageMult
                    1.00f, // airDamageMult
                    1.04f, // unitSpeedMult
                    1.18f, // buildSpeedMult
                    0.85f, // unitCostMult
                    1.25f, // massIncomeMult
                    1.00f, // energyIncomeMult
                    1.00f, // visionRangeMult
                    0, 0, 1, 0, 100, 0,
                    1 // Rush
                }
            },
            {
                FactionId::Solaris,
                "Solaris Dominion",
                "SOL",
                "Overclocked Energy & Recon",
                "+25% Energy | +10% Weapon Dmg | +20% Vision | +150 Energy | -6% Struct HP",
                glm::vec3(1.00f, 0.44f, 0.00f), // Warm Orange (Atlas #FF7100)
                glm::vec3(1.00f, 0.68f, 0.20f),
                glm::vec3(1.00f, 0.55f, 0.15f),
                glm::vec3(1.00f, 0.60f, 0.20f),
                ImVec4(1.00f, 0.50f, 0.10f, 1.0f),
                FactionModifiers{
                    1.00f, // unitHealthMult
                    0.94f, // structureHealthMult
                    1.10f, // shieldHealthMult
                    1.10f, // weaponDamageMult
                    1.05f, // airDamageMult
                    1.05f, // unitSpeedMult
                    1.05f, // buildSpeedMult
                    1.00f, // unitCostMult
                    1.00f, // massIncomeMult
                    1.25f, // energyIncomeMult
                    1.20f, // visionRangeMult
                    0, 0, 0, 0, 0, 150,
                    0 // Normal
                }
            }
        }};
        return kFactions;
    }

    inline const FactionDef& GetFactionDef(FactionId id) {
        int idx = std::clamp(static_cast<int>(id), 0, RUDE_FACTION_COUNT - 1);
        return GetRudeFactions()[static_cast<size_t>(idx)];
    }

    inline glm::vec3 ApplyMirrorContrast(const glm::vec3& color, bool isMirrorEnemy) {
        if (!isMirrorEnemy) return color;
        // Shift luminance/hue noticeably in mirror matches so rival units stay distinct
        return glm::clamp(glm::vec3(1.0f - color.r * 0.45f, color.g * 0.45f + 0.12f, color.b * 0.45f + 0.18f),
                          glm::vec3(0.12f), glm::vec3(1.0f));
    }

    inline ImU32 ToImU32(const glm::vec3& color, int alpha = 255) {
        int r = std::clamp(static_cast<int>(color.r * 255.0f), 0, 255);
        int g = std::clamp(static_cast<int>(color.g * 255.0f), 0, 255);
        int b = std::clamp(static_cast<int>(color.b * 255.0f), 0, 255);
        int a = std::clamp(alpha, 0, 255);
        return IM_COL32(r, g, b, a);
    }

    struct FactionAtlasUV {
        ImVec2 uv0;
        ImVec2 uv1;
    };

    // UV mappings for assets/ui/game_menu/atlasfactions.png (2048x2048)
    // Includes a 2px boundary inset (0.0010f) to avoid bilinear texture filtering bleed across card seams
    inline FactionAtlasUV GetFactionAtlasUV(FactionId id) {
        switch (id) {
            case FactionId::Vanguard:
                // Pixel: (1, 1277) -> (513, 2045)
                return { ImVec2(0.0015f, 0.6245f), ImVec2(0.2495f, 0.9975f) };
            case FactionId::Syndicate:
                // Pixel: (515, 1277) -> (1026, 2045)
                return { ImVec2(0.2525f, 0.6245f), ImVec2(0.5000f, 0.9975f) };
            case FactionId::Aegis:
                // Pixel: (1536, 1277) -> (2048, 2045)
                return { ImVec2(0.7510f, 0.6245f), ImVec2(0.9990f, 0.9975f) };
            case FactionId::Zephyr:
                // Pixel: (1026, 1277) -> (1536, 2045)
                return { ImVec2(0.5020f, 0.6245f), ImVec2(0.7490f, 0.9975f) };
            case FactionId::Obsidian:
                // Pixel: (1536, 509) -> (2048, 1277)
                return { ImVec2(0.7510f, 0.2495f), ImVec2(0.9990f, 0.6225f) };
            case FactionId::Solaris:
                // Pixel: (1024, 509) -> (1536, 1277)
                return { ImVec2(0.5010f, 0.2495f), ImVec2(0.7490f, 0.6225f) };
            default:
                return { ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f) };
        }
    }

} // namespace Crescendo
