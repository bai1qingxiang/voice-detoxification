#pragma once

#include <string>

namespace audio {

struct DecodeResult {
    std::string normalized_wav_path;
};

/// 将音频文件解码为临时的单声道 16 kHz PCM WAV。
DecodeResult decode_to_mono16k_wav(
    const std::string& input_path,
    const std::string& ffmpeg_path = "ffmpeg");

/// 删除存在的文件，并忽略清理失败。
void remove_file_if_exists(const std::string& path) noexcept;

} // namespace audio
