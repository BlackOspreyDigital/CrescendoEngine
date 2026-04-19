#pragma once
#include "VFSFormat.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <mutex>

class VirtualFileSystem {
private:
    std::ifstream pakStream;
    std::unordered_map<uint64_t, FatEntry> fileLookup;
    std::mutex vfsMutex; // Prevents thread collisions when seeking/reading the file

    bool isMounted = false;

    // Private constructor for Singleton pattern
    VirtualFileSystem() = default; 
    ~VirtualFileSystem() { if (pakStream.is_open()) pakStream.close(); }

public:
    // Global access point
    static VirtualFileSystem& Get() {
        static VirtualFileSystem instance;
        return instance;
    }

    // Delete copy/move constructors
    VirtualFileSystem(const VirtualFileSystem&) = delete;
    VirtualFileSystem& operator=(const VirtualFileSystem&) = delete;

    bool Mount(const std::string& pakPath);
    std::vector<char> ReadFile(const std::string& virtualPath);
    
    bool IsMounted() const { return isMounted; }
};