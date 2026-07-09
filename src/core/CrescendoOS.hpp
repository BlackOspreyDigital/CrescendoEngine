#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include "glm/glm.hpp"
#include "modules/blades_ui/BladesUI.hpp"
#include "modules/emulator/EmulatorModule.hpp"

namespace Crescendo::Core {

    enum class MasterMode : uint8_t {
        OS_Dashboard = 0,
        Editor_Workspace = 1,
        Emulation_Runtime = 2,
        Project_Runtime = 3
    };

    struct ProjectMetadata {
        std::string name;
        std::string vfsRoot;
        std::string defaultScene;
        int iconIndex;
    };

    class CrescendoOS {
    public:
        CrescendoOS();
        ~CrescendoOS() = default;

        void Initialize();
        void Update(float dt);
        
        void TransitionToMode(MasterMode newMode);
        MasterMode GetCurrentMode() const { return currentMode; }
        float GetTransitionProgress() const { return transitionProgress; }

        void OnTabSelected(int tabIndex);
        void LaunchProject(const std::string& vfsPath);
        void ExitToOS();

        Modules::BladesUI& GetBladesUI() { return bladesUI; }
        Modules::EmulatorModule& GetEmulator() { return emulatorModule; }
        const std::vector<ProjectMetadata>& GetProjectList() const { return projectList; }

    private:
        MasterMode currentMode{MasterMode::OS_Dashboard};
        MasterMode targetMode{MasterMode::OS_Dashboard};
        float transitionProgress{1.0f};
        float transitionSpeed{3.5f};

        Modules::BladesUI bladesUI;
        Modules::EmulatorModule emulatorModule;
        std::vector<ProjectMetadata> projectList;

        void ScanProjectsFromVFS();
    };
}