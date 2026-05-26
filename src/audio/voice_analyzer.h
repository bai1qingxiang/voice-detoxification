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
    static VoiceCharacteristics analyze(
        const std::vector<int16_t>& pcm_samples,
        int sample_rate);

    // 分析WAV文件
    static VoiceCharacteristics analyze_file(const std::string& wav_path);

private:
    // 计算音频的能量
    static std::vector<float> compute_energy(
        const std::vector<int16_t>& samples,
        int frame_size = 512);

    // 使用简单的方法估计基频
    static float estimate_fundamental_frequency(
        const std::vector<int16_t>& samples,
        int sample_rate);

    // 计算过零率
    static float compute_zcr(const std::vector<int16_t>& samples);

    // 计算频谱特征
    static float compute_spectral_centroid(
        const std::vector<int16_t>& samples,
        int sample_rate);
};

class VoiceConverter {
public:
    // 将一个音频的音色特征应用到另一个音频
    static std::vector<int16_t> transfer_voice_characteristics(
        const std::vector<int16_t>& target_audio,
        const VoiceCharacteristics& source_characteristics,
        int sample_rate);

    // 调整音频的基频
    static std::vector<int16_t> adjust_pitch(
        const std::vector<int16_t>& audio,
        float pitch_shift_semitones,
        int sample_rate);

    // 调整音频的速度
    static std::vector<int16_t> adjust_speed(
        const std::vector<int16_t>& audio,
        float speed_factor);

    // 调整音频的能量
    static std::vector<int16_t> adjust_energy(
        const std::vector<int16_t>& audio,
        float energy_factor);

    // 简单的时间拉伸（改变速度但保持基频）
    static std::vector<int16_t> time_stretch(
        const std::vector<int16_t>& audio,
        float stretch_factor);
};

} // namespace audio
