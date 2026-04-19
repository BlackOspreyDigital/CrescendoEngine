#pragma once
#include <cstdint>
#include <string_view>

// Force 1-byte alignment so the compiler doesn't insert padding 
// that breaks the binary layout on disk.
#pragma pack(push, 1)

struct PakHeader {
    char magic[4];       // 'C', 'R', 'S', 'C' (Crescendo)
    uint32_t version;    // Format version, start at 1
    uint64_t fatOffset;  // Byte offset to where the File Allocation Table begins
    uint32_t numEntries; // Number of files in the archive
};

struct FatEntry {
    uint64_t pathHash;   // FNV-1a hash of the virtual file path
    uint64_t offset;     // Starting byte of the file data
    uint64_t size;       // Size of the file in bytes
};

#pragma pack(pop)

// 64-bit FNV-1a Hash (Fast, excellent distribution, standard in games)
constexpr uint64_t HashPath(std::string_view path) {
    uint64_t hash = 0xCBF29CE484222325ull;
    for (char c : path) {
        // Standardize paths to use forward slashes before hashing
        if (c == '\\') c = '/'; 
        hash ^= static_cast<uint64_t>(c);
        hash *= 0x100000001B3ull;
    }
    return hash;
}