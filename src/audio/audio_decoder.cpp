#include "audio/audio_decoder.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <random>
#include <iostream>

namespace fs = std::filesystem;

namespace {

/// 为 FFmpeg 命令中的文件路径添加引号。
std::string quote_file_arg(const std::string& s) {
    return "\"" + s + "\"";
}

/// 在可执行文件路径包含空白字符时添加引号。
std::string quote_executable_if_needed(const std::string& s) {
    if (s.find(' ') != std::string::npos || s.find('\t') != std::string::npos) {
        return "\"" + s + "\"";
    }
    return s;
}

/// 规范化 Windows 路径分隔符，便于命令行使用。
std::string normalize_path_for_cmd(std::string s) {
    for (char& c : s) {
        if (c == '\\') {
            c = '/';
        }
    }
    return s;
}

/// 创建不易发生命名冲突的临时 WAV 路径。
std::string make_temp_wav_path() {
    const fs::path temp_dir = fs::temp_directory_path();

    const auto now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    std::mt19937_64 rng(static_cast<unsigned long long>(now));
    const auto r = rng();

    const fs::path out =
        temp_dir / ("voice_detox_" + std::to_string(now) + "_" + std::to_string(r) + ".wav");

    return out.string();
}

} // namespace

namespace audio {

/// 使用 FFmpeg 将任意输入音频解码为单声道 16 kHz PCM WAV。
DecodeResult decode_to_mono16k_wav(
    const std::string& input_path,
    const std::string& ffmpeg_path) {

    if (!fs::exists(input_path)) {
        throw std::runtime_error("Input audio file not found: " + input_path);
    }

    const std::string output_wav = make_temp_wav_path();

    const std::string ffmpeg_cmd_path =
        normalize_path_for_cmd(ffmpeg_path);

    const std::string input_cmd_path =
        normalize_path_for_cmd(fs::absolute(input_path).string());

    const std::string output_cmd_path =
        normalize_path_for_cmd(fs::absolute(output_wav).string());

    std::ostringstream cmd;
    cmd << quote_executable_if_needed(ffmpeg_cmd_path)
        << " -y -hide_banner -loglevel error"
        << " -i " << quote_file_arg(input_cmd_path)
        << " -vn"
        << " -ac 1"
        << " -ar 16000"
        << " -c:a pcm_s16le "
        << quote_file_arg(output_cmd_path);

    std::cout << "[DEBUG] ffmpeg cmd: " << cmd.str() << std::endl;

    const int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        throw std::runtime_error(
            "ffmpeg decode failed. Make sure ffmpeg is installed and available in PATH.");
    }

    if (!fs::exists(output_wav)) {
        throw std::runtime_error("ffmpeg finished but output WAV was not created.");
    }

    return DecodeResult{output_wav};
}

/// 尽力删除临时文件，并忽略清理过程中的异常。
void remove_file_if_exists(const std::string& path) noexcept {
    try {
        if (!path.empty() && fs::exists(path)) {
            fs::remove(path);
        }
    } catch (...) {
    }
}

} // namespace audio
