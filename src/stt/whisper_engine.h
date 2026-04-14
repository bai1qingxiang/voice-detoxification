#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct WhisperSegment {
    int64_t t0_ms = 0;
    int64_t t1_ms = 0;
    std::string text;
};

struct WhisperResult {
    std::string full_text;
    std::vector<WhisperSegment> segments;
};

class WhisperEngine {
public:
    explicit WhisperEngine(const std::string& model_path);
    ~WhisperEngine();

    WhisperEngine(const WhisperEngine&) = delete;
    WhisperEngine& operator=(const WhisperEngine&) = delete;

    bool is_loaded() const;

    WhisperResult transcribe_wav(const std::string& wav_path, int n_threads = 0) const;

    WhisperResult transcribe_audio_file(
        const std::string& audio_path,
        const std::string& ffmpeg_path = "ffmpeg",
        int n_threads = 0) const;

private:
    struct whisper_context* ctx_ = nullptr;
};