#include "VirtualFileSystem.hpp"
#include <iostream>
#include <filesystem>

bool VirtualFileSystem::Mount(const std::string& pakPath) {
    std::lock_guard<std::mutex> lock(vfsMutex);
    
    pakStream.open(pakPath, std::ios::binary);
    if (!pakStream.is_open()) {
        std::cerr << "[VFS] Failed to open archive: " << pakPath << "\n";
        return false;
    }

    PakHeader header;
    pakStream.read(reinterpret_cast<char*>(&header), sizeof(PakHeader));

    if (header.magic[0] != 'C' || header.magic[1] != 'R' || 
        header.magic[2] != 'S' || header.magic[3] != 'C') {
        std::cerr << "[VFS] Invalid magic bytes in archive. Not a Crescendo pak.\n";
        pakStream.close();
        return false;
    }

    // Jump to File Allocation Table and read it
    pakStream.seekg(header.fatOffset);
    std::vector<FatEntry> entries(header.numEntries);
    pakStream.read(reinterpret_cast<char*>(entries.data()), header.numEntries * sizeof(FatEntry));

    // Populate hash map for O(1) lookups
    for (const auto& entry : entries) {
        fileLookup[entry.pathHash] = entry;
    }
    
    isMounted = true;
    std::cout << "[VFS] Mounted archive successfully. (" << header.numEntries << " files)\n";
    return true;
}

std::vector<char> VirtualFileSystem::ReadFile(const std::string& virtualPath) {
    std::lock_guard<std::mutex> lock(vfsMutex);

    // 1. If we are running a packed release build, look in the archive first
    if (isMounted) {
        uint64_t hash = HashPath(virtualPath);
        auto it = fileLookup.find(hash);
        
        if (it != fileLookup.end()) {
            std::vector<char> buffer(it->second.size);
            pakStream.seekg(it->second.offset);
            pakStream.read(buffer.data(), it->second.size);
            return buffer;
        }
    }

    // 2. Fallback: We must be in Editor Mode, load directly from the OS hard drive!
    std::ifstream file(virtualPath, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[VFS] Warning: File not found: " << virtualPath << "\n";
        return {};
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    
    return buffer;
}