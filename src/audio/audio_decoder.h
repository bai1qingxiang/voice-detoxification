#pragma once

#include <string>

namespace audio {

struct DecodeResult {
    std::string normalized_wav_path;
};

DecodeResult decode_to_mono16k_wav(
    const std::string& input_path,
    const std::string& ffmpeg_path = "ffmpeg");

void remove_file_if_exists(const std::string& path) noexcept;

} // namespace audio