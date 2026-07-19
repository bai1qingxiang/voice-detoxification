#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct WhisperSegment {
    int64_t t0_ms = 0;
    int64_t t1_ms = 0;
    std::string text;
};

struct WhisperToken {
    int64_t t0_ms = 0;
    int64_t t1_ms = 0;
    size_t text_start = 0;
    size_t text_end = 0;
    std::string text;
};

struct WhisperResult {
    std::string full_text;
    int64_t audio_duration_ms = 0;
    std::vector<WhisperSegment> segments;
    std::vector<WhisperToken> tokens;
};

class WhisperEngine {
public:
    /// Loads a Whisper model from disk.
    explicit WhisperEngine(const std::string& model_path);
    /// Releases the loaded Whisper context.
    ~WhisperEngine();

    /// Prevents copying ownership of the native Whisper context.
    WhisperEngine(const WhisperEngine&) = delete;
    /// Prevents copy assignment of the native Whisper context.
    WhisperEngine& operator=(const WhisperEngine&) = delete;

    /// Reports whether the model is ready for transcription.
    bool is_loaded() const;

    /// Transcribes a normalized mono 16 kHz WAV with token timestamps.
    WhisperResult transcribe_wav(const std::string& wav_path, int n_threads = 0) const;

    /// Decodes and transcribes a supported audio input file.
    WhisperResult transcribe_audio_file(
        const std::string& audio_path,
        const std::string& ffmpeg_path = "ffmpeg",
        int n_threads = 0) const;

private:
    struct whisper_context* ctx_ = nullptr;
};
