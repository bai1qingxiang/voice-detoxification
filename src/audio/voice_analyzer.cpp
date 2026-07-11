#include "audio/voice_analyzer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>

namespace audio {

namespace {

float autocorrelation_pitch_detection(
    const std::vector<float>& signal,
    int sample_rate) {

    if (signal.size() < 1024 || sample_rate <= 0) {
        return 0.0f;
    }

    const int frame_size = std::min<int>(4096, static_cast<int>(signal.size()));
    std::vector<float> frame(signal.begin(), signal.begin() + frame_size);

    const float mean = std::accumulate(frame.begin(), frame.end(), 0.0f) / frame.size();
    for (auto& sample : frame) {
        sample -= mean;
    }

    float frame_energy = 0.0f;
    for (float sample : frame) {
        frame_energy += sample * sample;
    }
    if (frame_energy < 1.0e-6f) {
        return 0.0f;
    }

    const int min_lag = std::max(1, sample_rate / 500);
    const int max_lag = std::min(sample_rate / 50, frame_size - 2);
    if (max_lag <= min_lag) {
        return 0.0f;
    }

    float best_corr = 0.0f;
    int best_lag = 0;
    for (int lag = min_lag; lag <= max_lag; ++lag) {
        float numerator = 0.0f;
        float energy_a = 0.0f;
        float energy_b = 0.0f;
        for (int i = 0; i + lag < frame_size; ++i) {
            numerator += frame[i] * frame[i + lag];
            energy_a += frame[i] * frame[i];
            energy_b += frame[i + lag] * frame[i + lag];
        }

        const float denominator = std::sqrt(energy_a * energy_b);
        if (denominator <= 1.0e-6f) {
            continue;
        }

        const float normalized_corr = numerator / denominator;
        if (normalized_corr > best_corr) {
            best_corr = normalized_corr;
            best_lag = lag;
        }
    }

    if (best_lag <= 0 || best_corr < 0.30f ||
        best_lag == min_lag || best_lag == max_lag) {
        return 0.0f;
    }

    const float fundamental_freq = static_cast<float>(sample_rate) / best_lag;
    if (fundamental_freq < 70.0f || fundamental_freq > 400.0f) {
        return 0.0f;
    }

    return fundamental_freq;
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

    uint32_t riff_size = 0;
    file.read(reinterpret_cast<char*>(&riff_size), 4);

    char wave[4];
    file.read(wave, 4);
    if (std::string(wave, 4) != "WAVE") {
        throw std::runtime_error("Invalid WAV file: missing WAVE marker");
    }

    uint16_t audio_format = 0;
    uint16_t channels = 0;
    uint16_t bits_per_sample = 0;
    int sample_rate = 0;
    std::vector<int16_t> samples;

    while (file) {
        char chunk_id[4];
        file.read(chunk_id, 4);
        if (!file) {
            break;
        }

        uint32_t size = 0;
        file.read(reinterpret_cast<char*>(&size), 4);
        if (!file) {
            break;
        }

        const std::string id(chunk_id, 4);
        if (id == "fmt ") {
            file.read(reinterpret_cast<char*>(&audio_format), 2);
            file.read(reinterpret_cast<char*>(&channels), 2);
            file.read(reinterpret_cast<char*>(&sample_rate), 4);

            uint32_t byte_rate = 0;
            uint16_t block_align = 0;
            file.read(reinterpret_cast<char*>(&byte_rate), 4);
            file.read(reinterpret_cast<char*>(&block_align), 2);
            file.read(reinterpret_cast<char*>(&bits_per_sample), 2);

            if (size > 16) {
                file.seekg(static_cast<std::streamoff>(size - 16), std::ios::cur);
            }
        } else if (id == "data") {
            if (audio_format != 1 || bits_per_sample != 16 || channels == 0) {
                throw std::runtime_error("Only 16-bit PCM WAV files are supported for voice analysis");
            }
            if (size % sizeof(int16_t) != 0) {
                throw std::runtime_error("Invalid WAV data chunk size");
            }

            std::vector<int16_t> raw(size / sizeof(int16_t));
            file.read(reinterpret_cast<char*>(raw.data()), size);

            if (channels == 1) {
                samples = std::move(raw);
            } else {
                samples.reserve(raw.size() / channels);
                for (size_t i = 0; i + channels <= raw.size(); i += channels) {
                    int sum = 0;
                    for (uint16_t channel = 0; channel < channels; ++channel) {
                        sum += raw[i + channel];
                    }
                    samples.push_back(static_cast<int16_t>(sum / channels));
                }
            }
            break;
        } else {
            file.seekg(static_cast<std::streamoff>(size), std::ios::cur);
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

    std::vector<float> float_samples(pcm_samples.size());
    for (size_t i = 0; i < pcm_samples.size(); ++i) {
        float_samples[i] = static_cast<float>(pcm_samples[i]) / 32768.0f;
    }

    chars.mean_pitch = autocorrelation_pitch_detection(float_samples, sample_rate);

    float energy_sum = 0.0f;
    for (float sample : float_samples) {
        energy_sum += sample * sample;
    }
    chars.energy_mean = std::sqrt(energy_sum / float_samples.size());

    const auto frame_energy = compute_energy(pcm_samples);
    if (!frame_energy.empty()) {
        const float mean_energy = std::accumulate(frame_energy.begin(), frame_energy.end(), 0.0f) /
                                  frame_energy.size();
        float variance_sum = 0.0f;
        for (float energy : frame_energy) {
            const float delta = energy - mean_energy;
            variance_sum += delta * delta;
        }
        chars.energy_variance = variance_sum / frame_energy.size();
    }

    chars.zero_crossing_rate = compute_zcr(pcm_samples);
    chars.spectral_centroid = compute_spectral_centroid(pcm_samples, sample_rate);
    chars.pitch_variance = chars.mean_pitch > 0.0f ? chars.mean_pitch * 0.1f : 0.0f;

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
    if (frame_size <= 0) {
        return energy;
    }

    for (size_t i = 0; i + frame_size <= samples.size(); i += frame_size) {
        float frame_sum = 0.0f;
        for (int j = 0; j < frame_size; ++j) {
            const float sample = static_cast<float>(samples[i + j]) / 32768.0f;
            frame_sum += sample * sample;
        }
        energy.push_back(std::sqrt(frame_sum / frame_size));
    }
    return energy;
}

float VoiceAnalyzer::estimate_fundamental_frequency(
    const std::vector<int16_t>& samples,
    int sample_rate) {

    if (samples.size() < 1024) {
        return 0.0f;
    }

    std::vector<float> float_samples(samples.size());
    for (size_t i = 0; i < samples.size(); ++i) {
        float_samples[i] = static_cast<float>(samples[i]) / 32768.0f;
    }

    return autocorrelation_pitch_detection(float_samples, sample_rate);
}

float VoiceAnalyzer::compute_zcr(const std::vector<int16_t>& samples) {
    if (samples.size() < 2) {
        return 0.0f;
    }

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

    const float zcr = compute_zcr(samples);
    return zcr * sample_rate / 2.0f;
}

std::vector<int16_t> VoiceConverter::transfer_voice_characteristics(
    const std::vector<int16_t>& target_audio,
    const VoiceCharacteristics& source_characteristics,
    int sample_rate) {

    if (target_audio.empty()) {
        return target_audio;
    }

    const VoiceCharacteristics target_chars = VoiceAnalyzer::analyze(target_audio, sample_rate);

    float energy_factor = 1.0f;
    if (source_characteristics.energy_mean > 0.001f &&
        target_chars.energy_mean > 0.001f) {
        energy_factor = source_characteristics.energy_mean / target_chars.energy_mean;
    }

    if (!std::isfinite(energy_factor)) {
        energy_factor = 1.0f;
    }

    // This is not voice cloning. Avoid pitch/time resampling and only apply a
    // small loudness match so the synthesized voice stays natural.
    energy_factor = std::clamp(energy_factor, 0.85f, 1.15f);
    return adjust_energy(target_audio, energy_factor);
}

std::vector<int16_t> VoiceConverter::adjust_pitch(
    const std::vector<int16_t>& audio,
    float pitch_shift_semitones,
    int sample_rate) {

    (void)sample_rate;
    const float pitch_factor = std::pow(2.0f, pitch_shift_semitones / 12.0f);
    if (pitch_factor <= 0.5f || pitch_factor >= 2.0f) {
        return audio;
    }

    std::vector<int16_t> resampled;
    float pos = 0.0f;
    while (pos < static_cast<int>(audio.size()) - 1) {
        const int idx = static_cast<int>(pos);
        const float frac = pos - idx;
        const float val = (1.0f - frac) * audio[idx] + frac * audio[idx + 1];
        resampled.push_back(static_cast<int16_t>(std::clamp(val, -32768.0f, 32767.0f)));
        pos += pitch_factor;
    }

    return resampled;
}

std::vector<int16_t> VoiceConverter::adjust_speed(
    const std::vector<int16_t>& audio,
    float speed_factor) {

    if (speed_factor <= 0.0f) {
        return audio;
    }

    std::vector<int16_t> result;
    float pos = 0.0f;
    while (pos < static_cast<int>(audio.size()) - 1) {
        const int idx = static_cast<int>(pos);
        const float frac = pos - idx;
        const float val = (1.0f - frac) * audio[idx] + frac * audio[idx + 1];
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
        const float val = static_cast<float>(audio[i]) * energy_factor;
        result[i] = static_cast<int16_t>(std::clamp(val, -32768.0f, 32767.0f));
    }
    return result;
}

std::vector<int16_t> VoiceConverter::time_stretch(
    const std::vector<int16_t>& audio,
    float stretch_factor) {

    if (stretch_factor <= 0.0f) {
        return audio;
    }
    return adjust_speed(audio, 1.0f / stretch_factor);
}

} // namespace audio
