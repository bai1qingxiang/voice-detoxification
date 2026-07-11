#include <exception>
#include <iostream>
#include <iomanip>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <unordered_set>

#include "common/logger.h"
#include "stt/whisper_engine.h"
#include "nlp/text_detoxifier.h"
#include "tts/piper_engine.h"
#include "audio/voice_analyzer.h"
#include "audio/audio_decoder.h"
#include "audio/wav_writer.h"

namespace fs = std::filesystem;

namespace {

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

std::string quote_file_arg(const std::string& s) {
    return "\"" + s + "\"";
}

std::string quote_executable_if_needed(const std::string& s) {
    if (s.find(' ') != std::string::npos || s.find('\t') != std::string::npos) {
        return "\"" + s + "\"";
    }
    return s;
}

std::string normalize_path_for_cmd(std::string s) {
    for (char& c : s) {
        if (c == '\\') {
            c = '/';
        }
    }
    return s;
}

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

std::string strip_whisper_annotations(const std::string& text) {
    std::string result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '*') {
            const size_t end = text.find('*', i + 1);
            if (end != std::string::npos) {
                const std::string segment = text.substr(i + 1, end - i - 1);
                const bool looks_like_annotation =
                    !segment.empty() &&
                    segment.size() <= 40 &&
                    std::any_of(segment.begin(), segment.end(), [](unsigned char c) {
                        return std::isalpha(c) != 0;
                    });

                if (looks_like_annotation) {
                    i = end;
                    continue;
                }
            }
        }

        result.push_back(text[i]);
    }

    return result;
}

std::vector<std::string> extract_lower_words(const std::string& text) {
    std::vector<std::string> words;
    std::string current;

    for (unsigned char c : text) {
        if (std::isalpha(c)) {
            current.push_back(static_cast<char>(std::tolower(c)));
        } else if (!current.empty()) {
            words.push_back(current);
            current.clear();
        }
    }

    if (!current.empty()) {
        words.push_back(current);
    }

    return words;
}

bool should_skip_tts_for_detoxified_text(
    const nlp::TextDetoxifier::DetoxifiedText& detoxified) {

    const int toxic_issue_count = detoxified.replacements_made + detoxified.censored_words;
    if (toxic_issue_count == 0) {
        return false;
    }

    const auto original_words = extract_lower_words(strip_whisper_annotations(detoxified.original));
    if (original_words.empty()) {
        return true;
    }

    const float toxic_ratio =
        static_cast<float>(toxic_issue_count) / static_cast<float>(original_words.size());

    const auto cleaned_words = extract_lower_words(strip_whisper_annotations(detoxified.detoxified));
    static const std::unordered_set<std::string> filler_words = {
        "ah", "eh", "er", "hmm", "hm", "huh", "oh", "uh", "um",
        "you", "your", "yours", "yeah", "yep", "yes", "no",
        "sigh", "clock", "ticking"
    };

    int meaningful_words = 0;
    for (const auto& word : cleaned_words) {
        if (filler_words.find(word) == filler_words.end()) {
            meaningful_words++;
        }
    }

    return meaningful_words == 0 || toxic_ratio >= 0.35f;
}

} // namespace

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options] [whisper_model] [audio_file]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --help              Show this help message\n";
    std::cout << "  --censor-only       Censor toxic words with * instead of replacing\n";
    std::cout << "  --output FILE       Save output audio to FILE (default: output.wav)\n";
    std::cout << "  --tts-model FILE    Piper ONNX voice model (default: models/piper-en_US-libritts-high.onnx)\n";
    std::cout << "  --skip-tts          Skip text-to-speech (only detoxify text)\n";
    std::cout << "  --skip-voice-restore Skip voice characteristic restoration\n";
    std::cout << "  --ffmpeg PATH       Path to ffmpeg executable (default: ffmpeg)\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  whisper_model       Path to Whisper GGML model (default: models/ggml-medium.bin)\n";
    std::cout << "  audio_file          Path to audio file or directory (auto-detected if empty)\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << program_name << " models/ggml-small.bin audio.mp3\n";
    std::cout << "  " << program_name << " --censor-only models/ggml-small.bin audio.mp3\n";
    std::cout << "  " << program_name << " --output clean.wav models/ggml-small.bin audio.mp3\n";
}

int main(int argc, char** argv) {
    try {
        log_info("=== Voice Detoxification Complete Pipeline ===");

        // Parse arguments
        std::string model_path = "models/ggml-medium.bin";
        std::string audio_path = "";
        std::string ffmpeg_path = "ffmpeg";
        std::string output_path = "output.wav";
        std::string tts_model = "models/piper-en_US-libritts-high.onnx";
        bool censor_only = false;
        bool skip_tts = false;
        bool skip_voice_restore = false;

        // Parse command line
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];

            if (arg == "--help") {
                print_usage(argv[0]);
                return 0;
            } else if (arg == "--censor-only") {
                censor_only = true;
            } else if (arg == "--skip-tts") {
                skip_tts = true;
            } else if (arg == "--skip-voice-restore") {
                skip_voice_restore = true;
            } else if (arg == "--output" && i + 1 < argc) {
                output_path = argv[++i];
            } else if (arg == "--tts-model" && i + 1 < argc) {
                tts_model = argv[++i];
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
        nlp::DetoxificationOptions detox_options;
        detox_options.censor_only = censor_only;
        nlp::TextDetoxifier detoxifier(detox_options);

        // Process transcription for toxicity
        auto detoxified = detoxifier.detoxify(transcription.full_text);

        std::cout << detoxified.report << "\n";

        if (detoxified.original_toxicity != nlp::ToxicityLevel::CLEAN) {
            std::cout << "Detoxified text:\n" << detoxified.detoxified << "\n\n";
        } else {
            std::cout << "✓ No toxic content detected - text is clean.\n\n";
        }

        if (skip_tts) {
            log_info("TTS skipped (--skip-tts)");
            std::cout << "\nProcessing complete. TTS was skipped, so no clean audio file was generated.\n";
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

        if (should_skip_tts_for_detoxified_text(detoxified)) {
            std::cout << "\nProcessing complete. The remaining detoxified text is not suitable for speech synthesis,\n";
            std::cout << "so no clean audio file was generated.\n";
            return 0;
        }

        std::cout << std::string(70, '=') << "\n";
        std::cout << "STAGE 3: TEXT-TO-SPEECH SYNTHESIS\n";
        std::cout << std::string(70, '=') << "\n\n";

        log_info("Initializing Piper TTS...");
        bool tts_completed = false;
        bool voice_restore_completed = false;
        try {
            tts::PiperEngine tts_engine(tts_model);

            if (!tts_engine.is_loaded()) {
                throw std::runtime_error("Failed to load TTS model");
            }

            log_info("Synthesizing clean speech...");
            auto tts_result = tts_engine.synthesize(detoxified.detoxified);

            std::cout << "✓ Speech synthesis complete\n";
            std::cout << "  Duration: " << std::fixed << std::setprecision(2)
                     << tts_result.duration_seconds << " seconds\n";
            std::cout << "  Sample rate: " << tts_result.sample_rate << " Hz\n";
            std::cout << "  Samples: " << tts_result.audio_samples.size() << "\n\n";

            if (!skip_voice_restore) {
                std::cout << std::string(70, '=') << "\n";
                std::cout << "STAGE 4: VOICE CHARACTERISTIC RESTORATION\n";
                std::cout << std::string(70, '=') << "\n\n";

                log_info("Extracting original voice characteristics...");

                audio::VoiceCharacteristics original_voice;
                const std::string source_audio_path = resolve_audio_path_for_analysis(audio_path);
                if (source_audio_path.empty()) {
                    throw std::runtime_error("Could not find source audio for voice restoration.");
                }

                const auto decoded_source = audio::decode_to_mono16k_wav(source_audio_path, ffmpeg_path);
                try {
                    original_voice = audio::VoiceAnalyzer::analyze_file(decoded_source.normalized_wav_path);
                    audio::remove_file_if_exists(decoded_source.normalized_wav_path);
                } catch (...) {
                    audio::remove_file_if_exists(decoded_source.normalized_wav_path);
                    throw;
                }

                std::cout << "Original voice characteristics:\n";
                if (original_voice.mean_pitch > 0.0f) {
                    std::cout << "  Mean pitch: " << original_voice.mean_pitch << " Hz\n";
                } else {
                    std::cout << "  Mean pitch: unavailable\n";
                }
                std::cout << "  Energy: " << original_voice.energy_mean << "\n";
                std::cout << "  Pitch variance: " << original_voice.pitch_variance << "\n\n";

                log_info("Applying voice characteristics...");
                auto voice_adjusted = audio::VoiceConverter::transfer_voice_characteristics(
                    tts_result.audio_samples,
                    original_voice,
                    tts_result.sample_rate);

                std::cout << "✓ Voice characteristic restoration complete\n\n";

                // Write to file
                audio::WAVWriter::write_wav(output_path, voice_adjusted,
                                           tts_result.sample_rate, 1);
                voice_restore_completed = true;
            } else {
                // Write to file without voice restoration
                audio::WAVWriter::write_wav(output_path, tts_result.audio_samples,
                                           tts_result.sample_rate, 1);
            }
            tts_completed = true;
        } catch (const std::exception& e) {
            throw std::runtime_error(
                "Clean audio generation failed. No output audio was written: " + std::string(e.what()));
        }

        std::cout << std::string(70, '=') << "\n";
        std::cout << "PROCESSING COMPLETE\n";
        std::cout << std::string(70, '=') << "\n\n";

        std::cout << "Summary:\n";
        std::cout << "  ✓ Transcription: Complete\n";
        std::cout << "  ✓ Detoxification: " << (detoxified.replacements_made + detoxified.censored_words)
                 << " issue(s) fixed\n";
        std::cout << "  ✓ TTS Synthesis: " << (tts_completed ? "Complete" : "Skipped") << "\n";
        if (!skip_voice_restore && voice_restore_completed) {
            std::cout << "  ✓ Voice Restoration: Complete\n";
        }
        std::cout << "  ✓ Output: " << output_path << "\n\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n[ERROR] " << e.what() << std::endl;
        return 1;
    }
}
