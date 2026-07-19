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
    /// 验证 Piper 语音模型路径。
    explicit PiperEngine(const std::string& model_path);
    /// 释放 Piper 引擎资源。
    ~PiperEngine();

    /// 禁止复制 Piper 模型的所有权状态。
    PiperEngine(const PiperEngine&) = delete;
    /// 禁止对 Piper 模型所有权状态执行复制赋值。
    PiperEngine& operator=(const PiperEngine&) = delete;

    /// 返回引擎是否具有可用的语音模型。
    bool is_loaded() const;

    /// 将 UTF-8 文本合成为单声道 PCM 音频。
    TTSResult synthesize(const std::string& text);

    // Get available voices/speakers
    /// 返回受支持的内置英文语音标识符。
    std::vector<std::string> get_available_voices() const;

private:
    std::string model_path_;
    struct piper_config* config_ = nullptr;
    struct piper_voice* voice_ = nullptr;
    bool loaded_ = false;
};

} // namespace tts
