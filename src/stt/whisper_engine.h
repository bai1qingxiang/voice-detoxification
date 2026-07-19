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
    /// 从磁盘加载 Whisper 模型。
    explicit WhisperEngine(const std::string& model_path);
    /// 释放已加载的 Whisper 上下文。
    ~WhisperEngine();

    /// 禁止复制原生 Whisper 上下文的所有权。
    WhisperEngine(const WhisperEngine&) = delete;
    /// 禁止对原生 Whisper 上下文执行复制赋值。
    WhisperEngine& operator=(const WhisperEngine&) = delete;

    /// 返回模型是否已准备好执行转写。
    bool is_loaded() const;

    /// 转写规范化的单声道 16 kHz WAV，并生成词元时间戳。
    WhisperResult transcribe_wav(const std::string& wav_path, int n_threads = 0) const;

    /// 解码并转写受支持的音频输入文件。
    WhisperResult transcribe_audio_file(
        const std::string& audio_path,
        const std::string& ffmpeg_path = "ffmpeg",
        int n_threads = 0) const;

private:
    struct whisper_context* ctx_ = nullptr;
};
