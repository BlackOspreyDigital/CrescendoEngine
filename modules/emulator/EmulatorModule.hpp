#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <vulkan/vulkan.h>
#include "glm/glm.hpp"

namespace Crescendo::Modules {

    struct RomMetadata {
        std::string title;
        std::string vfsPath;
        size_t sizeBytes;
        bool isLoaded;
    };

    enum class EmulatorState {
        Stopped,
        Running,
        Paused
    };

    class EmulatorModule {
    public:
        EmulatorModule();
        ~EmulatorModule();

        // --- 1. VFS & Library Management ---
        void ScanLibrary(const std::string& vfsRomDirectory);
        const std::vector<RomMetadata>& GetRomLibrary() const { return romLibrary; }
        bool LoadRom(const std::string& vfsPath);

        // --- 2. Async Execution Control ---
        void Start();
        void Pause();
        void Stop();
        EmulatorState GetState() const { return currentState.load(); }

        // --- 3. Lock-Free Framebuffer Bridge ---
        void SubmitFramePacket(const void* rawRgbaData, uint32_t width, uint32_t height);
        void UpdateVulkanTexture(VkDevice device, VkCommandBuffer cmdBuffer);
        
        VkImageView GetFramebufferView() const { return emulatorFramebufferView; }
        VkSampler GetFramebufferSampler() const { return emulatorSampler; }

    private:
        std::vector<RomMetadata> romLibrary;
        std::atomic<EmulatorState> currentState{EmulatorState::Stopped};

        VkImage emulatorImage = VK_NULL_HANDLE;
        VkDeviceMemory emulatorImageMemory = VK_NULL_HANDLE;
        VkImageView emulatorFramebufferView = VK_NULL_HANDLE;
        VkSampler emulatorSampler = VK_NULL_HANDLE;

        void* frameRingBufferPtr = nullptr; 
        std::atomic<bool> newFrameReady{false};
    };
}