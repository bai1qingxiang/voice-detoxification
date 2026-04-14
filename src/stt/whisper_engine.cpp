#include "stt/whisper_engine.h"

#include "audio/audio_decoder.h"
#include "whisper.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct WavData {
    int sample_rate = 0;
    int channels = 0;
    int bits_per_sample = 0;
    std::vector<float> pcmf32;
};

template <typename T>
T read_little_endian(std::ifstream& in) {
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    if (!in) {
        throw std::runtime_error("Failed to read WAV file.");
    }
    return value;
}

bool is_supported_audio_file(const fs::path& path) {
    if (!fs::is_regular_file(path)) {
        return false;
    }

    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    return ext == ".mp3" ||
           ext == ".wav" ||
           ext == ".m4a" ||
           ext == ".flac" ||
           ext == ".ogg" ||
           ext == ".aac" ||
           ext == ".wma";
}

std::string find_first_audio_file_in_directory(const fs::path& dir) {
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        return "";
    }

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (is_supported_audio_file(entry.path())) {
            return entry.path().string();
        }
    }

    return "";
}

std::string resolve_input_audio_path(const std::string& audio_path) {
    // 1) 传入的是存在的文件：直接用
    if (!audio_path.empty()) {
        fs::path p(audio_path);

        if (fs::exists(p) && fs::is_regular_file(p)) {
            return p.string();
        }

        // 2) 传入的是目录：就在目录里找第一个支持的音频
        if (fs::exists(p) && fs::is_directory(p)) {
            const std::string found = find_first_audio_file_in_directory(p);
            if (!found.empty()) {
                return found;
            }
        }
    }

    // 3) 默认扫 tests/inputs
    {
        const std::string found = find_first_audio_file_in_directory("tests/inputs");
        if (!found.empty()) {
            return found;
        }
    }

    // 4) 兼容旧目录 tests/input
    {
        const std::string found = find_first_audio_file_in_directory("tests/input");
        if (!found.empty()) {
            return found;
        }
    }

    if (!audio_path.empty()) {
        throw std::runtime_error(
            "Input audio file not found, and no supported audio file was found in tests/inputs or tests/input.");
    }

    throw std::runtime_error(
        "No supported audio file found in tests/inputs or tests/input.");
}

WavData load_wav_pcm_s16le_mono_16k(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Failed to open WAV file: " + path);
    }

    char riff[4];
    in.read(riff, 4);
    if (std::strncmp(riff, "RIFF", 4) != 0) {
        throw std::runtime_error("Invalid WAV file: missing RIFF.");
    }

    (void) read_little_endian<std::uint32_t>(in);

    char wave[4];
    in.read(wave, 4);
    if (std::strncmp(wave, "WAVE", 4) != 0) {
        throw std::runtime_error("Invalid WAV file: missing WAVE.");
    }

    bool found_fmt = false;
    bool found_data = false;

    std::uint16_t audio_format = 0;
    std::uint16_t num_channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits_per_sample = 0;
    std::vector<std::int16_t> pcm16;

    while (in && (!found_fmt || !found_data)) {
        char chunk_id[4];
        in.read(chunk_id, 4);
        if (!in) {
            break;
        }

        const std::uint32_t chunk_size = read_little_endian<std::uint32_t>(in);

        if (std::strncmp(chunk_id, "fmt ", 4) == 0) {
            audio_format = read_little_endian<std::uint16_t>(in);
            num_channels = read_little_endian<std::uint16_t>(in);
            sample_rate = read_little_endian<std::uint32_t>(in);
            (void) read_little_endian<std::uint32_t>(in);
            (void) read_little_endian<std::uint16_t>(in);
            bits_per_sample = read_little_endian<std::uint16_t>(in);

            if (chunk_size > 16) {
                in.seekg(static_cast<std::streamoff>(chunk_size - 16), std::ios::cur);
            }

            found_fmt = true;
        } else if (std::strncmp(chunk_id, "data", 4) == 0) {
            if (chunk_size % sizeof(std::int16_t) != 0) {
                throw std::runtime_error("Unsupported WAV data size.");
            }

            pcm16.resize(chunk_size / sizeof(std::int16_t));
            in.read(reinterpret_cast<char*>(pcm16.data()), static_cast<std::streamsize>(chunk_size));
            if (!in) {
                throw std::runtime_error("Failed to read WAV PCM data.");
            }

            found_data = true;
        } else {
            in.seekg(static_cast<std::streamoff>(chunk_size), std::ios::cur);
        }

        if (chunk_size % 2 == 1) {
            in.seekg(1, std::ios::cur);
        }
    }

    if (!found_fmt) {
        throw std::runtime_error("Invalid WAV file: missing fmt chunk.");
    }
    if (!found_data) {
        throw std::runtime_error("Invalid WAV file: missing data chunk.");
    }
    if (audio_format != 1) {
        throw std::runtime_error("Only PCM WAV is supported for now.");
    }
    if (num_channels != 1) {
        throw std::runtime_error("Only mono WAV is supported for now.");
    }
    if (sample_rate != 16000) {
        throw std::runtime_error("Only 16 kHz WAV is supported for now.");
    }
    if (bits_per_sample != 16) {
        throw std::runtime_error("Only 16-bit WAV is supported for now.");
    }

    WavData out;
    out.sample_rate = static_cast<int>(sample_rate);
    out.channels = static_cast<int>(num_channels);
    out.bits_per_sample = static_cast<int>(bits_per_sample);
    out.pcmf32.resize(pcm16.size());

    constexpr float scale = 1.0f / 32768.0f;
    for (size_t i = 0; i < pcm16.size(); ++i) {
        out.pcmf32[i] = static_cast<float>(pcm16[i]) * scale;
    }

    return out;
}

int resolve_thread_count(int requested) {
    if (requested > 0) {
        return requested;
    }

    const unsigned hc = std::thread::hardware_concurrency();
    if (hc == 0) {
        return 4;
    }

    return static_cast<int>(std::max(1u, hc));
}

} // namespace

WhisperEngine::WhisperEngine(const std::string& model_path) {
    whisper_context_params cparams = whisper_context_default_params();

    cparams.use_gpu = false;
    cparams.flash_attn = false;

    ctx_ = whisper_init_from_file_with_params(model_path.c_str(), cparams);

    if (!ctx_) {
        throw std::runtime_error("Failed to load Whisper model: " + model_path);
    }
}

WhisperEngine::~WhisperEngine() {
    if (ctx_) {
        whisper_free(ctx_);
        ctx_ = nullptr;
    }
}

bool WhisperEngine::is_loaded() const {
    return ctx_ != nullptr;
}

WhisperResult WhisperEngine::transcribe_wav(const std::string& wav_path, int n_threads) const {
    if (!ctx_) {
        throw std::runtime_error("Whisper model is not loaded.");
    }

    std::cout << "[DEBUG] loading wav: " << wav_path << std::endl;
    const WavData wav = load_wav_pcm_s16le_mono_16k(wav_path);

    std::cout << "[DEBUG] wav loaded, sample_rate=" << wav.sample_rate
              << ", channels=" << wav.channels
              << ", bits=" << wav.bits_per_sample
              << ", samples=" << wav.pcmf32.size() << std::endl;

    whisper_full_params params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads = 1 ;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.translate = false;
    params.no_context = true;
    params.single_segment = false;
    params.no_timestamps = false;
    params.language = "en";
    params.detect_language = false;

    std::cout << "[DEBUG] calling whisper_full..." << std::endl;

    const int rc = whisper_full(
        ctx_,
        params,
        wav.pcmf32.data(),
        static_cast<int>(wav.pcmf32.size()));

    std::cout << "[DEBUG] whisper_full returned: " << rc << std::endl;

    if (rc != 0) {
        throw std::runtime_error("whisper_full() failed.");
    }

    WhisperResult result;

    const int n_segments = whisper_full_n_segments(ctx_);
    std::cout << "[DEBUG] n_segments = " << n_segments << std::endl;

    for (int i = 0; i < n_segments; ++i) {
        WhisperSegment seg;
        seg.t0_ms = static_cast<int64_t>(whisper_full_get_segment_t0(ctx_, i)) * 10;
        seg.t1_ms = static_cast<int64_t>(whisper_full_get_segment_t1(ctx_, i)) * 10;
        seg.text = whisper_full_get_segment_text(ctx_, i);

        result.full_text += seg.text;
        result.segments.push_back(std::move(seg));
    }

    return result;
}

WhisperResult WhisperEngine::transcribe_audio_file(
    const std::string& audio_path,
    const std::string& ffmpeg_path,
    int n_threads) const {

    const std::string resolved_audio_path = resolve_input_audio_path(audio_path);
    std::cout << "[DEBUG] resolved audio path: " << resolved_audio_path << std::endl;

    const auto decoded = audio::decode_to_mono16k_wav(resolved_audio_path, ffmpeg_path);
    std::cout << "[DEBUG] decoded wav path: " << decoded.normalized_wav_path << std::endl;

    try {
        WhisperResult result = transcribe_wav(decoded.normalized_wav_path, n_threads);
        audio::remove_file_if_exists(decoded.normalized_wav_path);
        return result;
    } catch (...) {
        audio::remove_file_if_exists(decoded.normalized_wav_path);
        throw;
    }
}