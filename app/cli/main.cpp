#include <exception>
#include <iostream>
#include <iomanip>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <vector>

#include "common/logger.h"
#include "stt/whisper_engine.h"
#include "nlp/text_detoxifier.h"
#include "audio/audio_decoder.h"

namespace fs = std::filesystem;

namespace {

/// 判断路径是否指向受支持的音频输入格式。
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

/// 查找目录中的第一个受支持音频文件。
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

/// 将指定文件、目录或默认测试输入解析为实际音频文件。
std::string resolve_audio_path_for_analysis(const std::string& audio_path) {
    if (!audio_path.empty()) {
        fs::path p(audio_path);
        if (fs::exists(p) && fs::is_regular_file(p)) {
            return p.string();
        }
        if (fs::exists(p) && fs::is_directory(p)) {
            const std::string found = find_first_audio_file_in_directory(p);
            if (!found.empty()) {
                return found;
            }
        }
    }

    std::string found = find_first_audio_file_in_directory("tests/inputs");
    if (!found.empty()) {
        return found;
    }

    return find_first_audio_file_in_directory("tests/input");
}

/// 为文件路径添加引号，使其可安全用作命令行参数。
std::string quote_file_arg(const std::string& s) {
    return "\"" + s + "\"";
}

/// 仅在可执行文件路径包含空白字符时添加引号。
std::string quote_executable_if_needed(const std::string& s) {
    if (s.find(' ') != std::string::npos || s.find('\t') != std::string::npos) {
        return "\"" + s + "\"";
    }
    return s;
}

/// 将反斜杠转换为正斜杠，便于 Windows 命令执行。
std::string normalize_path_for_cmd(std::string s) {
    for (char& c : s) {
        if (c == '\\') {
            c = '/';
        }
    }
    return s;
}

/// 将干净的源音频转换为 PCM WAV，同时保持语音内容不变。
void convert_original_audio_to_wav(
    const std::string& input_path,
    const std::string& output_path,
    const std::string& ffmpeg_path) {

    const fs::path output_fs_path(output_path);
    if (output_fs_path.has_parent_path()) {
        fs::create_directories(output_fs_path.parent_path());
    }

    const std::string ffmpeg_cmd_path = normalize_path_for_cmd(ffmpeg_path);
    const std::string input_cmd_path = normalize_path_for_cmd(fs::absolute(input_path).string());
    const std::string output_cmd_path = normalize_path_for_cmd(fs::absolute(output_path).string());

    std::ostringstream cmd;
    cmd << quote_executable_if_needed(ffmpeg_cmd_path)
        << " -y -hide_banner -loglevel error"
        << " -i " << quote_file_arg(input_cmd_path)
        << " -vn"
        << " -c:a pcm_s16le "
        << quote_file_arg(output_cmd_path);

    std::cout << "[DEBUG] ffmpeg copy-clean cmd: " << cmd.str() << std::endl;

    const int rc = std::system(cmd.str().c_str());
    if (rc != 0 || !fs::exists(output_path)) {
        throw std::runtime_error("Failed to write clean WAV copy with ffmpeg.");
    }
}

struct MuteRange {
    double start_seconds = 0.0;
    double end_seconds = 0.0;
};

/// 将有毒文本偏移映射为带边界扩展且已合并的音频时间范围。
std::vector<MuteRange> build_mute_ranges(
    const WhisperResult& transcription,
    const std::vector<nlp::ToxicityMatch>& matches) {

    std::vector<MuteRange> ranges;
    for (const auto& match : matches) {
        double start = std::numeric_limits<double>::max();
        double end = -1.0;
        double point_timestamp = -1.0;

        for (const auto& token : transcription.tokens) {
            const bool overlaps = token.text_start < match.end_pos && token.text_end > match.start_pos;
            if (overlaps && token.t0_ms >= 0 && token.t1_ms > token.t0_ms) {
                start = std::min(start, token.t0_ms / 1000.0);
                end = std::max(end, token.t1_ms / 1000.0);
            } else if (overlaps && token.t0_ms >= 0) {
                if (point_timestamp < 0.0) point_timestamp = token.t0_ms / 1000.0;
            }
        }

        // Whisper sometimes collapses the last tokens to one timestamp at the
        // end of its 30-second window. Use that point through the actual audio
        // tail instead of estimating it from the whole segment.
        if (end <= start && point_timestamp >= 0.0) {
            start = point_timestamp;
            end = transcription.audio_duration_ms / 1000.0;
        }

        // Token timestamps can occasionally be unavailable. In that case,
        // estimate the word span inside its timestamped Whisper segment.
        if (end <= start) {
            size_t segment_text_start = 0;
            for (const auto& segment : transcription.segments) {
                const size_t segment_text_end = segment_text_start + segment.text.size();
                const bool overlaps = segment_text_start < match.end_pos &&
                                      segment_text_end > match.start_pos;
                if (overlaps && !segment.text.empty() && segment.t1_ms > segment.t0_ms) {
                    const size_t local_start = std::max(match.start_pos, segment_text_start) - segment_text_start;
                    const size_t local_end = std::min(match.end_pos, segment_text_end) - segment_text_start;
                    const double duration = (segment.t1_ms - segment.t0_ms) / 1000.0;
                    start = segment.t0_ms / 1000.0 + duration * local_start / segment.text.size();
                    end = segment.t0_ms / 1000.0 + duration * local_end / segment.text.size();
                    break;
                }
                segment_text_start = segment_text_end;
            }
        }

        if (end > start) {
            constexpr double padding_seconds = 0.08;
            ranges.push_back({std::max(0.0, start - padding_seconds), end + padding_seconds});
        }
    }

    std::sort(ranges.begin(), ranges.end(), [](const MuteRange& a, const MuteRange& b) {
        return a.start_seconds < b.start_seconds;
    });

    std::vector<MuteRange> merged;
    for (const auto& range : ranges) {
        if (!merged.empty() && range.start_seconds <= merged.back().end_seconds + 0.03) {
            merged.back().end_seconds = std::max(merged.back().end_seconds, range.end_seconds);
        } else {
            merged.push_back(range);
        }
    }
    return merged;
}

/// 写出 WAV 文件，仅将有毒范围静音并保留其他原始音频。
void mute_audio_ranges_to_wav(
    const std::string& input_path,
    const std::string& output_path,
    const std::string& ffmpeg_path,
    const std::vector<MuteRange>& ranges) {

    if (ranges.empty()) throw std::runtime_error("No timestamp ranges were available for audio redaction.");

    const fs::path output_fs_path(output_path);
    if (output_fs_path.has_parent_path()) fs::create_directories(output_fs_path.parent_path());

    std::ostringstream filter;
    filter << std::fixed << std::setprecision(3);
    for (size_t i = 0; i < ranges.size(); ++i) {
        if (i > 0) filter << ',';
        filter << "volume=0:enable='between(t," << ranges[i].start_seconds
               << ',' << ranges[i].end_seconds << ")'";
    }

    std::ostringstream cmd;
    cmd << quote_executable_if_needed(normalize_path_for_cmd(ffmpeg_path))
        << " -y -hide_banner -loglevel error"
        << " -i " << quote_file_arg(normalize_path_for_cmd(fs::absolute(input_path).string()))
        << " -vn -af \"" << filter.str() << "\""
        << " -c:a pcm_s16le "
        << quote_file_arg(normalize_path_for_cmd(fs::absolute(output_path).string()));

    std::cout << "[DEBUG] ffmpeg silence-redaction cmd: " << cmd.str() << std::endl;
    const int rc = std::system(cmd.str().c_str());
    if (rc != 0 || !fs::exists(output_path)) {
        throw std::runtime_error("Failed to write silence-redacted WAV with ffmpeg.");
    }
}

} // namespace

/// 输出命令行选项和使用示例。
void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options] [whisper_model] [audio_file]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --help              Show this help message\n";
    std::cout << "  --output FILE       Save output audio to FILE (default: output.wav)\n";
    std::cout << "  --skip-audio        Only transcribe and report detected words\n";
    std::cout << "  --ffmpeg PATH       Path to ffmpeg executable (default: ffmpeg)\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  whisper_model       Path to Whisper GGML model (default: models/ggml-medium.bin)\n";
    std::cout << "  audio_file          Path to audio file or directory (auto-detected if empty)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " models/ggml-small.bin audio.mp3\n";
    std::cout << "  " << program_name << " --output clean.wav models/ggml-small.bin audio.mp3\n";
}

/// 执行语音转写、有毒内容检测和基于时间戳的静音处理。
int main(int argc, char** argv) {
    try {
        log_info("=== Voice Detoxification Complete Pipeline ===");

        // Parse arguments
        std::string model_path = "models/ggml-medium.bin";
        std::string audio_path = "";
        std::string ffmpeg_path = "ffmpeg";
        std::string output_path = "output.wav";
        bool skip_audio = false;

        // Parse command line
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--help") {
                print_usage(argv[0]);
                return 0;
            } else if (arg == "--skip-audio") {
                skip_audio = true;
            } else if (arg == "--output" && i + 1 < argc) {
                output_path = argv[++i];
            } else if (arg == "--ffmpeg" && i + 1 < argc) {
                ffmpeg_path = argv[++i];
            } else if (arg[0] != '-') {
                // Positional arguments
                if (model_path == "models/ggml-medium.bin") {
                    model_path = arg;
                } else if (audio_path.empty()) {
                    audio_path = arg;
                }
            }
        }

        std::cout << "\n" << std::string(70, '=') << "\n";
        std::cout << "STAGE 1: SPEECH-TO-TEXT TRANSCRIPTION\n";
        std::cout << std::string(70, '=') << "\n\n";

        // Initialize transcriber
        log_info("Loading Whisper STT model...");
        WhisperEngine whisper(model_path);

        if (!whisper.is_loaded()) {
            throw std::runtime_error("Failed to load Whisper model");
        }

        // Transcribe audio
        log_info("Transcribing audio...");
        WhisperResult transcription = whisper.transcribe_audio_file(audio_path, ffmpeg_path);

        std::cout << "✓ Transcription complete\n";
        std::cout << "Original text:\n" << transcription.full_text << "\n\n";

        std::cout << std::string(70, '=') << "\n";
        std::cout << "STAGE 2: TOXICITY DETECTION & TEXT DETOXIFICATION\n";
        std::cout << std::string(70, '=') << "\n\n";

        // Initialize detoxifier
        log_info("Analyzing content for toxic language...");
        nlp::TextDetoxifier detoxifier;

        // Process transcription for toxicity
        auto detoxified = detoxifier.detoxify(transcription.full_text);

        std::cout << detoxified.report << "\n";

        if (detoxified.original_toxicity != nlp::ToxicityLevel::CLEAN) {
            std::cout << "Detoxified text:\n" << detoxified.detoxified << "\n\n";
        } else {
            std::cout << "✓ No toxic content detected - text is clean.\n\n";
        }

        if (skip_audio) {
            log_info("Audio output skipped (--skip-audio)");
            std::cout << "\nProcessing complete. Audio output was skipped.\n";
            return 0;
        }

        if (detoxified.original_toxicity == nlp::ToxicityLevel::CLEAN) {
            const std::string source_audio_path = resolve_audio_path_for_analysis(audio_path);
            if (source_audio_path.empty()) {
                throw std::runtime_error("Could not find source audio to preserve clean input.");
            }

            log_info("Input is already clean; preserving original audio content.");
            convert_original_audio_to_wav(source_audio_path, output_path, ffmpeg_path);

            std::cout << std::string(70, '=') << "\n";
            std::cout << "PROCESSING COMPLETE\n";
            std::cout << std::string(70, '=') << "\n\n";
            std::cout << "Summary:\n";
            std::cout << "  ✓ Transcription: Complete\n";
            std::cout << "  ✓ Detoxification: No toxic content detected\n";
            std::cout << "  ✓ Audio Preservation: Original clean audio copied to WAV\n";
            std::cout << "  ✓ Output: " << output_path << "\n\n";
            return 0;
        }

        std::cout << std::string(70, '=') << "\n";
        std::cout << "STAGE 3: SILENCE REDACTION\n";
        std::cout << std::string(70, '=') << "\n\n";

        const std::string source_audio_path = resolve_audio_path_for_analysis(audio_path);
        if (source_audio_path.empty()) {
            throw std::runtime_error("Could not find source audio for silence redaction.");
        }

        const auto mute_ranges = build_mute_ranges(transcription, detoxified.matches);
        std::cout << "Muting " << mute_ranges.size() << " audio range(s):\n";
        for (const auto& range : mute_ranges) {
            std::cout << "  " << std::fixed << std::setprecision(3)
                      << range.start_seconds << "s - " << range.end_seconds << "s\n";
        }

        log_info("Replacing detected toxic speech with silence...");
        mute_audio_ranges_to_wav(source_audio_path, output_path, ffmpeg_path, mute_ranges);

        std::cout << std::string(70, '=') << "\n";
        std::cout << "PROCESSING COMPLETE\n";
        std::cout << std::string(70, '=') << "\n\n";

        std::cout << "Summary:\n";
        std::cout << "  ✓ Transcription: Complete\n";
        std::cout << "  ✓ Silence redaction: " << detoxified.censored_words
                 << " issue(s) muted\n";
        std::cout << "  ✓ Original speech outside muted ranges: Preserved\n";
        std::cout << "  ✓ Output: " << output_path << "\n\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] " << e.what() << std::endl;
        return 1;
    }
}
