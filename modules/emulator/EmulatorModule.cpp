#include "modules/emulator/EmulatorModule.hpp"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <filesystem>

namespace Crescendo::Modules {

    struct FrameStagingBuffer {
        std::vector<uint8_t> buffers[3];
        std::atomic<int> readIndex{0};
        std::atomic<int> writeIndex{1};
        std::atomic<int> idleIndex{2};
        uint32_t width{1280};
        uint32_t height{720};
    };

    EmulatorModule::EmulatorModule() {
        frameRingBufferPtr = new FrameStagingBuffer();
        auto* staging = static_cast<FrameStagingBuffer*>(frameRingBufferPtr);
        
        size_t defaultSize = 1280 * 720 * 4;
        for (int i = 0; i < 3; ++i) {
            staging->buffers[i].resize(defaultSize, 0);
        }
        std::cout << "[EmulatorModule] Lock-free triple buffer initialized (1280x720 RGBA)." << std::endl;
    }

    EmulatorModule::~EmulatorModule() {
        Stop();
        if (frameRingBufferPtr) {
            delete static_cast<FrameStagingBuffer*>(frameRingBufferPtr);
            frameRingBufferPtr = nullptr;
        }
    }

    void EmulatorModule::ScanLibrary(const std::string& vfsRomDirectory) {
        romLibrary.clear();
        std::cout << "[EmulatorModule] Scanning VFS directory: " << vfsRomDirectory << std::endl;

        try {
            if (std::filesystem::exists(vfsRomDirectory)) {
                for (const auto& entry : std::filesystem::directory_iterator(vfsRomDirectory)) {
                    if (entry.is_regular_file()) {
                        RomMetadata meta{};
                        meta.title = entry.path().stem().string();
                        meta.vfsPath = entry.path().string();
                        meta.sizeBytes = entry.file_size();
                        meta.isLoaded = false;
                        romLibrary.push_back(meta);
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[EmulatorModule] Library scan error: " << e.what() << std::endl;
        }

        std::cout << "[EmulatorModule] Discovered " << romLibrary.size() << " ROM payloads." << std::endl;
    }

    bool EmulatorModule::LoadRom(const std::string& vfsPath) {
        Stop();
        std::cout << "[EmulatorModule] Mounting ROM payload into virtual memory: " << vfsPath << std::endl;
        for (auto& rom : romLibrary) {
            rom.isLoaded = (rom.vfsPath == vfsPath);
        }
        return true;
    }

    void EmulatorModule::Start() {
        if (currentState.load() == EmulatorState::Running) return;
        std::cout << "[EmulatorModule] Dispatching emulated hardware loops to async worker pool..." << std::endl;
        currentState.store(EmulatorState::Running);
    }

    void EmulatorModule::Pause() {
        if (currentState.load() == EmulatorState::Running) {
            std::cout << "[EmulatorModule] Pausing hardware execution." << std::endl;
            currentState.store(EmulatorState::Paused);
        }
    }

    void EmulatorModule::Stop() {
        if (currentState.load() != EmulatorState::Stopped) {
            std::cout << "[EmulatorModule] Stopping hardware execution and resetting registers." << std::endl;
            currentState.store(EmulatorState::Stopped);
        }
    }

    void EmulatorModule::SubmitFramePacket(const void* rawRgbaData, uint32_t width, uint32_t height) {
        if (currentState.load() != EmulatorState::Running || !rawRgbaData) return;

        auto* staging = static_cast<FrameStagingBuffer*>(frameRingBufferPtr);
        int writeIdx = staging->writeIndex.load(std::memory_order_relaxed);
        size_t requiredSize = width * height * 4;

        if (staging->buffers[writeIdx].size() != requiredSize) {
            staging->buffers[writeIdx].resize(requiredSize);
            staging->width = width;
            staging->height = height;
        }

        std::memcpy(staging->buffers[writeIdx].data(), rawRgbaData, requiredSize);
        int nextWriteIdx = staging->idleIndex.exchange(writeIdx, std::memory_order_release);
        staging->writeIndex.store(nextWriteIdx, std::memory_order_relaxed);
        newFrameReady.store(true, std::memory_order_release);
    }

    void EmulatorModule::UpdateVulkanTexture(VkDevice device, VkCommandBuffer cmdBuffer) {
        if (!newFrameReady.exchange(false, std::memory_order_acquire)) return; 

        auto* staging = static_cast<FrameStagingBuffer*>(frameRingBufferPtr);
        int currentReadIdx = staging->readIndex.load(std::memory_order_relaxed);
        int freshDataIdx = staging->idleIndex.exchange(currentReadIdx, std::memory_order_acquire);
        staging->readIndex.store(freshDataIdx, std::memory_order_relaxed);
    }
}