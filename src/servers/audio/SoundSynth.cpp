#include "SoundSynth.hpp"
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include "deps/json/json.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Crescendo {

    // =========================================================================
    // DSP EFFECTS IMPLEMENTATIONS
    // =========================================================================

    void BiquadFilterEffect::Reset() {
        x1 = x2 = y1 = y2 = 0.0f;
    }

    void BiquadFilterEffect::Process(std::vector<float>& buffer, uint32_t sampleRate) {
        if (type == FilterType::None || buffer.empty()) return;

        Reset();
        const size_t totalSamples = buffer.size();

        // Calculate coefficients with dynamic cutoff sweep support
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;

        auto UpdateCoeffs = [&](float currentCutoff) {
            float fc = std::clamp(currentCutoff, 20.0f, (float)sampleRate * 0.48f);
            float q = std::clamp(resonance, 0.1f, 12.0f);
            float w0 = 2.0f * (float)M_PI * fc / (float)sampleRate;
            float alpha = std::sin(w0) / (2.0f * q);
            float cosw0 = std::cos(w0);

            switch (type) {
                case FilterType::LowPass:
                    b0 = (1.0f - cosw0) * 0.5f;
                    b1 = 1.0f - cosw0;
                    b2 = (1.0f - cosw0) * 0.5f;
                    a0 = 1.0f + alpha;
                    a1 = -2.0f * cosw0;
                    a2 = 1.0f - alpha;
                    break;
                case FilterType::HighPass:
                    b0 = (1.0f + cosw0) * 0.5f;
                    b1 = -(1.0f + cosw0);
                    b2 = (1.0f + cosw0) * 0.5f;
                    a0 = 1.0f + alpha;
                    a1 = -2.0f * cosw0;
                    a2 = 1.0f - alpha;
                    break;
                case FilterType::BandPass:
                    b0 = alpha;
                    b1 = 0.0f;
                    b2 = -alpha;
                    a0 = 1.0f + alpha;
                    a1 = -2.0f * cosw0;
                    a2 = 1.0f - alpha;
                    break;
                case FilterType::Notch:
                    b0 = 1.0f;
                    b1 = -2.0f * cosw0;
                    b2 = 1.0f;
                    a0 = 1.0f + alpha;
                    a1 = -2.0f * cosw0;
                    a2 = 1.0f - alpha;
                    break;
                default:
                    b0 = a0 = 1.0f;
                    b1 = b2 = a1 = a2 = 0.0f;
                    break;
            }
        };

        UpdateCoeffs(cutoff);

        for (size_t i = 0; i < totalSamples; i++) {
            // Recalculate coefficients if cutoff is sweeping
            if (std::abs(cutoffSweep) > 0.01f && (i % 16 == 0)) {
                float progress = (float)i / (float)totalSamples;
                float dynamicCutoff = cutoff + cutoffSweep * (1.0f - progress);
                UpdateCoeffs(dynamicCutoff);
            }

            float x0 = buffer[i];
            float y0 = (b0 / a0) * x0 + (b1 / a0) * x1 + (b2 / a0) * x2
                     - (a1 / a0) * y1 - (a2 / a0) * y2;

            x2 = x1;
            x1 = x0;
            y2 = y1;
            y1 = y0;

            buffer[i] = y0;
        }
    }

    void DistortionEffect::Process(std::vector<float>& buffer, uint32_t /*sampleRate*/) {
        if (!enabled || buffer.empty()) return;

        float k = std::clamp(drive, 1.0f, 25.0f);
        float w = std::clamp(mix, 0.0f, 1.0f);
        float tanhK = std::tanh(k);
        if (std::abs(tanhK) < 0.001f) tanhK = 1.0f;

        for (size_t i = 0; i < buffer.size(); i++) {
            float in = buffer[i];
            float driven = in * k;
            float out = in;

            switch (type) {
                case DistortionType::SoftTanh:
                    out = std::tanh(driven) / tanhK;
                    break;
                case DistortionType::HardClip:
                    out = std::clamp(driven, -1.0f, 1.0f);
                    break;
                case DistortionType::Foldback: {
                    float val = driven * 0.5f;
                    if (val > 1.0f || val < -1.0f) {
                        float absVal = std::abs(val);
                        float fold = 4.0f * std::abs(std::fmod(absVal - 0.25f, 1.0f) - 0.5f) - 1.0f;
                        out = (val < 0.0f) ? -fold : fold;
                    } else {
                        out = val;
                    }
                    break;
                }
            }

            buffer[i] = in * (1.0f - w) + out * w;
        }
    }

    void BitcrusherEffect::Process(std::vector<float>& buffer, uint32_t /*sampleRate*/) {
        if (!enabled || buffer.empty()) return;

        int bits = std::clamp(bitDepth, 2, 16);
        int downsample = std::max(1, downsampleFactor);
        float levels = (float)(1 << (bits - 1));
        float w = std::clamp(mix, 0.0f, 1.0f);

        float heldSample = 0.0f;
        for (size_t i = 0; i < buffer.size(); i++) {
            float in = buffer[i];
            if (i % downsample == 0) {
                float quantized = std::round(in * levels) / levels;
                heldSample = std::clamp(quantized, -1.0f, 1.0f);
            }
            buffer[i] = in * (1.0f - w) + heldSample * w;
        }
    }

    void DelayEffect::Reset() {
        m_delayBuffer.clear();
        m_writeIndex = 0;
        m_lastOutput = 0.0f;
    }

    void DelayEffect::Process(std::vector<float>& buffer, uint32_t sampleRate) {
        if (!enabled || buffer.empty()) return;

        size_t delaySamples = (size_t)(std::clamp(delayTimeMs, 1.0f, 2000.0f) * 0.001f * (float)sampleRate);
        if (delaySamples == 0) return;

        if (m_delayBuffer.size() != delaySamples) {
            m_delayBuffer.assign(delaySamples, 0.0f);
            m_writeIndex = 0;
            m_lastOutput = 0.0f;
        }

        float fb = std::clamp(feedback, 0.0f, 0.95f);
        float damp = std::clamp(damping, 0.0f, 0.95f);
        float w = std::clamp(mix, 0.0f, 1.0f);

        for (size_t i = 0; i < buffer.size(); i++) {
            float in = buffer[i];
            float delayed = m_delayBuffer[m_writeIndex];

            // Low-pass damping inside the feedback loop
            m_lastOutput = delayed * (1.0f - damp) + m_lastOutput * damp;

            m_delayBuffer[m_writeIndex] = in + m_lastOutput * fb;
            m_writeIndex = (m_writeIndex + 1) % delaySamples;

            buffer[i] = in * (1.0f - w) + delayed * w;
        }
    }

    void ReverbEffect::Reset() {
        m_initialized = false;
    }

    void ReverbEffect::Process(std::vector<float>& buffer, uint32_t sampleRate) {
        if (!enabled || buffer.empty()) return;

        // Comb tuning in samples (scaled for 44.1kHz standard)
        static const size_t combTunings[4] = { 1116, 1188, 1277, 1356 };
        static const size_t allPassTunings[2] = { 225, 341 };

        float scale = (float)sampleRate / 44100.0f;
        for (int i = 0; i < 4; i++) {
            size_t size = (size_t)(combTunings[i] * scale);
            if (m_combs[i].buffer.size() != size) m_combs[i].Init(size);
        }
        for (int i = 0; i < 2; i++) {
            size_t size = (size_t)(allPassTunings[i] * scale);
            if (m_allPass[i].buffer.size() != size) m_allPass[i].Init(size, 0.5f);
        }

        float roomFeedback = std::clamp(roomSize, 0.0f, 0.98f) * 0.28f + 0.7f;
        float damp = std::clamp(damping, 0.0f, 0.95f) * 0.4f;
        float w = std::clamp(mix, 0.0f, 1.0f);

        for (size_t i = 0; i < buffer.size(); i++) {
            float in = buffer[i];
            float combSum = 0.0f;

            // 4 Parallel Comb Filters
            for (int c = 0; c < 4; c++) {
                auto& comb = m_combs[c];
                float out = comb.buffer[comb.index];
                comb.filterStore = out * (1.0f - damp) + comb.filterStore * damp;
                comb.buffer[comb.index] = in + comb.filterStore * roomFeedback;
                comb.index = (comb.index + 1) % comb.buffer.size();
                combSum += out;
            }

            float wet = combSum * 0.25f;

            // 2 Series All-Pass Filters
            for (int a = 0; a < 2; a++) {
                auto& ap = m_allPass[a];
                float bufOut = ap.buffer[ap.index];
                float apOut = -wet + bufOut;
                ap.buffer[ap.index] = wet + bufOut * ap.feedback;
                ap.index = (ap.index + 1) % ap.buffer.size();
                wet = apOut;
            }

            buffer[i] = in * (1.0f - w) + wet * w;
        }
    }

    void LimiterEffect::Process(std::vector<float>& buffer, uint32_t /*sampleRate*/) {
        if (!enabled || buffer.empty()) return;

        float ceilVal = std::clamp(ceiling, 0.5f, 1.0f);
        float gain = std::max(0.01f, makeupGain);

        for (size_t i = 0; i < buffer.size(); i++) {
            float sample = buffer[i] * gain;
            if (std::abs(sample) > ceilVal) {
                // Smooth soft saturation knee
                sample = (sample > 0.0f ? 1.0f : -1.0f) * (ceilVal + (1.0f - ceilVal) * std::tanh((std::abs(sample) - ceilVal) / (1.0f - ceilVal)));
            }
            buffer[i] = std::clamp(sample, -1.0f, 1.0f);
        }
    }

    // =========================================================================
    // NOISE GENERATOR HELPERS
    // =========================================================================

    struct NoiseGenerator {
        // Paul Kellet's filter coefficients for pink noise
        float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f, b3 = 0.0f, b4 = 0.0f, b5 = 0.0f, b6 = 0.0f;
        float lastBrown = 0.0f;

        float NextWhite() {
            return ((float)(std::rand() % 20001) / 10000.0f) - 1.0f;
        }

        float NextPink() {
            float white = NextWhite();
            b0 = 0.99886f * b0 + white * 0.0555179f;
            b1 = 0.99332f * b1 + white * 0.0750759f;
            b2 = 0.96900f * b2 + white * 0.1538520f;
            b3 = 0.86650f * b3 + white * 0.3104856f;
            b4 = 0.55000f * b4 + white * 0.5329522f;
            b5 = -0.7616f * b5 - white * 0.0168980f;
            float pink = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362f) * 0.11f;
            b6 = white * 0.115926f;
            return std::clamp(pink, -1.0f, 1.0f);
        }

        float NextBrown() {
            float white = NextWhite();
            lastBrown = (lastBrown + (0.025f * white)) / 1.02f;
            return std::clamp(lastBrown * 3.5f, -1.0f, 1.0f);
        }

        float Next(NoiseType type) {
            switch (type) {
                case NoiseType::Pink:  return NextPink();
                case NoiseType::Brown: return NextBrown();
                default:               return NextWhite();
            }
        }
    };

    // =========================================================================
    // SOUND SYNTHESIS ENGINE
    // =========================================================================

    std::vector<float> SoundSynth::GenerateFloat(const SynthSoundParams& params, uint32_t sampleRate) {
        float totalDuration = params.GetTotalDuration();
        if (totalDuration <= 0.005f) totalDuration = 0.05f;

        size_t totalSamples = (size_t)(totalDuration * (float)sampleRate);
        if (totalSamples < 64) totalSamples = 64;

        std::vector<float> buffer(totalSamples, 0.0f);

        double phase = 0.0;
        double subPhase = 0.0;
        double modPhase = 0.0;
        NoiseGenerator noiseGen;

        float attack = std::max(0.0001f, params.attackTime);
        float decay = std::max(0.0001f, params.decayTime);
        float sustainLevel = std::clamp(params.sustainLevel, 0.0f, 1.0f);
        float sustainTime = std::max(0.0f, params.sustainTime);
        float release = std::max(0.0001f, params.releaseTime);

        float slideDuration = params.pitchSlideDuration > 0.0f ? params.pitchSlideDuration : totalDuration;
        slideDuration = std::max(0.001f, slideDuration);

        // Sub-oscillator octave multiplier
        float subOctaveMult = 0.5f;
        if (params.subOctave == -2) subOctaveMult = 0.25f;
        else if (params.subOctave == -1) subOctaveMult = 0.5f;
        else if (params.subOctave == 0)  subOctaveMult = 1.0f;
        else if (params.subOctave == 1)  subOctaveMult = 2.0f;

        for (size_t i = 0; i < totalSamples; i++) {
            float t = (float)i / (float)sampleRate;

            // --- 1. PITCH & FREQUENCY CALCULATION ---
            float normSlideT = std::clamp(t / slideDuration, 0.0f, 1.0f);
            float currentFreq = params.baseFreq;

            switch (params.pitchSlideCurve) {
                case PitchSlideType::Linear:
                    currentFreq = params.baseFreq + (params.targetFreq - params.baseFreq) * normSlideT;
                    break;
                case PitchSlideType::Exponential: {
                    float ratio = params.targetFreq / std::max(1.0f, params.baseFreq);
                    currentFreq = params.baseFreq * std::pow(ratio, normSlideT);
                    break;
                }
                case PitchSlideType::SCurve: {
                    float smooth = normSlideT * normSlideT * (3.0f - 2.0f * normSlideT);
                    currentFreq = params.baseFreq + (params.targetFreq - params.baseFreq) * smooth;
                    break;
                }
            }

            // Pitch Jump (2-stage transient trigger)
            if (params.pitchJumpEnabled && t >= params.pitchJumpTime) {
                currentFreq += params.pitchJumpAmount;
            }

            // Vibrato (LFO)
            if (params.vibratoDepth > 0.01f && t >= params.vibratoDelay) {
                float vibPhase = 2.0f * (float)M_PI * params.vibratoRate * (t - params.vibratoDelay);
                currentFreq += std::sin(vibPhase) * params.vibratoDepth;
            }
            currentFreq = std::clamp(currentFreq, 10.0f, (float)sampleRate * 0.48f);

            // --- 2. PHASE ACCUMULATION & FM ---
            float carrierFreq = currentFreq;
            if (params.fmEnabled) {
                float modFreq = currentFreq * std::max(0.1f, params.fmRatio);
                modPhase += 2.0 * M_PI * (double)modFreq / (double)sampleRate;
                if (modPhase > 2.0 * M_PI) modPhase = std::fmod(modPhase, 2.0 * M_PI);
                float modSample = std::sin((float)modPhase) * params.fmDepth;
                carrierFreq += modSample * currentFreq;
                carrierFreq = std::clamp(carrierFreq, 5.0f, (float)sampleRate * 0.48f);
            }

            phase += 2.0 * M_PI * (double)carrierFreq / (double)sampleRate;
            if (phase > 2.0 * M_PI) phase = std::fmod(phase, 2.0 * M_PI);

            // --- 3. PRIMARY OSCILLATOR SAMPLE ---
            float oscSample = 0.0f;
            float normPhase = (float)(phase / (2.0 * M_PI));

            switch (params.waveform) {
                case WaveformType::Sine:
                case WaveformType::FM:
                    oscSample = std::sin((float)phase);
                    break;
                case WaveformType::Triangle:
                    oscSample = 4.0f * std::abs(normPhase - 0.5f) - 1.0f;
                    break;
                case WaveformType::Sawtooth:
                    oscSample = 2.0f * normPhase - 1.0f;
                    break;
                case WaveformType::Square: {
                    float pw = std::clamp(params.pulseWidth, 0.05f, 0.95f);
                    oscSample = (normPhase < pw) ? 1.0f : -1.0f;
                    break;
                }
                case WaveformType::WhiteNoise:
                    oscSample = noiseGen.Next(NoiseType::White);
                    break;
                case WaveformType::PinkNoise:
                    oscSample = noiseGen.Next(NoiseType::Pink);
                    break;
                case WaveformType::BrownNoise:
                    oscSample = noiseGen.Next(NoiseType::Brown);
                    break;
            }

            float sampleMix = oscSample * params.oscMix;

            // --- 4. SUB-OSCILLATOR LAYER ---
            if (params.subOscEnabled) {
                float subFreq = currentFreq * subOctaveMult;
                subPhase += 2.0 * M_PI * (double)subFreq / (double)sampleRate;
                if (subPhase > 2.0 * M_PI) subPhase = std::fmod(subPhase, 2.0 * M_PI);
                float normSubPhase = (float)(subPhase / (2.0 * M_PI));

                float subSample = 0.0f;
                switch (params.subWaveform) {
                    case WaveformType::Triangle:
                        subSample = 4.0f * std::abs(normSubPhase - 0.5f) - 1.0f;
                        break;
                    case WaveformType::Square:
                        subSample = (normSubPhase < 0.5f) ? 1.0f : -1.0f;
                        break;
                    case WaveformType::Sawtooth:
                        subSample = 2.0f * normSubPhase - 1.0f;
                        break;
                    default:
                        subSample = std::sin((float)subPhase);
                        break;
                }
                sampleMix += subSample * params.subMix;
            }

            // --- 5. NOISE LAYER ---
            if (params.noiseEnabled) {
                float noiseSample = noiseGen.Next(params.noiseType);
                float noiseEnv = std::exp(-std::max(0.1f, params.noiseDecay * 12.0f) * t);
                sampleMix += noiseSample * params.noiseMix * noiseEnv;
            }

            // --- 6. AMPLITUDE ENVELOPE (ADSR + PUNCH) ---
            float env = 0.0f;
            if (t < attack) {
                env = t / attack;
            } else if (t < attack + decay) {
                float decayNorm = (t - attack) / decay;
                env = 1.0f - (1.0f - sustainLevel) * decayNorm;
            } else if (t < attack + decay + sustainTime) {
                env = sustainLevel;
            } else {
                float releaseNorm = (t - (attack + decay + sustainTime)) / release;
                float rem = std::clamp(1.0f - releaseNorm, 0.0f, 1.0f);
                env = sustainLevel * rem * rem; // Quadratic fade out for smooth tail
            }

            // Transient Punch boost in initial 12ms
            if (params.punch > 0.01f && t < 0.012f) {
                float punchFactor = (1.0f - t / 0.012f);
                env *= (1.0f + params.punch * punchFactor * 1.5f);
            }

            buffer[i] = sampleMix * env;
        }

        // --- 7. DSP EFFECTS RACK (EXTENSIBLE PIPELINE) ---

        // A. Dynamic Biquad Filter
        if (params.filterEnabled && params.filterType != FilterType::None) {
            BiquadFilterEffect filter;
            filter.type = params.filterType;
            filter.cutoff = params.filterCutoff;
            filter.resonance = params.filterResonance;
            filter.cutoffSweep = params.filterCutoffSweep;
            filter.Process(buffer, sampleRate);
        }

        // B. Distortion / Saturation
        if (params.distortionEnabled) {
            DistortionEffect dist;
            dist.enabled = true;
            dist.type = params.distortionType;
            dist.drive = params.distortionDrive;
            dist.mix = params.distortionMix;
            dist.Process(buffer, sampleRate);
        }

        // C. Bitcrusher
        if (params.bitcrushEnabled) {
            BitcrusherEffect bitcrush;
            bitcrush.enabled = true;
            bitcrush.bitDepth = params.bitcrushBits;
            bitcrush.downsampleFactor = params.bitcrushDownsample;
            bitcrush.mix = params.bitcrushMix;
            bitcrush.Process(buffer, sampleRate);
        }

        // D. Delay / Echo
        if (params.delayEnabled) {
            DelayEffect delay;
            delay.enabled = true;
            delay.delayTimeMs = params.delayTimeMs;
            delay.feedback = params.delayFeedback;
            delay.damping = params.delayDamping;
            delay.mix = params.delayMix;
            delay.Process(buffer, sampleRate);
        }

        // E. Reverb
        if (params.reverbEnabled) {
            ReverbEffect reverb;
            reverb.enabled = true;
            reverb.roomSize = params.reverbRoomSize;
            reverb.damping = params.reverbDamping;
            reverb.mix = params.reverbMix;
            reverb.Process(buffer, sampleRate);
        }

        // F. Master Volume & Limiter
        LimiterEffect limiter;
        limiter.enabled = params.limiterEnabled;
        limiter.makeupGain = params.masterVolume;
        limiter.ceiling = 0.98f;
        limiter.Process(buffer, sampleRate);

        return buffer;
    }

    std::vector<int16_t> SoundSynth::GeneratePCM(const SynthSoundParams& params, uint32_t sampleRate) {
        std::vector<float> floatBuf = GenerateFloat(params, sampleRate);
        std::vector<int16_t> pcm(floatBuf.size());

        for (size_t i = 0; i < floatBuf.size(); i++) {
            float s = std::clamp(floatBuf[i], -1.0f, 1.0f);
            pcm[i] = (int16_t)(s * 32767.0f);
        }

        return pcm;
    }

    bool SoundSynth::WriteWavFile(const std::string& path, const std::vector<int16_t>& pcm, uint32_t sampleRate) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return false;

        uint32_t dataSize = (uint32_t)(pcm.size() * sizeof(int16_t));
        uint32_t chunkSize = 36 + dataSize;
        uint16_t audioFormat = 1; // PCM
        uint16_t numChannels = 1; // Mono
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
        if (dataSize > 0) {
            file.write((const char*)pcm.data(), dataSize);
        }
        return true;
    }

    // =========================================================================
    // GAME SOUND PRESETS LIBRARY
    // =========================================================================

    SynthSoundParams SoundSynth::GetPresetCannon() {
        SynthSoundParams p;
        p.name = "Cannon Blast";
        p.targetFilepath = "assets/audio/cannon.wav";

        // Heavy chest-thumping bass transient + mid crack
        p.waveform = WaveformType::Sine;
        p.baseFreq = 160.0f;
        p.targetFreq = 26.0f;
        p.pitchSlideCurve = PitchSlideType::Exponential;
        p.pitchSlideDuration = 0.28f;
        p.oscMix = 0.85f;

        // Sub-oscillator for sub-bass shockwave
        p.subOscEnabled = true;
        p.subWaveform = WaveformType::Sine;
        p.subOctave = -1;
        p.subMix = 0.65f;

        // Dense brown explosive noise
        p.noiseEnabled = true;
        p.noiseType = NoiseType::Brown;
        p.noiseMix = 0.65f;
        p.noiseDecay = 1.3f;

        // Envelope: Instant snappy attack, hefty decay
        p.attackTime = 0.002f;
        p.decayTime = 0.28f;
        p.sustainLevel = 0.0f;
        p.releaseTime = 0.12f;
        p.punch = 0.85f;

        // LowPass Filter sweeping down as blast dissipates
        p.filterEnabled = true;
        p.filterType = FilterType::LowPass;
        p.filterCutoff = 1600.0f;
        p.filterResonance = 1.6f;
        p.filterCutoffSweep = -950.0f;

        // Soft saturation for warm aggressive impact
        p.distortionEnabled = true;
        p.distortionType = DistortionType::SoftTanh;
        p.distortionDrive = 3.6f;
        p.distortionMix = 0.65f;

        // Subtle battlefield room reverb
        p.reverbEnabled = true;
        p.reverbRoomSize = 0.65f;
        p.reverbDamping = 0.35f;
        p.reverbMix = 0.22f;

        p.masterVolume = 1.0f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetRocket() {
        SynthSoundParams p;
        p.name = "Rocket Launch";
        p.targetFilepath = "assets/audio/rocket.wav";

        // Ascending tone + rushing turbulent air
        p.waveform = WaveformType::Triangle;
        p.baseFreq = 160.0f;
        p.targetFreq = 580.0f;
        p.pitchSlideCurve = PitchSlideType::SCurve;
        p.pitchSlideDuration = 0.35f;
        p.oscMix = 0.85f;

        p.subOscEnabled = true;
        p.subWaveform = WaveformType::Sine;
        p.subOctave = -1;
        p.subMix = 0.35f;

        // Pink noise thruster roar
        p.noiseEnabled = true;
        p.noiseType = NoiseType::Pink;
        p.noiseMix = 0.80f;
        p.noiseDecay = 0.5f;

        p.attackTime = 0.04f;
        p.decayTime = 0.22f;
        p.sustainLevel = 0.35f;
        p.sustainTime = 0.06f;
        p.releaseTime = 0.12f;
        p.punch = 0.3f;

        // BandPass filter tracking rocket exhaust
        p.filterEnabled = true;
        p.filterType = FilterType::BandPass;
        p.filterCutoff = 800.0f;
        p.filterResonance = 2.0f;
        p.filterCutoffSweep = 450.0f;

        p.distortionEnabled = true;
        p.distortionType = DistortionType::SoftTanh;
        p.distortionDrive = 2.0f;
        p.distortionMix = 0.4f;

        // Delay whoosh
        p.delayEnabled = true;
        p.delayTimeMs = 55.0f;
        p.delayFeedback = 0.35f;
        p.delayMix = 0.25f;

        p.masterVolume = 1.8f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetLaser() {
        SynthSoundParams p;
        p.name = "Laser Pew";
        p.targetFilepath = "assets/audio/laser.wav";

        // High-tech plasma beam: Exponential drop with FM harmonics
        p.waveform = WaveformType::Sawtooth;
        p.baseFreq = 2600.0f;
        p.targetFreq = 120.0f;
        p.pitchSlideCurve = PitchSlideType::Exponential;
        p.pitchSlideDuration = 0.13f;
        p.oscMix = 0.85f;

        // FM modulation gives modern sci-fi laser sizzle
        p.fmEnabled = true;
        p.fmRatio = 1.5f;
        p.fmDepth = 2.2f;

        p.attackTime = 0.001f;
        p.decayTime = 0.12f;
        p.sustainLevel = 0.0f;
        p.releaseTime = 0.04f;
        p.punch = 0.9f;

        // HighPass removes low rumble, keeps plasma crisp
        p.filterEnabled = true;
        p.filterType = FilterType::HighPass;
        p.filterCutoff = 180.0f;
        p.filterResonance = 1.2f;

        p.distortionEnabled = true;
        p.distortionType = DistortionType::SoftTanh;
        p.distortionDrive = 2.2f;
        p.distortionMix = 0.45f;

        // Quick sci-fi chirp reflection
        p.delayEnabled = true;
        p.delayTimeMs = 38.0f;
        p.delayFeedback = 0.25f;
        p.delayMix = 0.2f;

        p.masterVolume = 0.9f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetTurret() {
        SynthSoundParams p;
        p.name = "Turret Pulse";
        p.targetFilepath = "assets/audio/turret.wav";

        // Rapid snappy plasma tap with metallic Gatling bite
        p.waveform = WaveformType::Square;
        p.pulseWidth = 0.35f;
        p.baseFreq = 950.0f;
        p.targetFreq = 180.0f;
        p.pitchSlideCurve = PitchSlideType::Exponential;
        p.pitchSlideDuration = 0.09f;
        p.oscMix = 0.85f;

        p.noiseEnabled = true;
        p.noiseType = NoiseType::White;
        p.noiseMix = 0.25f;
        p.noiseDecay = 2.5f;

        p.attackTime = 0.001f;
        p.decayTime = 0.08f;
        p.sustainLevel = 0.0f;
        p.releaseTime = 0.03f;
        p.punch = 0.95f;

        p.filterEnabled = true;
        p.filterType = FilterType::BandPass;
        p.filterCutoff = 1100.0f;
        p.filterResonance = 2.2f;

        p.distortionEnabled = true;
        p.distortionType = DistortionType::HardClip;
        p.distortionDrive = 2.4f;
        p.distortionMix = 0.5f;

        p.masterVolume = 0.92f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetExplosion() {
        SynthSoundParams p;
        p.name = "Explosion";
        p.targetFilepath = "assets/audio/explosion.wav";

        // Heavy detonation thump + dense noise tail
        p.waveform = WaveformType::Sine;
        p.baseFreq = 85.0f;
        p.targetFreq = 22.0f;
        p.pitchSlideCurve = PitchSlideType::Exponential;
        p.pitchSlideDuration = 0.45f;
        p.oscMix = 0.75f;

        p.subOscEnabled = true;
        p.subWaveform = WaveformType::Sine;
        p.subOctave = -1;
        p.subMix = 0.75f;

        p.noiseEnabled = true;
        p.noiseType = NoiseType::Brown;
        p.noiseMix = 0.85f;
        p.noiseDecay = 0.7f;

        p.attackTime = 0.002f;
        p.decayTime = 0.42f;
        p.sustainLevel = 0.08f;
        p.sustainTime = 0.05f;
        p.releaseTime = 0.22f;
        p.punch = 0.95f;

        p.filterEnabled = true;
        p.filterType = FilterType::LowPass;
        p.filterCutoff = 2400.0f;
        p.filterResonance = 1.5f;
        p.filterCutoffSweep = -1800.0f;

        p.distortionEnabled = true;
        p.distortionType = DistortionType::SoftTanh;
        p.distortionDrive = 4.2f;
        p.distortionMix = 0.75f;

        p.reverbEnabled = true;
        p.reverbRoomSize = 0.75f;
        p.reverbDamping = 0.35f;
        p.reverbMix = 0.35f;

        p.masterVolume = 1.0f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetShieldHit() {
        SynthSoundParams p;
        p.name = "Shield Hit";
        p.targetFilepath = "assets/audio/shield_hit.wav";

        // Resonant harmonic ping + sizzle
        p.waveform = WaveformType::Sine;
        p.baseFreq = 980.0f;
        p.targetFreq = 540.0f;
        p.pitchSlideCurve = PitchSlideType::Exponential;
        p.pitchSlideDuration = 0.08f;
        p.oscMix = 0.95f;

        p.fmEnabled = true;
        p.fmRatio = 2.0f;
        p.fmDepth = 1.4f;

        p.noiseEnabled = true;
        p.noiseType = NoiseType::Pink;
        p.noiseMix = 0.35f;
        p.noiseDecay = 2.0f;

        p.attackTime = 0.001f;
        p.decayTime = 0.12f;
        p.sustainLevel = 0.0f;
        p.releaseTime = 0.06f;
        p.punch = 0.8f;

        p.filterEnabled = true;
        p.filterType = FilterType::BandPass;
        p.filterCutoff = 1750.0f;
        p.filterResonance = 3.5f;

        p.delayEnabled = true;
        p.delayTimeMs = 45.0f;
        p.delayFeedback = 0.35f;
        p.delayMix = 0.25f;

        p.masterVolume = 1.6f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetFootstep() {
        SynthSoundParams p;
        p.name = "Footstep";
        p.targetFilepath = "assets/audio/footstep.wav";

        // Low thud + ground grit crunch
        p.waveform = WaveformType::Sine;
        p.baseFreq = 80.0f;
        p.targetFreq = 32.0f;
        p.pitchSlideCurve = PitchSlideType::Exponential;
        p.pitchSlideDuration = 0.045f;
        p.oscMix = 0.65f;

        p.noiseEnabled = true;
        p.noiseType = NoiseType::Brown;
        p.noiseMix = 0.45f;
        p.noiseDecay = 2.2f;

        p.attackTime = 0.002f;
        p.decayTime = 0.045f;
        p.sustainLevel = 0.0f;
        p.releaseTime = 0.02f;
        p.punch = 0.65f;

        p.filterEnabled = true;
        p.filterType = FilterType::LowPass;
        p.filterCutoff = 750.0f;
        p.filterResonance = 1.0f;

        p.distortionEnabled = true;
        p.distortionType = DistortionType::SoftTanh;
        p.distortionDrive = 1.8f;
        p.distortionMix = 0.3f;

        p.masterVolume = 0.75f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetUISelect() {
        SynthSoundParams p;
        p.name = "UI Select";
        p.targetFilepath = "assets/audio/ui_select.wav";

        // Clean, pleasant dual-tone chirp
        p.waveform = WaveformType::Sine;
        p.baseFreq = 1200.0f;
        p.targetFreq = 1750.0f;
        p.pitchSlideCurve = PitchSlideType::SCurve;
        p.pitchSlideDuration = 0.035f;
        p.oscMix = 0.9f;

        p.attackTime = 0.002f;
        p.decayTime = 0.035f;
        p.sustainLevel = 0.0f;
        p.releaseTime = 0.02f;
        p.punch = 0.4f;

        p.reverbEnabled = true;
        p.reverbRoomSize = 0.4f;
        p.reverbDamping = 0.2f;
        p.reverbMix = 0.15f;

        p.masterVolume = 0.7f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetUIError() {
        SynthSoundParams p;
        p.name = "UI Error";
        p.targetFilepath = "assets/audio/ui_error.wav";

        // Buzzing cautionary dual pulse
        p.waveform = WaveformType::Square;
        p.pulseWidth = 0.4f;
        p.baseFreq = 200.0f;
        p.targetFreq = 160.0f;
        p.pitchSlideCurve = PitchSlideType::Linear;
        p.pitchSlideDuration = 0.14f;
        p.oscMix = 0.8f;

        // Vibrato buzz
        p.vibratoRate = 45.0f;
        p.vibratoDepth = 25.0f;

        p.attackTime = 0.002f;
        p.decayTime = 0.12f;
        p.sustainLevel = 0.0f;
        p.releaseTime = 0.03f;
        p.punch = 0.5f;

        p.distortionEnabled = true;
        p.distortionType = DistortionType::SoftTanh;
        p.distortionDrive = 2.0f;
        p.distortionMix = 0.4f;

        p.masterVolume = 0.75f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetBeam() {
        SynthSoundParams p;
        p.name = "Construction Beam";
        p.targetFilepath = "assets/audio/beam.wav";

        // Pulsing harmonic hum with subtle electrical sizzle
        p.waveform = WaveformType::Sawtooth;
        p.baseFreq = 220.0f;
        p.targetFreq = 220.0f;
        p.oscMix = 0.85f;

        p.fmEnabled = true;
        p.fmRatio = 3.0f;
        p.fmDepth = 1.2f;

        p.vibratoRate = 9.0f;
        p.vibratoDepth = 14.0f;

        p.noiseEnabled = true;
        p.noiseType = NoiseType::Pink;
        p.noiseMix = 0.2f;
        p.noiseDecay = 0.4f;

        p.attackTime = 0.05f;
        p.decayTime = 0.15f;
        p.sustainLevel = 0.65f;
        p.sustainTime = 0.25f;
        p.releaseTime = 0.10f;

        p.filterEnabled = true;
        p.filterType = FilterType::BandPass;
        p.filterCutoff = 1200.0f;
        p.filterResonance = 1.8f;

        p.delayEnabled = true;
        p.delayTimeMs = 60.0f;
        p.delayFeedback = 0.35f;
        p.delayMix = 0.25f;

        p.masterVolume = 1.4f;
        return p;
    }

    SynthSoundParams SoundSynth::GetPresetEMP() {
        SynthSoundParams p;
        p.name = "EMP Shockwave";
        p.targetFilepath = "assets/audio/emp.wav";

        // Dramatic wide frequency dive with electrical sizzle
        p.waveform = WaveformType::Sawtooth;
        p.baseFreq = 3400.0f;
        p.targetFreq = 32.0f;
        p.pitchSlideCurve = PitchSlideType::Exponential;
        p.pitchSlideDuration = 1.05f;
        p.oscMix = 0.75f;

        p.subOscEnabled = true;
        p.subWaveform = WaveformType::Sine;
        p.subOctave = -1;
        p.subMix = 0.55f;

        p.noiseEnabled = true;
        p.noiseType = NoiseType::Pink;
        p.noiseMix = 0.45f;
        p.noiseDecay = 0.6f;

        p.attackTime = 0.005f;
        p.decayTime = 0.85f;
        p.sustainLevel = 0.05f;
        p.sustainTime = 0.05f;
        p.releaseTime = 0.35f;
        p.punch = 0.85f;

        p.filterEnabled = true;
        p.filterType = FilterType::LowPass;
        p.filterCutoff = 3200.0f;
        p.filterResonance = 2.4f;
        p.filterCutoffSweep = -2800.0f;

        p.distortionEnabled = true;
        p.distortionType = DistortionType::Foldback;
        p.distortionDrive = 3.2f;
        p.distortionMix = 0.6f;

        p.reverbEnabled = true;
        p.reverbRoomSize = 0.85f;
        p.reverbDamping = 0.3f;
        p.reverbMix = 0.38f;

        p.masterVolume = 0.95f;
        return p;
    }

    std::vector<std::string> SoundSynth::GetPresetNames() {
        return {
            "Cannon Blast",
            "Rocket Launch",
            "Laser Pew",
            "Turret Pulse",
            "Explosion",
            "Shield Hit",
            "Footstep",
            "UI Select",
            "UI Error",
            "Construction Beam",
            "EMP Shockwave"
        };
    }

    SynthSoundParams SoundSynth::GetPresetByName(const std::string& name) {
        if (name == "Cannon Blast")       return GetPresetCannon();
        if (name == "Rocket Launch")      return GetPresetRocket();
        if (name == "Laser Pew")          return GetPresetLaser();
        if (name == "Turret Pulse")       return GetPresetTurret();
        if (name == "Explosion")          return GetPresetExplosion();
        if (name == "Shield Hit")         return GetPresetShieldHit();
        if (name == "Footstep")           return GetPresetFootstep();
        if (name == "UI Select")          return GetPresetUISelect();
        if (name == "UI Error")           return GetPresetUIError();
        if (name == "Construction Beam")  return GetPresetBeam();
        if (name == "EMP Shockwave")      return GetPresetEMP();
        return GetPresetCannon();
    }

    void SoundSynth::MutateParams(SynthSoundParams& p, float mutationAmount) {
        float m = std::clamp(mutationAmount, 0.01f, 1.0f);
        auto RandDelta = [&](float range) {
            float r = ((float)(std::rand() % 2001) / 1000.0f) - 1.0f;
            return r * range * m;
        };

        p.baseFreq = std::clamp(p.baseFreq * (1.0f + RandDelta(0.4f)), 20.0f, 8000.0f);
        p.targetFreq = std::clamp(p.targetFreq * (1.0f + RandDelta(0.4f)), 20.0f, 8000.0f);
        p.attackTime = std::clamp(p.attackTime + RandDelta(0.02f), 0.001f, 0.5f);
        p.decayTime = std::clamp(p.decayTime + RandDelta(0.1f), 0.01f, 2.0f);
        p.punch = std::clamp(p.punch + RandDelta(0.3f), 0.0f, 1.0f);

        if (p.filterEnabled) {
            p.filterCutoff = std::clamp(p.filterCutoff * (1.0f + RandDelta(0.35f)), 50.0f, 15000.0f);
            p.filterResonance = std::clamp(p.filterResonance + RandDelta(0.5f), 0.2f, 8.0f);
        }
        if (p.distortionEnabled) {
            p.distortionDrive = std::clamp(p.distortionDrive + RandDelta(1.5f), 1.0f, 15.0f);
        }
        if (p.fmEnabled) {
            p.fmDepth = std::clamp(p.fmDepth + RandDelta(1.0f), 0.0f, 8.0f);
        }
    }

    SynthSoundParams SoundSynth::GenerateRandom(WaveformType preferType) {
        SynthSoundParams p;
        p.name = "Procedural Sound";
        p.waveform = preferType;
        p.baseFreq = 100.0f + (float)(std::rand() % 1500);
        p.targetFreq = 30.0f + (float)(std::rand() % 1200);
        p.pitchSlideCurve = (PitchSlideType)(std::rand() % 3);
        p.pitchSlideDuration = 0.05f + (float)(std::rand() % 40) * 0.01f;

        p.attackTime = 0.001f + (float)(std::rand() % 10) * 0.005f;
        p.decayTime = 0.05f + (float)(std::rand() % 35) * 0.01f;
        p.releaseTime = 0.02f + (float)(std::rand() % 15) * 0.01f;
        p.punch = (float)(std::rand() % 100) * 0.01f;

        p.noiseEnabled = (std::rand() % 2 == 0);
        p.noiseType = (NoiseType)(std::rand() % 3);
        p.noiseMix = 0.1f + (float)(std::rand() % 40) * 0.01f;

        p.filterEnabled = (std::rand() % 2 == 0);
        p.filterType = (FilterType)(1 + (std::rand() % 3));
        p.filterCutoff = 300.0f + (float)(std::rand() % 4000);
        p.filterResonance = 1.0f + (float)(std::rand() % 20) * 0.1f;

        p.distortionEnabled = (std::rand() % 2 == 0);
        p.distortionDrive = 1.5f + (float)(std::rand() % 30) * 0.1f;

        p.masterVolume = 0.85f;
        return p;
    }

    // =========================================================================
    // JSON SERIALIZATION / PRESET PERSISTENCE
    // =========================================================================

    std::string SoundSynth::ToJson(const SynthSoundParams& p) {
        nlohmann::json j;
        j["name"] = p.name;
        j["targetFilepath"] = p.targetFilepath;
        j["waveform"] = (int)p.waveform;
        j["baseFreq"] = p.baseFreq;
        j["pulseWidth"] = p.pulseWidth;
        j["oscMix"] = p.oscMix;

        j["subOscEnabled"] = p.subOscEnabled;
        j["subWaveform"] = (int)p.subWaveform;
        j["subOctave"] = p.subOctave;
        j["subMix"] = p.subMix;

        j["noiseEnabled"] = p.noiseEnabled;
        j["noiseType"] = (int)p.noiseType;
        j["noiseMix"] = p.noiseMix;
        j["noiseDecay"] = p.noiseDecay;

        j["fmEnabled"] = p.fmEnabled;
        j["fmRatio"] = p.fmRatio;
        j["fmDepth"] = p.fmDepth;

        j["targetFreq"] = p.targetFreq;
        j["pitchSlideDuration"] = p.pitchSlideDuration;
        j["pitchSlideCurve"] = (int)p.pitchSlideCurve;

        j["pitchJumpEnabled"] = p.pitchJumpEnabled;
        j["pitchJumpTime"] = p.pitchJumpTime;
        j["pitchJumpAmount"] = p.pitchJumpAmount;

        j["vibratoRate"] = p.vibratoRate;
        j["vibratoDepth"] = p.vibratoDepth;
        j["vibratoDelay"] = p.vibratoDelay;

        j["attackTime"] = p.attackTime;
        j["decayTime"] = p.decayTime;
        j["sustainLevel"] = p.sustainLevel;
        j["sustainTime"] = p.sustainTime;
        j["releaseTime"] = p.releaseTime;
        j["punch"] = p.punch;

        j["filterEnabled"] = p.filterEnabled;
        j["filterType"] = (int)p.filterType;
        j["filterCutoff"] = p.filterCutoff;
        j["filterResonance"] = p.filterResonance;
        j["filterCutoffSweep"] = p.filterCutoffSweep;

        j["distortionEnabled"] = p.distortionEnabled;
        j["distortionType"] = (int)p.distortionType;
        j["distortionDrive"] = p.distortionDrive;
        j["distortionMix"] = p.distortionMix;

        j["bitcrushEnabled"] = p.bitcrushEnabled;
        j["bitcrushBits"] = p.bitcrushBits;
        j["bitcrushDownsample"] = p.bitcrushDownsample;
        j["bitcrushMix"] = p.bitcrushMix;

        j["delayEnabled"] = p.delayEnabled;
        j["delayTimeMs"] = p.delayTimeMs;
        j["delayFeedback"] = p.delayFeedback;
        j["delayDamping"] = p.delayDamping;
        j["delayMix"] = p.delayMix;

        j["reverbEnabled"] = p.reverbEnabled;
        j["reverbRoomSize"] = p.reverbRoomSize;
        j["reverbDamping"] = p.reverbDamping;
        j["reverbMix"] = p.reverbMix;

        j["masterVolume"] = p.masterVolume;
        j["limiterEnabled"] = p.limiterEnabled;

        return j.dump(4);
    }

    bool SoundSynth::FromJson(const std::string& jsonStr, SynthSoundParams& p) {
        try {
            nlohmann::json j = nlohmann::json::parse(jsonStr);
            if (j.contains("name")) p.name = j["name"].get<std::string>();
            if (j.contains("targetFilepath")) p.targetFilepath = j["targetFilepath"].get<std::string>();
            if (j.contains("waveform")) p.waveform = (WaveformType)j["waveform"].get<int>();
            if (j.contains("baseFreq")) p.baseFreq = j["baseFreq"].get<float>();
            if (j.contains("pulseWidth")) p.pulseWidth = j["pulseWidth"].get<float>();
            if (j.contains("oscMix")) p.oscMix = j["oscMix"].get<float>();

            if (j.contains("subOscEnabled")) p.subOscEnabled = j["subOscEnabled"].get<bool>();
            if (j.contains("subWaveform")) p.subWaveform = (WaveformType)j["subWaveform"].get<int>();
            if (j.contains("subOctave")) p.subOctave = j["subOctave"].get<int>();
            if (j.contains("subMix")) p.subMix = j["subMix"].get<float>();

            if (j.contains("noiseEnabled")) p.noiseEnabled = j["noiseEnabled"].get<bool>();
            if (j.contains("noiseType")) p.noiseType = (NoiseType)j["noiseType"].get<int>();
            if (j.contains("noiseMix")) p.noiseMix = j["noiseMix"].get<float>();
            if (j.contains("noiseDecay")) p.noiseDecay = j["noiseDecay"].get<float>();

            if (j.contains("fmEnabled")) p.fmEnabled = j["fmEnabled"].get<bool>();
            if (j.contains("fmRatio")) p.fmRatio = j["fmRatio"].get<float>();
            if (j.contains("fmDepth")) p.fmDepth = j["fmDepth"].get<float>();

            if (j.contains("targetFreq")) p.targetFreq = j["targetFreq"].get<float>();
            if (j.contains("pitchSlideDuration")) p.pitchSlideDuration = j["pitchSlideDuration"].get<float>();
            if (j.contains("pitchSlideCurve")) p.pitchSlideCurve = (PitchSlideType)j["pitchSlideCurve"].get<int>();

            if (j.contains("pitchJumpEnabled")) p.pitchJumpEnabled = j["pitchJumpEnabled"].get<bool>();
            if (j.contains("pitchJumpTime")) p.pitchJumpTime = j["pitchJumpTime"].get<float>();
            if (j.contains("pitchJumpAmount")) p.pitchJumpAmount = j["pitchJumpAmount"].get<float>();

            if (j.contains("vibratoRate")) p.vibratoRate = j["vibratoRate"].get<float>();
            if (j.contains("vibratoDepth")) p.vibratoDepth = j["vibratoDepth"].get<float>();
            if (j.contains("vibratoDelay")) p.vibratoDelay = j["vibratoDelay"].get<float>();

            if (j.contains("attackTime")) p.attackTime = j["attackTime"].get<float>();
            if (j.contains("decayTime")) p.decayTime = j["decayTime"].get<float>();
            if (j.contains("sustainLevel")) p.sustainLevel = j["sustainLevel"].get<float>();
            if (j.contains("sustainTime")) p.sustainTime = j["sustainTime"].get<float>();
            if (j.contains("releaseTime")) p.releaseTime = j["releaseTime"].get<float>();
            if (j.contains("punch")) p.punch = j["punch"].get<float>();

            if (j.contains("filterEnabled")) p.filterEnabled = j["filterEnabled"].get<bool>();
            if (j.contains("filterType")) p.filterType = (FilterType)j["filterType"].get<int>();
            if (j.contains("filterCutoff")) p.filterCutoff = j["filterCutoff"].get<float>();
            if (j.contains("filterResonance")) p.filterResonance = j["filterResonance"].get<float>();
            if (j.contains("filterCutoffSweep")) p.filterCutoffSweep = j["filterCutoffSweep"].get<float>();

            if (j.contains("distortionEnabled")) p.distortionEnabled = j["distortionEnabled"].get<bool>();
            if (j.contains("distortionType")) p.distortionType = (DistortionType)j["distortionType"].get<int>();
            if (j.contains("distortionDrive")) p.distortionDrive = j["distortionDrive"].get<float>();
            if (j.contains("distortionMix")) p.distortionMix = j["distortionMix"].get<float>();

            if (j.contains("bitcrushEnabled")) p.bitcrushEnabled = j["bitcrushEnabled"].get<bool>();
            if (j.contains("bitcrushBits")) p.bitcrushBits = j["bitcrushBits"].get<int>();
            if (j.contains("bitcrushDownsample")) p.bitcrushDownsample = j["bitcrushDownsample"].get<int>();
            if (j.contains("bitcrushMix")) p.bitcrushMix = j["bitcrushMix"].get<float>();

            if (j.contains("delayEnabled")) p.delayEnabled = j["delayEnabled"].get<bool>();
            if (j.contains("delayTimeMs")) p.delayTimeMs = j["delayTimeMs"].get<float>();
            if (j.contains("delayFeedback")) p.delayFeedback = j["delayFeedback"].get<float>();
            if (j.contains("delayDamping")) p.delayDamping = j["delayDamping"].get<float>();
            if (j.contains("delayMix")) p.delayMix = j["delayMix"].get<float>();

            if (j.contains("reverbEnabled")) p.reverbEnabled = j["reverbEnabled"].get<bool>();
            if (j.contains("reverbRoomSize")) p.reverbRoomSize = j["reverbRoomSize"].get<float>();
            if (j.contains("reverbDamping")) p.reverbDamping = j["reverbDamping"].get<float>();
            if (j.contains("reverbMix")) p.reverbMix = j["reverbMix"].get<float>();

            if (j.contains("masterVolume")) p.masterVolume = j["masterVolume"].get<float>();
            if (j.contains("limiterEnabled")) p.limiterEnabled = j["limiterEnabled"].get<bool>();

            return true;
        } catch (...) {
            return false;
        }
    }

    bool SoundSynth::SavePresetFile(const std::string& filepath, const SynthSoundParams& params) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(filepath).parent_path(), ec);
        std::ofstream file(filepath);
        if (!file.is_open()) return false;
        file << ToJson(params);
        return true;
    }

    bool SoundSynth::LoadPresetFile(const std::string& filepath, SynthSoundParams& outParams) {
        std::ifstream file(filepath);
        if (!file.is_open()) return false;
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        return FromJson(content, outParams);
    }

} // namespace Crescendo
