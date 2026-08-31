#include <iostream>
#include <filesystem>
#include "core/Engine.hpp"

namespace fs = std::filesystem;

std::string GetCommandLineArg(int argc, char* argv[], const std::string& flag) {
    for (int i = 0; i < argc - 1; ++i) {
        if (std::string(argv[i]) == flag) return std::string(argv[i + 1]);
    }
    return "";
}

int main(int argc, char* argv[]) {
    std::cout << "========================================================\n";
    std::cout << " CRESCENDO SDK - LEMUR TAG EDITOR (v1.0)\n";
    std::cout << "========================================================\n";

    std::string projectPath = GetCommandLineArg(argc, argv, "-project");
    if (projectPath.empty()) projectPath = "./";
    if (projectPath.back() == '/' || projectPath.back() == '\\') projectPath.pop_back();

    std::string tagsPath = projectPath + "/tags";
    if (!fs::exists(tagsPath)) {
        std::cerr << "[Lemur] FATAL: Invalid project. 'tags/' directory missing at: " << tagsPath << "\n";
        return -1;
    }

    Crescendo::Engine engine;
    if (!engine.Initialize("Lemur - Tag Editor", 1280, 720, projectPath, true)) {
        std::cerr << "Failed to initialize Lemur!" << std::endl;
        return -1;
    }

    engine.Run();
    engine.Shutdown();

    return 0;
}