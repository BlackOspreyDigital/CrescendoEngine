#include "../IO/VFSFormat.hpp" 
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

void PackProject(const std::string& projectDir, const std::string& outputFile) {
    std::ofstream out(outputFile, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << outputFile << "\n";
        return;
    }

    // 1. Write a dummy header
    PakHeader header = { {'C','R','S','C'}, 1, 0, 0 };
    out.write(reinterpret_cast<const char*>(&header), sizeof(PakHeader));

    std::vector<FatEntry> fat;

    // The folders we want to pack
    std::vector<std::string> targetDirs = { "data", "tags" };

    // 2. Iterate through specific project directories
    for (const auto& dir : targetDirs) {
        std::string fullDirPath = projectDir + "/" + dir;
        
        if (!fs::exists(fullDirPath) || !fs::is_directory(fullDirPath)) {
            std::cout << "[Packer] Skipping missing directory: " << fullDirPath << "\n";
            continue;
        }

        for (const auto& entry : fs::recursive_directory_iterator(fullDirPath)) {
            if (!entry.is_regular_file()) continue;

            // Generate a clean relative path (e.g., "data/models/player.glb")
            std::string fullPath = entry.path().string();
            std::string virtualPath = fs::relative(entry.path(), projectDir).generic_string();

            FatEntry fatEntry;
            fatEntry.pathHash = HashPath(virtualPath);
            fatEntry.offset = out.tellp(); // Current write position
            fatEntry.size = fs::file_size(entry);

            // Read raw file and write immediately to the .pak
            std::ifstream in(fullPath, std::ios::binary);
            out << in.rdbuf(); 

            fat.push_back(fatEntry);
            std::cout << "Packed: " << virtualPath << "\n";
        }
    }

    // 3. Write the File Allocation Table (FAT)
    header.fatOffset = out.tellp(); 
    header.numEntries = static_cast<uint32_t>(fat.size());

    out.write(reinterpret_cast<const char*>(fat.data()), fat.size() * sizeof(FatEntry));

    // 4. Overwrite the dummy header with the real FAT offset
    out.seekp(0);
    out.write(reinterpret_cast<const char*>(&header), sizeof(PakHeader));
    
    std::cout << "========================================================\n";
    std::cout << "SUCCESS: Packed " << header.numEntries << " files into " << outputFile << "\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: CrescendoPacker <project_root>\n";
        std::cerr << "Example: CrescendoPacker ./MyGame\n";
        return 1;
    }

    std::string projectDir = argv[1];

    // Clean trailing slash
    if (projectDir.back() == '/' || projectDir.back() == '\\') {
        projectDir.pop_back();
    }

    if (!fs::exists(projectDir) || !fs::is_directory(projectDir)) {
        std::cerr << "Error: Project directory does not exist: " << projectDir << "\n";
        return 1;
    }

    // Ensure the maps directory exists to receive the compiled pak
    std::string mapsDir = projectDir + "/maps";
    if (!fs::exists(mapsDir)) {
        fs::create_directory(mapsDir);
        std::cout << "[Packer] Created missing 'maps/' directory.\n";
    }

    std::string outputFile = mapsDir + "/data.pak";

    std::cout << "========================================================\n";
    std::cout << " CRESCENDO PACKER v2 \n";
    std::cout << "========================================================\n";
    std::cout << "Target Project: " << projectDir << "\n";
    std::cout << "Output Payload: " << outputFile << "\n\n";

    PackProject(projectDir, outputFile);
    return 0;
}