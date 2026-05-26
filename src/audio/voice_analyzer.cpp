#include "audio/voice_analyzer.h"

#include <cmath>
#include <algorithm>
#include <numeric>
#include <iostream>
#include <fstream>

namespace audio {

namespace {

// 简单的自相关基频检测
float autocorrelation_pitch_detection(
    const std::vector<float>& signal,
    int sample_rate) {

    if (signal.size() < 4096) {
        return 100.0f;  // 默认基频
    }

    const int frame_size = 2048;
    std::vector<float> frame(signal.begin(), signal.begin() + frame_size);

    // 标准化
    float mean = std::accumulate(frame.begin(), frame.end(), 0.0f) / frame.size();
    for (auto& s : frame) {
        s -= mean;
    }

    // 计算自相关
    std::vector<float> autocorr(frame_size / 2);
    for (size_t lag = 0; lag < autocorr.size(); ++lag) {
        float sum = 0.0f;
        for (size_t i = 0; i < frame.size() - lag; ++i) {
            sum += frame[i] * frame[i + lag];
        }
        autocorr[lag] = sum;
    }

    // 找到第一个显著峰值（对应基频）
    float max_corr = autocorr[0];
    int best_lag = 1;
    const int min_lag = sample_rate / 500;  // 最大500Hz
    const int max_lag = sample_rate / 50;   // 最小50Hz

    for (int lag = min_lag; lag < std::min(max_lag, (int)autocorr.size()); ++lag) {
        if (autocorr[lag] > max_corr * 0.5f && autocorr[lag] > autocorr[best_lag]) {
            best_lag = lag;
        }
    }

    float fundamental_freq = static_cast<float>(sample_rate) / best_lag;
    return fundamental_freq;
}

// 读取WAV文件
std::pair<std::vector<int16_t>, int> read_wav_file_legacy(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    // 跳过WAV头
    file.seekg(24);
    int sample_rate;
    file.read(reinterpret_cast<char*>(&sample_rate), 4);

    // 找到data块
    file.seekg(0);
    char chunk_id[4];
    while (file.read(chunk_id, 4)) {
        uint32_t size;
        file.read(reinterpret_cast<char*>(&size), 4);

        if (std::string(chunk_id, 4) == "data") {
            std::vector<int16_t> samples(size / 2);
            file.read(reinterpret_cast<char*>(samples.data()), size);
            return {samples, sample_rate};
        } else {
            file.seekg(size, std::ios::cur);
        }
    }

    throw std::runtime_error("No data chunk in WAV file");
}

std::pair<std::vector<int16_t>, int> read_wav_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open file: " + path);
    }

    char riff[4];
    file.read(riff, 4);
    if (std::string(riff, 4) != "RIFF") {
        throw std::runtime_error("Invalid WAV file: missing RIFF chunk");
    }

    uint32_t riff_size;
    file.read(reinterpret_cast<char*>(&riff_size), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::string(wave, 4) != "WAVE") {
        throw std::runtime_error("Invalid WAV file: missing WAVE marker");
    }

    int sample_rate = 0;
    std::vector<int16_t> samples;

    while (file) {
        char chunk_id[4];
        file.read(chunk_id, 4);
        if (!file) {
            break;
        }

        uint32_t size;
        file.read(reinterpret_cast<char*>(&size), 4);

        const std::string id(chunk_id, 4);
        if (id == "fmt ") {
            uint16_t audio_format = 0;
            uint16_t channels = 0;
            file.read(reinterpret_cast<char*>(&audio_format), 2);
            file.read(reinterpret_cast<char*>(&channels), 2);
            file.read(reinterpret_cast<char*>(&sample_rate), 4);

            if (size > 8) {
                file.seekg(static_cast<std::streamoff>(size - 8), std::ios::cur);
            }
        } else if (id == "data") {
            if (size % sizeof(int16_t) != 0) {
                throw std::runtime_error("Invalid WAV data chunk size");
            }

            samples.resize(size / sizeof(int16_t));
            file.read(reinterpret_cast<char*>(samples.data()), size);
            break;
        } else {
            file.seekg(size, std::ios::cur);
        }

        if (size % 2 == 1) {
            file.seekg(1, std::ios::cur);
        }
    }

    if (sample_rate <= 0) {
        throw std::runtime_error("No fmt chunk in WAV file");
    }

    if (samples.empty()) {
        throw std::runtime_error("No data chunk in WAV file");
    }

    return {samples, sample_rate};
}

} // namespace

VoiceCharacteristics VoiceAnalyzer::analyze(
    const std::vector<int16_t>& pcm_samples,
    int sample_rate) {

    VoiceCharacteristics chars;

    if (pcm_samples.empty()) {
        return chars;
    }

    // 转换为浮点数
    std::vector<float> float_samples(pcm_samples.size());
    for (size_t i = 0; i < pcm_samples.size(); ++i) {
        float_samples[i] = static_cast<float>(pcm_samples[i]) / 32768.0f;
    }

    // 计算基频
    chars.mean_pitch = autocorrelation_pitch_detection(float_samples, sample_rate);

    // 计算能量
    float energy_sum = 0.0f;
    float energy_sq_sum = 0.0f;
    for (float s : float_samples) {
        energy_sum += s * s;
    }
    chars.energy_mean = std::sqrt(energy_sum / float_samples.size());
    chars.energy_variance = chars.energy_mean * 0.1f;  // 粗略估计

    // 计算过零率
    chars.zero_crossing_rate = compute_zcr(pcm_samples);

    // 计算频谱质心
    chars.spectral_centroid = compute_spectral_centroid(pcm_samples, sample_rate);

    // 估计基频方差
    chars.pitch_variance = chars.mean_pitch * 0.1f;

    return chars;
}

VoiceCharacteristics VoiceAnalyzer::analyze_file(const std::string& wav_path) {
    auto [samples, sample_rate] = read_wav_file(wav_path);
    return analyze(samples, sample_rate);
}

std::vector<float> VoiceAnalyzer::compute_energy(
    const std::vector<int16_t>& samples,
    int frame_size) {

    std::vector<float> energy;
    for (size_t i = 0; i + frame_size <= samples.size(); i += frame_size) {
        float e = 0.0f;
        for (int j = 0; j < frame_size; ++j) {
            float s = static_cast<float>(samples[i + j]) / 32768.0f;
            e += s * s;
        }
        energy.push_back(std::sqrt(e / frame_size));
    }
    return energy;
}

float VoiceAnalyzer::estimate_fundamental_frequency(
    const std::vector<int16_t>& samples,
    int sample_rate) {

    if (samples.size() < 4096) {
        return 100.0f;
    }

    std::vector<float> float_samples(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        float_samples[i] = static_cast<float>(samples[i]) / 32768.0f;
    }

    return autocorrelation_pitch_detection(float_samples, sample_rate);
}

float VoiceAnalyzer::compute_zcr(const std::vector<int16_t>& samples) {
    if (samples.size() < 2) return 0.0f;

    int zero_crossings = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if ((samples[i] >= 0 && samples[i - 1] < 0) ||
            (samples[i] < 0 && samples[i - 1] >= 0)) {
            zero_crossings++;
        }
    }

    return static_cast<float>(zero_crossings) / (samples.size() - 1);
}

float VoiceAnalyzer::compute_spectral_centroid(
    const std::vector<int16_t>& samples,
    int sample_rate) {

    // 简化：使用过零率作为频谱特性的代理
    float zcr = compute_zcr(samples);
    return zcr * sample_rate / 2.0f;  // 缩放到Hz
}

std::vector<int16_t> VoiceConverter::transfer_voice_characteristics(
    const std::vector<int16_t>& target_audio,
    const VoiceCharacteristics& source_characteristics,
    int sample_rate) {

    std::vector<int16_t> result = target_audio;

    // 分析目标音频
    VoiceCharacteristics target_chars = VoiceAnalyzer::analyze(target_audio, sample_rate);

    // 计算需要调整的因子
    float pitch_shift = source_characteristics.mean_pitch / (target_chars.mean_pitch + 0.001f);
    float energy_factor = (source_characteristics.energy_mean) /
                         (target_chars.energy_mean + 0.001f);

    // 应用能量调整
    result = adjust_energy(result, std::min(energy_factor, 2.0f));

    // 应用基频调整（通过改变采样率）
    float semitones = 12.0f * std::log2(pitch_shift);
    result = adjust_pitch(result, semitones, sample_rate);

    return result;
}

std::vector<int16_t> VoiceConverter::adjust_pitch(
    const std::vector<int16_t>& audio,
    float pitch_shift_semitones,
    int sample_rate) {

    // 简单的方法：改变采样速率来改变基频
    // 这会同时改变速度和基频，所以我们需要时间拉伸来补偿
    float pitch_factor = std::pow(2.0f, pitch_shift_semitones / 12.0f);

    // 首先改变基频
    std::vector<int16_t> pitched = audio;
    if (pitch_factor > 0.5f && pitch_factor < 2.0f) {
        // 简单的线性插值重采样
        std::vector<int16_t> resampled;
        float pos = 0.0f;

        while (pos < static_cast<int>(audio.size()) - 1) {
            int idx = static_cast<int>(pos);
            float frac = pos - idx;

            float val = (1.0f - frac) * audio[idx] + frac * audio[idx + 1];
            resampled.push_back(static_cast<int16_t>(std::clamp(val, -32768.0f, 32767.0f)));

            pos += pitch_factor;
        }

        pitched = resampled;
    }

    return pitched;
}

std::vector<int16_t> VoiceConverter::adjust_speed(
    const std::vector<int16_t>& audio,
    float speed_factor) {

    if (speed_factor <= 0.0f) {
        return audio;
    }

    // 简单的重采样方法
    std::vector<int16_t> result;
    float pos = 0.0f;

    while (pos < static_cast<int>(audio.size()) - 1) {
        int idx = static_cast<int>(pos);
        float frac = pos - idx;

        float val = (1.0f - frac) * audio[idx] + frac * audio[idx + 1];
        result.push_back(static_cast<int16_t>(std::clamp(val, -32768.0f, 32767.0f)));

        pos += speed_factor;
    }

    return result;
}

std::vector<int16_t> VoiceConverter::adjust_energy(
    const std::vector<int16_t>& audio,
    float energy_factor) {

    std::vector<int16_t> result(audio.size());
    for (size_t i = 0; i < audio.size(); ++i) {
        float val = static_cast<float>(audio[i]) * energy_factor;
        result[i] = static_cast<int16_t>(std::clamp(val, -32768.0f, 32767.0f));
    }
    return result;
}

std::vector<int16_t> VoiceConverter::time_stretch(
    const std::vector<int16_t>& audio,
    float stretch_factor) {

    // 简单的时间拉伸（改变速度但保持基频）
    // 这是一个简化版本，真正的实现需要更复杂的算法
    return adjust_speed(audio, 1.0f / stretch_factor);
}

} // namespace audio
