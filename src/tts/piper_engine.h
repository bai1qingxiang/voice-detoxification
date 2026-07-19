#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace tts {

struct TTSResult {
    std::vector<int16_t> audio_samples;  // PCM 16-bit samples
    int sample_rate = 22050;              // 22.05kHz
    int num_channels = 1;                 // Mono
    float duration_seconds = 0.0f;
};

class PiperEngine {
public:
    /// Validates a Piper voice model path.
    explicit PiperEngine(const std::string& model_path);
    /// Releases Piper engine resources.
    ~PiperEngine();

    /// Prevents copying Piper model ownership state.
    PiperEngine(const PiperEngine&) = delete;
    /// Prevents copy assignment of Piper model ownership state.
    PiperEngine& operator=(const PiperEngine&) = delete;

    /// Reports whether the engine has a usable voice model.
    bool is_loaded() const;

    /// Synthesizes UTF-8 text into mono PCM audio.
    TTSResult synthesize(const std::string& text);

    // Get available voices/speakers
    /// Returns supported built-in English voice identifiers.
    std::vector<std::string> get_available_voices() const;

private:
    std::string model_path_;
    struct piper_config* config_ = nullptr;
    struct piper_voice* voice_ = nullptr;
    bool loaded_ = false;
};

} // namespace tts
