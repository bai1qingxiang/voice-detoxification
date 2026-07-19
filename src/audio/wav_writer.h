#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace audio {

struct WAVFile {
    std::vector<int16_t> samples;
    int sample_rate = 16000;
    int num_channels = 1;
};

class WAVWriter {
public:
    /// 将 PCM 样本写入标准 WAV 文件。
    static void write_wav(
        const std::string& output_path,
        const std::vector<int16_t>& samples,
        int sample_rate,
        int num_channels = 1);

    /// 将 WAV 文件读取到内存中。
    static WAVFile read_wav(const std::string& input_path);

    /// 串联两个采样率一致的 WAV 缓冲区。
    static WAVFile concatenate_wav(const WAVFile& first, const WAVFile& second);

    /// 按指定比例混合两个 WAV 缓冲区。
    static WAVFile mix_wav(const WAVFile& first, const WAVFile& second, float ratio = 0.5f);
};

} // namespace audio
