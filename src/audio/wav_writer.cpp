#include "audio/wav_writer.h"

#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cstring>

namespace audio {

namespace {

struct WAVHeader {
    char riff[4];
    uint32_t chunk_size;
    char wave[4];
    char fmt[4];
    uint32_t subchunk1_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t subchunk2_size;
};

} // namespace

/// 将 16 位 PCM 样本序列化为标准 WAV 文件。
void WAVWriter::write_wav(
    const std::string& output_path,
    const std::vector<int16_t>& samples,
    int sample_rate,
    int num_channels) {

    std::ofstream file(output_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open output file: " + output_path);
    }

    WAVHeader header;
    std::memcpy(header.riff, "RIFF", 4);
    std::memcpy(header.wave, "WAVE", 4);
    std::memcpy(header.fmt, "fmt ", 4);
    std::memcpy(header.data, "data", 4);

    header.subchunk1_size = 16;
    header.audio_format = 1;  // PCM
    header.num_channels = num_channels;
    header.sample_rate = sample_rate;
    header.bits_per_sample = 16;
    header.block_align = num_channels * header.bits_per_sample / 8;
    header.byte_rate = sample_rate * header.block_align;
    header.subchunk2_size = samples.size() * sizeof(int16_t);
    header.chunk_size = 36 + header.subchunk2_size;

    // Write header
    file.write(reinterpret_cast<const char*>(&header), sizeof(WAVHeader));

    // Write samples
    file.write(reinterpret_cast<const char*>(samples.data()),
               samples.size() * sizeof(int16_t));

    if (!file) {
        throw std::runtime_error("Failed to write WAV file: " + output_path);
    }

    file.close();
}

/// 将标准 16 位 PCM WAV 文件读取到内存中。
WAVFile WAVWriter::read_wav(const std::string& input_path) {
    std::ifstream file(input_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open WAV file: " + input_path);
    }

    WAVHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(WAVHeader));

    if (std::string(header.riff, 4) != "RIFF" ||
        std::string(header.wave, 4) != "WAVE" ||
        header.audio_format != 1) {
        throw std::runtime_error("Invalid WAV file format");
    }

    WAVFile wav_file;
    wav_file.sample_rate = header.sample_rate;
    wav_file.num_channels = header.num_channels;

    // Read samples
    size_t num_samples = header.subchunk2_size / sizeof(int16_t);
    wav_file.samples.resize(num_samples);
    file.read(reinterpret_cast<char*>(wav_file.samples.data()),
              header.subchunk2_size);

    if (!file) {
        throw std::runtime_error("Failed to read WAV file: " + input_path);
    }

    return wav_file;
}

/// 串联两个采样率一致的 WAV 缓冲区。
WAVFile WAVWriter::concatenate_wav(const WAVFile& first, const WAVFile& second) {
    if (first.sample_rate != second.sample_rate) {
        throw std::runtime_error("Sample rates don't match");
    }

    WAVFile result = first;
    result.samples.insert(result.samples.end(),
                         second.samples.begin(),
                         second.samples.end());
    return result;
}

/// 按指定比例混合两个 WAV 缓冲区的重叠样本。
WAVFile WAVWriter::mix_wav(const WAVFile& first, const WAVFile& second, float ratio) {
    size_t min_size = std::min(first.samples.size(), second.samples.size());

    WAVFile result = first;
    result.samples.resize(min_size);

    for (size_t i = 0; i < min_size; ++i) {
        float mixed = (1.0f - ratio) * first.samples[i] + ratio * second.samples[i];
        result.samples[i] = static_cast<int16_t>(
            std::clamp(mixed, -32768.0f, 32767.0f));
    }

    return result;
}

} // namespace audio
