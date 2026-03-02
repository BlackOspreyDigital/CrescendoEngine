#include "AudioServer.hpp"
#include <iostream>

#define MINIAUDIO_IMPLEMENTATION
#include "deps/miniaudio.h" // Adjust this path to wherever you saved miniaudio.h

namespace Crescendo {

    AudioServer::~AudioServer() {
        Shutdown();
    }

    bool AudioServer::Initialize() {
        m_engine = new ma_engine;
        
        ma_result result = ma_engine_init(NULL, m_engine);
        if (result != MA_SUCCESS) {
            std::cerr << "[Audio] Failed to initialize MiniAudio engine!" << std::endl;
            delete m_engine;
            m_engine = nullptr;
            return false;
        }

        std::cout << "[Audio] MiniAudio Initialized Successfully." << std::endl;
        return true;
    }

    void AudioServer::Shutdown() {
        if (m_ambientSound) {
            ma_sound_uninit(m_ambientSound);
            delete m_ambientSound;
            m_ambientSound = nullptr;
        }
        
        // Wipe all 3D emitters before killing the engine!
        ClearSpatialEmitters();

        if (m_engine) {
            ma_engine_uninit(m_engine);
            delete m_engine;
            m_engine = nullptr;
        }
    }

    bool AudioServer::LoadAmbientSound(const std::string& filepath, float volume) {
        if (!m_engine) return false;
        if (!m_ambientSound) m_ambientSound = new ma_sound;

        // Initialize the sound as a dedicated object we can control
        if (ma_sound_init_from_file(m_engine, filepath.c_str(), 0, NULL, NULL, m_ambientSound) != MA_SUCCESS) {
            std::cerr << "[Audio] Failed to load ambient sound: " << filepath << std::endl;
            return false;
        }

        ma_sound_set_volume(m_ambientSound, volume);
        ma_sound_set_looping(m_ambientSound, MA_TRUE); // Force it to loop 
        return true;
    }

    void AudioServer::PlayAmbientSound() {
        if (m_ambientSound) ma_sound_start(m_ambientSound);
    }

    void AudioServer::StopAmbientSound() {
        if (m_ambientSound) ma_sound_stop(m_ambientSound);
    }

    // --- ONE SHOT SOUNDS (Footsteps, Impacts) ---
    void AudioServer::PlayOneShot(const std::string& filepath, float volume) {
        if (!m_engine) return;
        
        // We will wire the volume parameter up later when we build sound groups, 
        // but this fires the sound immediately in MiniAudio!
        ma_engine_play_sound(m_engine, filepath.c_str(), NULL);
    }

    // --- 3D SPATIAL AUDIO ---
    void AudioServer::UpdateListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
        if (!m_engine) return;
        ma_engine_listener_set_position(m_engine, 0, position.x, position.y, position.z);
        ma_engine_listener_set_direction(m_engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(m_engine, 0, up.x, up.y, up.z);
    }

    bool AudioServer::LoadSpatialEmitter(const std::string& filepath, glm::vec3 position, float volume) {
        if (!m_engine) return false;
        
        // Allocate a brand new sound for this specific entity
        ma_sound* newEmitter = new ma_sound;

        if (ma_sound_init_from_file(m_engine, filepath.c_str(), 0, NULL, NULL, newEmitter) != MA_SUCCESS) {
            std::cerr << "[Audio] Failed to load spatial emitter: " << filepath << std::endl;
            delete newEmitter; // Clean up if it fails!
            return false;
        }

        ma_sound_set_volume(newEmitter, volume);
        ma_sound_set_looping(newEmitter, MA_TRUE);
        ma_sound_set_spatialization_enabled(newEmitter, MA_TRUE);
        ma_sound_set_position(newEmitter, position.x, position.y, position.z);
        ma_sound_set_rolloff(newEmitter, 2.0f);
        ma_sound_set_min_distance(newEmitter, 5.0f);
        ma_sound_set_max_distance(newEmitter, 40.0f);

        // Store it in our active roster
        m_spatialEmitters.push_back(newEmitter);
        return true;
    }

    void AudioServer::PlaySpatialEmitters() {
        for (auto* emitter : m_spatialEmitters) {
            ma_sound_start(emitter);
        }
    }

    void AudioServer::StopSpatialEmitters() {
        for (auto* emitter : m_spatialEmitters) {
            ma_sound_stop(emitter);
        }
    }

    void AudioServer::ClearSpatialEmitters() {
        for (auto* emitter : m_spatialEmitters) {
            ma_sound_stop(emitter);
            ma_sound_uninit(emitter);
            delete emitter;
        }
        m_spatialEmitters.clear();
    }
}