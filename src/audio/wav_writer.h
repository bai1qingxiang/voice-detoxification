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
    // 将PCM样本写入WAV文件
    static void write_wav(
        const std::string& output_path,
        const std::vector<int16_t>& samples,
        int sample_rate,
        int num_channels = 1);

    // 读取WAV文件
    static WAVFile read_wav(const std::string& input_path);

    // 合并两个WAV文件（串联）
    static WAVFile concatenate_wav(const WAVFile& first, const WAVFile& second);

    // 混合两个WAV文件（叠加）
    static WAVFile mix_wav(const WAVFile& first, const WAVFile& second, float ratio = 0.5f);
};

} // namespace audio
