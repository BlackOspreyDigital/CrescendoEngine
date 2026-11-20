#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <cstdint>

namespace Crescendo {

    enum class WaveformType : int {
        Sine = 0,
        Triangle = 1,
        Sawtooth = 2,
        Square = 3,
        WhiteNoise = 4,
        PinkNoise = 5,
        BrownNoise = 6,
        FM = 7
    };

    enum class NoiseType : int {
        White = 0,
        Pink = 1,
        Brown = 2
    };

    enum class PitchSlideType : int {
        Linear = 0,
        Exponential = 1,
        SCurve = 2
    };

    enum class FilterType : int {
        None = 0,
        LowPass = 1,
        HighPass = 2,
        BandPass = 3,
        Notch = 4
    };

    enum class DistortionType : int {
        SoftTanh = 0,
        HardClip = 1,
        Foldback = 2
    };

    // --- EXTENSIBLE DSP EFFECT ARCHITECTURE ---
    class ISynthEffect {
    public:
        virtual ~ISynthEffect() = default;
        virtual const char* GetName() const = 0;
        virtual void Reset() = 0;
        virtual void Process(std::vector<float>& buffer, uint32_t sampleRate) = 0;
    };

    // Concrete Biquad Filter (LowPass, HighPass, BandPass with dynamic envelope sweep)
    class BiquadFilterEffect : public ISynthEffect {
    public:
        FilterType type = FilterType::None;
        float cutoff = 2000.0f;        // Hz (20 - 20000)
        float resonance = 1.0f;       // Q (0.1 - 10.0)
        float cutoffSweep = 0.0f;     // Hz to sweep over sound duration

        const char* GetName() const override { return "Biquad Filter"; }
        void Reset() override;
        void Process(std::vector<float>& buffer, uint32_t sampleRate) override;

    private:
        float x1 = 0.0f, x2 = 0.0f;
        float y1 = 0.0f, y2 = 0.0f;
    };

    // Concrete Overdrive / Saturation / Distortion
    class DistortionEffect : public ISynthEffect {
    public:
        bool enabled = false;
        DistortionType type = DistortionType::SoftTanh;
        float drive = 1.0f;           // 1.0 (clean) to 20.0 (extreme)
        float mix = 1.0f;             // 0.0 (dry) to 1.0 (wet)

        const char* GetName() const override { return "Distortion"; }
        void Reset() override {}
        void Process(std::vector<float>& buffer, uint32_t sampleRate) override;
    };

    // Concrete Lo-Fi Bitcrusher
    class BitcrusherEffect : public ISynthEffect {
    public:
        bool enabled = false;
        int bitDepth = 16;            // 2 to 16 bits
        int downsampleFactor = 1;     // 1 to 32
        float mix = 1.0f;

        const char* GetName() const override { return "Bitcrusher"; }
        void Reset() override {}
        void Process(std::vector<float>& buffer, uint32_t sampleRate) override;
    };

    // Concrete Delay / Echo
    class DelayEffect : public ISynthEffect {
    public:
        bool enabled = false;
        float delayTimeMs = 120.0f;   // ms (10 - 1000)
        float feedback = 0.35f;       // 0.0 - 0.95
        float damping = 0.2f;         // High-freq damping (0.0 - 1.0)
        float mix = 0.3f;             // Wet mix (0.0 - 1.0)

        const char* GetName() const override { return "Delay"; }
        void Reset() override;
        void Process(std::vector<float>& buffer, uint32_t sampleRate) override;

    private:
        std::vector<float> m_delayBuffer;
        size_t m_writeIndex = 0;
        float m_lastOutput = 0.0f;
    };

    // Concrete Reverb / Diffusion Tank
    class ReverbEffect : public ISynthEffect {
    public:
        bool enabled = false;
        float roomSize = 0.5f;        // 0.0 to 1.0 (comb feedback scaling)
        float damping = 0.25f;        // 0.0 to 1.0
        float mix = 0.25f;            // Wet mix (0.0 to 1.0)

        const char* GetName() const override { return "Reverb"; }
        void Reset() override;
        void Process(std::vector<float>& buffer, uint32_t sampleRate) override;

    private:
        struct CombFilter {
            std::vector<float> buffer;
            size_t index = 0;
            float filterStore = 0.0f;
            void Init(size_t size) { buffer.assign(size, 0.0f); index = 0; filterStore = 0.0f; }
        };

        struct AllPassFilter {
            std::vector<float> buffer;
            size_t index = 0;
            float feedback = 0.5f;
            void Init(size_t size, float fb) { buffer.assign(size, 0.0f); index = 0; feedback = fb; }
        };

        CombFilter m_combs[4];
        AllPassFilter m_allPass[2];
        bool m_initialized = false;
    };

    // Concrete Peak Limiter / Soft Clipper
    class LimiterEffect : public ISynthEffect {
    public:
        bool enabled = true;
        float ceiling = 0.98f;        // Peak ceiling
        float makeupGain = 1.0f;      // Linear gain boost

        const char* GetName() const override { return "Peak Limiter"; }
        void Reset() override {}
        void Process(std::vector<float>& buffer, uint32_t sampleRate) override;
    };

    // --- SYNTH PARAMETERS STRUCT ---
    struct SynthSoundParams {
        std::string name = "Custom Sound";
        std::string targetFilepath = "assets/audio/custom.wav";

        // Oscillator 1 (Carrier)
        WaveformType waveform = WaveformType::Sine;
        float baseFreq = 440.0f;           // Hz (20 to 10000)
        float pulseWidth = 0.5f;           // 0.05 to 0.95 (for square wave)
        float oscMix = 1.0f;

        // Oscillator 2 (Sub / Harmonic)
        bool subOscEnabled = false;
        WaveformType subWaveform = WaveformType::Sine;
        int subOctave = -1;                // -2, -1, 0, +1
        float subMix = 0.4f;

        // Noise Generator Layer
        bool noiseEnabled = false;
        NoiseType noiseType = NoiseType::White;
        float noiseMix = 0.2f;
        float noiseDecay = 1.0f;           // Multiplier on envelope decay for noise

        // Frequency Modulation (FM)
        bool fmEnabled = false;
        float fmRatio = 1.0f;              // Modulator freq = baseFreq * fmRatio
        float fmDepth = 2.0f;              // Modulation index

        // Pitch Dynamics & Glide
        float targetFreq = 440.0f;         // End frequency
        float pitchSlideDuration = 0.0f;   // Seconds (0 = entire duration)
        PitchSlideType pitchSlideCurve = PitchSlideType::Exponential;
        
        // Pitch Jump (2-stage transient)
        bool pitchJumpEnabled = false;
        float pitchJumpTime = 0.05f;       // Time in seconds when pitch jump occurs
        float pitchJumpAmount = 0.0f;      // Hz offset or semitone delta

        // Vibrato (LFO Frequency Modulation)
        float vibratoRate = 0.0f;          // Hz (0 to 40)
        float vibratoDepth = 0.0f;         // Hz deviation
        float vibratoDelay = 0.0f;         // Seconds before vibrato starts

        // Amplitude Envelope (ADSR + Punch)
        float attackTime = 0.01f;          // Seconds
        float decayTime = 0.15f;           // Seconds
        float sustainLevel = 0.0f;         // 0.0 to 1.0
        float sustainTime = 0.0f;          // Seconds
        float releaseTime = 0.05f;         // Seconds
        float punch = 0.0f;                // Transient attack punch boost (0.0 to 1.0)

        // Effects Rack Settings
        bool filterEnabled = false;
        FilterType filterType = FilterType::LowPass;
        float filterCutoff = 2500.0f;
        float filterResonance = 1.0f;
        float filterCutoffSweep = 0.0f;

        bool distortionEnabled = false;
        DistortionType distortionType = DistortionType::SoftTanh;
        float distortionDrive = 2.5f;
        float distortionMix = 0.5f;

        bool bitcrushEnabled = false;
        int bitcrushBits = 8;
        int bitcrushDownsample = 2;
        float bitcrushMix = 0.5f;

        bool delayEnabled = false;
        float delayTimeMs = 120.0f;
        float delayFeedback = 0.3f;
        float delayDamping = 0.2f;
        float delayMix = 0.3f;

        bool reverbEnabled = false;
        float reverbRoomSize = 0.6f;
        float reverbDamping = 0.3f;
        float reverbMix = 0.25f;

        // Master Output
        float masterVolume = 0.85f;
        bool limiterEnabled = true;

        float GetTotalDuration() const {
            return attackTime + decayTime + sustainTime + releaseTime;
        }
    };

    // --- THE SYNTHESIZER ENGINE ---
    class SoundSynth {
    public:
        // Synthesis Core
        static std::vector<float> GenerateFloat(const SynthSoundParams& params, uint32_t sampleRate = 44100);
        static std::vector<int16_t> GeneratePCM(const SynthSoundParams& params, uint32_t sampleRate = 44100);

        // WAV file writer (overwrites cleanly)
        static bool WriteWavFile(const std::string& path, const std::vector<int16_t>& pcm, uint32_t sampleRate = 44100);

        // Game Sound Presets
        static SynthSoundParams GetPresetCannon();
        static SynthSoundParams GetPresetRocket();
        static SynthSoundParams GetPresetLaser();
        static SynthSoundParams GetPresetTurret();
        static SynthSoundParams GetPresetExplosion();
        static SynthSoundParams GetPresetShieldHit();
        static SynthSoundParams GetPresetFootstep();
        static SynthSoundParams GetPresetUISelect();
        static SynthSoundParams GetPresetUIError();
        static SynthSoundParams GetPresetBeam();
        static SynthSoundParams GetPresetEMP();

        static std::vector<std::string> GetPresetNames();
        static SynthSoundParams GetPresetByName(const std::string& name);

        // Mutation / Procedural Sound Discovery
        static void MutateParams(SynthSoundParams& params, float mutationAmount = 0.25f);
        static SynthSoundParams GenerateRandom(WaveformType preferType = WaveformType::Sine);

        // Serialization
        static std::string ToJson(const SynthSoundParams& params);
        static bool FromJson(const std::string& jsonStr, SynthSoundParams& outParams);
        static bool SavePresetFile(const std::string& filepath, const SynthSoundParams& params);
        static bool LoadPresetFile(const std::string& filepath, SynthSoundParams& outParams);
    };

} // namespace Crescendo
