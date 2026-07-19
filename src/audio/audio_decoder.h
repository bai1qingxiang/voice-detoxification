#pragma once

#include <string>

namespace audio {

struct DecodeResult {
    std::string normalized_wav_path;
};

/// Decodes an audio file into a temporary mono 16 kHz PCM WAV.
DecodeResult decode_to_mono16k_wav(
    const std::string& input_path,
    const std::string& ffmpeg_path = "ffmpeg");

/// Removes a file if present and ignores cleanup failures.
void remove_file_if_exists(const std::string& path) noexcept;

} // namespace audio
