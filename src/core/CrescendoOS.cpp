#include "core/CrescendoOS.hpp"
#include <iostream>

namespace Crescendo::Core {

    CrescendoOS::CrescendoOS() {
        ScanProjectsFromVFS();
    }

    void CrescendoOS::Initialize() {
        std::cout << "[Crescendo OS] Supervisor initialized. Booting into Dashboard Shell..." << std::endl;
        currentMode = MasterMode::OS_Dashboard;
        transitionProgress = 1.0f;
    }

    void CrescendoOS::ScanProjectsFromVFS() {
        projectList = {
            { "Momentum Surf", "vfs://projects/surf_game", "scenes/surf_arena.json", 0 },
            { "Crescendo Engine Demo", "vfs://projects/engine_demo", "scenes/viking_village.json", 1 },
            { "Voxel Sandbox", "vfs://projects/voxel_test", "scenes/planetary_voxels.json", 2 }
        };
    }

    void CrescendoOS::Update(float dt) {
        if (currentMode == MasterMode::OS_Dashboard || transitionProgress < 1.0f) {
            bladesUI.Update(dt);
        }

        if (currentMode != targetMode) {
            transitionProgress -= transitionSpeed * dt;
            if (transitionProgress <= 0.0f) {
                currentMode = targetMode;
                transitionProgress = 1.0f;
                std::cout << "[Crescendo OS] Mode transition complete: " << static_cast<int>(currentMode) << std::endl;
            }
        }
    }

    void CrescendoOS::TransitionToMode(MasterMode newMode) {
        if (currentMode == newMode) return;
        std::cout << "[Crescendo OS] Routing state: " << static_cast<int>(currentMode) 
                  << " -> " << static_cast<int>(newMode) << std::endl;
        targetMode = newMode;
        transitionProgress = 1.0f;
    }

    void CrescendoOS::LaunchProject(const std::string& vfsPath) {
        std::cout << "[Crescendo OS] Mounting project workspace: " << vfsPath << std::endl;
        TransitionToMode(MasterMode::Editor_Workspace);
    }

    void CrescendoOS::ExitToOS() {
        std::cout << "[Crescendo OS] Suspending workspace. Returning to Dashboard Shell..." << std::endl;
        if (emulatorModule.GetState() == Modules::EmulatorState::Running) {
            emulatorModule.Pause();
        }
        TransitionToMode(MasterMode::OS_Dashboard);
    }

    // =========================================================
    // MASTER OS TAB ROUTING
    // =========================================================
    void CrescendoOS::OnTabSelected(int tabIndex) {
        std::cout << "[Crescendo OS] Executing tab selection for index: " << tabIndex << std::endl;

        switch (tabIndex) {
            case 0: // HOME TAB (Project Workspace / Editor)
                std::cout << "[Crescendo OS] -> Routing to Editor Workspace..." << std::endl;
                // If a project is selected, launch it; otherwise boot into standard Editor Mode
                if (!projectList.empty()) {
                    LaunchProject(projectList[0].vfsRoot);
                } else {
                    TransitionToMode(MasterMode::Editor_Workspace);
                }
                break;

            case 1: // EMULATION TAB (Hardware Supervisor)
                std::cout << "[Crescendo OS] -> Routing to Emulation Hub..." << std::endl;
                // If emulator is already running a ROM, jump straight to full-screen TV runtime!
                if (emulatorModule.GetState() == Modules::EmulatorState::Running) {
                    TransitionToMode(MasterMode::Emulation_Runtime);
                } else {
                    // Otherwise, stay on Dashboard so ImGui Viewport can display the ROM Library browser
                    std::cout << "[Crescendo OS] Emulator idle. Displaying ROM Library..." << std::endl;
                }
                break;

            case 2: // SYSTEM TAB (Engine & VFS Config)
                std::cout << "[Crescendo OS] -> Opening System & Hardware Config..." << std::endl;
                // Keep Dashboard active while displaying system inspection overlays
                break;

            case 3: // COMMUNITY / TOOLS TAB
                std::cout << "[Crescendo OS] -> Accessing Tools & Asset Pipeline..." << std::endl;
                break;

            default:
                std::cerr << "[Crescendo OS] Unknown tab index selected: " << tabIndex << std::endl;
                break;
        }
    }
}