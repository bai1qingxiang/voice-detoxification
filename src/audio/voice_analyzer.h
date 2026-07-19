#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace audio {

struct VoiceCharacteristics {
    float mean_pitch = 0.0f;           // 平均基频
    float pitch_variance = 0.0f;       // 基频方差
    float energy_mean = 0.0f;          // 平均能量
    float energy_variance = 0.0f;      // 能量方差
    float spectral_centroid = 0.0f;    // 频谱质心
    float zero_crossing_rate = 0.0f;   // 过零率
};

class VoiceAnalyzer {
public:
    // 从音频数据提取音色特征
    /// Extracts voice characteristics from in-memory PCM samples.
    static VoiceCharacteristics analyze(
        const std::vector<int16_t>& pcm_samples,
        int sample_rate);

    // 分析WAV文件
    /// Loads and analyzes a 16-bit PCM WAV file.
    static VoiceCharacteristics analyze_file(const std::string& wav_path);

private:
    // 计算音频的能量
    /// Computes frame-level RMS energy values.
    static std::vector<float> compute_energy(
        const std::vector<int16_t>& samples,
        int frame_size = 512);

    // 使用简单的方法估计基频
    /// Estimates the fundamental frequency of a voice signal.
    static float estimate_fundamental_frequency(
        const std::vector<int16_t>& samples,
        int sample_rate);

    // 计算过零率
    /// Computes normalized zero-crossing rate.
    static float compute_zcr(const std::vector<int16_t>& samples);

    // 计算频谱特征
    /// Approximates the spectral centroid of PCM samples.
    static float compute_spectral_centroid(
        const std::vector<int16_t>& samples,
        int sample_rate);
};

class VoiceConverter {
public:
    // 将一个音频的音色特征应用到另一个音频
    /// Matches bounded source loudness characteristics onto target audio.
    static std::vector<int16_t> transfer_voice_characteristics(
        const std::vector<int16_t>& target_audio,
        const VoiceCharacteristics& source_characteristics,
        int sample_rate);

    // 调整音频的基频
    /// Adjusts PCM pitch by a semitone offset.
    static std::vector<int16_t> adjust_pitch(
        const std::vector<int16_t>& audio,
        float pitch_shift_semitones,
        int sample_rate);

    // 调整音频的速度
    /// Adjusts PCM playback speed by a multiplicative factor.
    static std::vector<int16_t> adjust_speed(
        const std::vector<int16_t>& audio,
        float speed_factor);

    // 调整音频的能量
    /// Scales PCM sample energy with clipping protection.
    static std::vector<int16_t> adjust_energy(
        const std::vector<int16_t>& audio,
        float energy_factor);

    // 简单的时间拉伸（改变速度但保持基频）
    /// Adjusts PCM duration by a stretch factor.
    static std::vector<int16_t> time_stretch(
        const std::vector<int16_t>& audio,
        float stretch_factor);
};

} // namespace audio
