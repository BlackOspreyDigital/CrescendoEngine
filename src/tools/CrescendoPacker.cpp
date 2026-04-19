#include "../IO/VFSFormat.hpp" 
#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

void PackDirectory(const std::string& sourceDir, const std::string& outputFile) {
    std::ofstream out(outputFile, std::ios::binary);
    if (!out.is_open()) {
        std::cerr << "Failed to open output file: " << outputFile << "\n";
        return;
    }

    // 1. Write a dummy header
    PakHeader header = { {'C','R','S','C'}, 1, 0, 0 };
    out.write(reinterpret_cast<const char*>(&header), sizeof(PakHeader));

    std::vector<FatEntry> fat;

    // 2. Iterate through all files in the asset directory
    for (const auto& entry : fs::recursive_directory_iterator(sourceDir)) {
        if (!entry.is_regular_file()) continue;

        std::string fullPath = entry.path().string();
        // Create the virtual path (e.g., "models/player.mesh")
        std::string virtualPath = fullPath.substr(sourceDir.length() + 1);

        FatEntry fatEntry;
        fatEntry.pathHash = HashPath(virtualPath);
        fatEntry.offset = out.tellp(); // Current write position
        fatEntry.size = fs::file_size(entry);

        // Read raw file and write immediately to the .pak
        std::ifstream in(fullPath, std::ios::binary);
        out << in.rdbuf(); 

        fat.push_back(fatEntry);
    }

    // 3. Write the File Allocation Table (FAT)
    header.fatOffset = out.tellp(); 
    header.numEntries = static_cast<uint32_t>(fat.size());

    out.write(reinterpret_cast<const char*>(fat.data()), fat.size() * sizeof(FatEntry));

    // 4. Overwrite the dummy header with the real FAT offset
    out.seekp(0);
    out.write(reinterpret_cast<const char*>(&header), sizeof(PakHeader));
    
    std::cout << "Successfully packed " << header.numEntries << " files into " << outputFile << "\n";
}

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: CrescendoPacker <source_directory> <output_file.pak>\n";
        std::cerr << "Example: CrescendoPacker ./assets ./data.pak\n";
        return 1;
    }

    std::string sourceDir = argv[1];
    std::string outputFile = argv[2];

    // Ensure the source directory doesn't have a trailing slash for clean string slicing
    if (sourceDir.back() == '/' || sourceDir.back() == '\\') {
        sourceDir.pop_back();
    }

    if (!fs::exists(sourceDir) || !fs::is_directory(sourceDir)) {
        std::cerr << "Error: Source directory does not exist.\n";
        return 1;
    }

    PackDirectory(sourceDir, outputFile);
    return 0;
}