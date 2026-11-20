#include "AudioServer.hpp"
#include "SoundSynth.hpp"
#include <iostream>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <filesystem>

#define MINIAUDIO_IMPLEMENTATION
#include "deps/miniaudio.h" // Adjust this path to wherever you saved miniaudio.h

namespace Crescendo {

    AudioServer* AudioServer::s_instance = nullptr;

    AudioServer::AudioServer() {
        s_instance = this;
    }

    AudioServer::~AudioServer() {
        Shutdown();
        if (s_instance == this) {
            s_instance = nullptr;
        }
    }

    void AudioServer::GenerateProceduralSounds(bool forceOverwrite) {
        auto WritePreset = [&](const std::string& path, const SynthSoundParams& params) {
            if (!forceOverwrite && std::filesystem::exists(path)) {
                return;
            }
            auto pcm = SoundSynth::GeneratePCM(params);
            SoundSynth::WriteWavFile(path, pcm);
        };

        WritePreset("assets/audio/cannon.wav", SoundSynth::GetPresetCannon());
        WritePreset("assets/audio/rocket.wav", SoundSynth::GetPresetRocket());
        WritePreset("assets/audio/laser.wav", SoundSynth::GetPresetLaser());
        WritePreset("assets/audio/turret.wav", SoundSynth::GetPresetTurret());
        WritePreset("assets/audio/explosion.wav", SoundSynth::GetPresetExplosion());
        WritePreset("assets/audio/shield_hit.wav", SoundSynth::GetPresetShieldHit());
        WritePreset("assets/audio/footstep.wav", SoundSynth::GetPresetFootstep());
        WritePreset("assets/audio/ui_select.wav", SoundSynth::GetPresetUISelect());
        WritePreset("assets/audio/ui_error.wav", SoundSynth::GetPresetUIError());
        WritePreset("assets/audio/beam.wav", SoundSynth::GetPresetBeam());
        WritePreset("assets/audio/emp.wav", SoundSynth::GetPresetEMP());
    }

    void AudioServer::PlayPcm(const std::vector<int16_t>& pcm, uint32_t sampleRate, float volume) {
        if (!m_engine || pcm.empty()) return;
        std::string previewPath = "assets/audio/.preview_" + std::to_string(m_previewRingIndex % 4) + ".wav";
        m_previewRingIndex++;
        if (SoundSynth::WriteWavFile(previewPath, pcm, sampleRate)) {
            PlayOneShot(previewPath, volume);
        }
    }
    void AudioServer::StopSequencer() {
        if (m_sequencerSound) {
            ma_sound_stop(m_sequencerSound);
            ma_sound_uninit(m_sequencerSound);
            delete m_sequencerSound;
            m_sequencerSound = nullptr;
        }
    }

    void AudioServer::PlaySequencerPcm(const std::vector<int16_t>& pcm, uint32_t sampleRate, float volume) {
        if (!m_engine || pcm.empty()) return;

        StopSequencer();

        const std::string sequencePath = "assets/audio/.sequencer_preview.wav";
        if (!SoundSynth::WriteWavFile(sequencePath, pcm, sampleRate)) return;

        m_sequencerSound = new ma_sound;
        if (ma_sound_init_from_file(m_engine, sequencePath.c_str(), 0, nullptr, nullptr, m_sequencerSound) != MA_SUCCESS) {
            delete m_sequencerSound;
            m_sequencerSound = nullptr;
            return;
        }

        ma_sound_set_volume(m_sequencerSound, volume);
        ma_sound_start(m_sequencerSound);
    }


    void AudioServer::PlaySynthSound(const SynthSoundParams& params) {
        auto pcm = SoundSynth::GeneratePCM(params);
        PlayPcm(pcm, 44100, params.masterVolume);
    }

    bool AudioServer::Initialize() {
        s_instance = this;
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
        GenerateProceduralSounds(true);
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
        StopSequencer();
        
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