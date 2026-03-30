#pragma once
#include <vector>
#include <string>

#include <glm/glm.hpp>

struct ma_engine; 
struct ma_sound; 

namespace Crescendo {
    class AudioServer {
    public:
        AudioServer() = default;
        ~AudioServer();

        bool Initialize();
        void Shutdown();

        void UpdateListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

        // --- AMBIENT SOUND CONTROLS ---
        bool LoadAmbientSound(const std::string& filepath, float volume = 1.0f);
        void PlayAmbientSound();
        void StopAmbientSound();

        // --- 3D SPATIAL EMITTERS ---
        // Now supports unlimited entities!
        bool LoadSpatialEmitter(const std::string& filepath, glm::vec3 position, float volume = 1.0f);
        void PlaySpatialEmitters();
        void StopSpatialEmitters();
        void ClearSpatialEmitters(); 

        // One-shot for footsteps
        void PlayOneShot(const std::string& filepath, float volume = 1.0f);

    private:
        ma_engine* m_engine = nullptr;
        ma_sound* m_ambientSound = nullptr;
        
        // --- THE UPGRADE ---
        std::vector<ma_sound*> m_spatialEmitters; 
    };
}