#include "AudioServer.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>

#define MINIAUDIO_IMPLEMENTATION
#include "deps/miniaudio.h" // Adjust this path to wherever you saved miniaudio.h

static void WriteWav(const std::string& path, const std::vector<int16_t>& pcm, uint32_t sampleRate = 44100) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ifstream check(path);
    if (check.is_open()) return; // File already generated

    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) return;

    uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
    uint32_t chunkSize = 36 + dataSize;
    uint16_t audioFormat = 1;
    uint16_t numChannels = 1;
    uint32_t byteRate = sampleRate * numChannels * sizeof(int16_t);
    uint16_t blockAlign = numChannels * sizeof(int16_t);
    uint16_t bitsPerSample = 16;
    uint32_t subchunk1Size = 16;

    file.write("RIFF", 4);
    file.write((const char*)&chunkSize, 4);
    file.write("WAVE", 4);
    file.write("fmt ", 4);
    file.write((const char*)&subchunk1Size, 4);
    file.write((const char*)&audioFormat, 2);
    file.write((const char*)&numChannels, 2);
    file.write((const char*)&sampleRate, 4);
    file.write((const char*)&byteRate, 4);
    file.write((const char*)&blockAlign, 2);
    file.write((const char*)&bitsPerSample, 2);
    file.write("data", 4);
    file.write((const char*)&dataSize, 4);
    file.write((const char*)pcm.data(), dataSize);
}

static void GenerateProceduralSounds() {
    const uint32_t sampleRate = 44100;

    // 1. Cannon blast (Tank): Heavy bass boom 110Hz -> 30Hz + punch over 0.35s
    {
        int totalSamples = (int)(sampleRate * 0.35f);
        std::vector<int16_t> pcm(totalSamples);
        double phase = 0.0;
        for (int i = 0; i < totalSamples; i++) {
            float t = (float)i / (float)sampleRate;
            float freq = 120.0f * exp(-9.0f * t) + 30.0f;
            phase += 2.0 * 3.1415926535 * (double)freq / (double)sampleRate;
            float env = (1.0f - t / 0.35f);
            env = env * env;
            float noise = ((float)(rand() % 1000) / 500.0f - 1.0f) * exp(-16.0f * t);
            float sample = (sin(phase) * 0.75f + noise * 0.45f) * env;
            sample = std::clamp(sample, -1.0f, 1.0f);
            pcm[i] = (int16_t)(sample * 32767.0f);
        }
        WriteWav("assets/audio/cannon.wav", pcm);
    }

    // 2. Rocket launch / flight whoosh: 220Hz -> 550Hz rising pitch + turbulent air over 0.30s
    {
        int totalSamples = (int)(sampleRate * 0.30f);
        std::vector<int16_t> pcm(totalSamples);
        double phase = 0.0;
        for (int i = 0; i < totalSamples; i++) {
            float t = (float)i / (float)sampleRate;
            float normT = t / 0.30f;
            float freq = 200.0f + 380.0f * normT;
            phase += 2.0 * 3.1415926535 * (double)freq / (double)sampleRate;
            float env = sin(normT * 3.14159265f);
            float noise = ((float)(rand() % 1000) / 500.0f - 1.0f) * 0.4f;
            float sample = (sin(phase) * 0.55f + noise) * env;
            sample = std::clamp(sample, -1.0f, 1.0f);
            pcm[i] = (int16_t)(sample * 32767.0f);
        }
        WriteWav("assets/audio/rocket.wav", pcm);
    }

    // 3. Laser pew: 950Hz -> 180Hz drop over 0.16s
    {
        int totalSamples = (int)(sampleRate * 0.16f);
        std::vector<int16_t> pcm(totalSamples);
        double phase = 0.0;
        for (int i = 0; i < totalSamples; i++) {
            float t = (float)i / (float)sampleRate;
            float freq = 950.0f * exp(-14.0f * t) + 160.0f;
            phase += 2.0 * 3.1415926535 * (double)freq / (double)sampleRate;
            float env = (1.0f - t / 0.16f);
            float sample = sin(phase) * env * 0.85f;
            sample = std::clamp(sample, -1.0f, 1.0f);
            pcm[i] = (int16_t)(sample * 32767.0f);
        }
        WriteWav("assets/audio/laser.wav", pcm);
    }

    // 4. Turret pulse shot: 750Hz sharp rapid plasma tap over 0.14s
    {
        int totalSamples = (int)(sampleRate * 0.14f);
        std::vector<int16_t> pcm(totalSamples);
        double phase = 0.0;
        for (int i = 0; i < totalSamples; i++) {
            float t = (float)i / (float)sampleRate;
            float freq = 750.0f * exp(-18.0f * t) + 220.0f;
            phase += 2.0 * 3.1415926535 * (double)freq / (double)sampleRate;
            float env = (1.0f - t / 0.14f) * (1.0f - t / 0.14f);
            float sample = sin(phase) * env * 0.85f;
            sample = std::clamp(sample, -1.0f, 1.0f);
            pcm[i] = (int16_t)(sample * 32767.0f);
        }
        WriteWav("assets/audio/turret.wav", pcm);
    }
}

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
        ma_engine_set_volume(m_engine, m_masterVolume);
        std::cout << "[Audio] MiniAudio Initialized Successfully (Master Volume: " << (int)(m_masterVolume * 100.0f) << "%)." << std::endl;
        GenerateProceduralSounds();
        return true;
    }

    void AudioServer::SetMasterVolume(float volume) {
        m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
        if (m_engine) {
            ma_engine_set_volume(m_engine, m_masterVolume);
        }
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