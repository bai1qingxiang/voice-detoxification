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
    explicit PiperEngine(const std::string& model_path);
    ~PiperEngine();

    PiperEngine(const PiperEngine&) = delete;
    PiperEngine& operator=(const PiperEngine&) = delete;

    bool is_loaded() const;

    TTSResult synthesize(const std::string& text);

    // Get available voices/speakers
    std::vector<std::string> get_available_voices() const;

private:
    std::string model_path_;
    struct piper_config* config_ = nullptr;
    struct piper_voice* voice_ = nullptr;
    bool loaded_ = false;
};

} // namespace tts
