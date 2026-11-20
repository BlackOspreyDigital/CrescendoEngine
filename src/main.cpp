#include <iostream>
#include <string>
#include <filesystem>
#include "core/Engine.hpp"
#include "servers/camera/Camera.hpp"
#include "core/EnginePaths.hpp"

namespace fs = std::filesystem;

// Lightweight argument parser
std::string GetCommandLineArg(int argc, char* argv[], const std::string& flag) {
    for (int i = 0; i < argc - 1; ++i) {
        if (std::string(argv[i]) == flag) {
            return std::string(argv[i + 1]);
        }
    }
    return "";
}

int main(int argc, char* argv[]) {
    // --- BOOT SEQUENCE ---
    std::cout << "========================================================\n";
    std::cout << " CRESCENDO SDK - SPECTRA EDITOR (v1.0)\n";
    std::cout << "========================================================\n";

    std::string projectPath = GetCommandLineArg(argc, argv, "-project");
    
    // Fallback if no project argument is provided
    if (projectPath.empty()) {
        std::cout << "[Boot] Warning: No -project argument provided.\n";
        if (fs::exists("./tags") && fs::exists("./data")) {
            projectPath = "./";
        } else if (fs::exists("MyProject/tags") && fs::exists("MyProject/data")) {
            projectPath = "MyProject";
        } else if (fs::exists("Meridian_Crescendo_Edition/tags") && fs::exists("Meridian_Crescendo_Edition/data")) {
            projectPath = "Meridian_Crescendo_Edition";
        } else {
            projectPath = "MyProject";
        }
        std::cout << "[Boot] Defaulting project path to: " << projectPath << "\n";
    }

    // Clean up trailing slashes
    if (!projectPath.empty() && (projectPath.back() == '/' || projectPath.back() == '\\')) {
        projectPath.pop_back();
    }

    std::cout << "[Boot] Target Project Directory: " << projectPath << "\n";

    // Enforce the H3EK Directory Structure (auto-create if missing to ensure successful launch)
    if (!fs::exists(projectPath + "/tags")) {
        fs::create_directories(projectPath + "/tags");
    }
    if (!fs::exists(projectPath + "/data")) {
        fs::create_directories(projectPath + "/data");
    }

    std::cout << "[Boot] Project structure verified. Initializing Core...\n";
    std::cout << "========================================================\n";

    Crescendo::Engine engine;

    // Boot up the engine and core servers, passing the project path
    if (!engine.Initialize("Spectra - Crescendo Engine", 1920, 1080, projectPath)) {
        std::cerr << "Failed to initialize Crescendo Engine!" << std::endl;
        return -1;
    }

    if (auto* camera = engine.renderer->GetMainCamera()) {
        camera->SetPosition(glm::vec3(0.0f, -10.0f, 5.0f)); 
        camera->SetRotation(glm::vec3(0.0f, 90.0f, 0.0f)); 
    }

    engine.Run();
    engine.Shutdown();

    return 0;
}