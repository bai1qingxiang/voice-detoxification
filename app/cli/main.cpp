#include <exception>
#include <iostream>
#include <iomanip>
#include <string>

#include "common/logger.h"
#include "stt/whisper_engine.h"
#include "nlp/text_detoxifier.h"

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [whisper_model] [audio_file] [ffmpeg_path] [--censor-only]\n";
    std::cout << "  whisper_model: Path to Whisper GGML model (default: models/ggml-medium.bin)\n";
    std::cout << "  audio_file: Path to audio file or directory (auto-detected if empty)\n";
    std::cout << "  ffmpeg_path: Path to ffmpeg executable (default: ffmpeg)\n";
    std::cout << "  --censor-only: Censor toxic words with * instead of replacing\n";
}

int main(int argc, char** argv) {
    try {
        log_info("voice_detox_app started.");

        // Parse arguments
        std::string model_path = "models/ggml-medium.bin";
        std::string audio_path = "";
        std::string ffmpeg_path = "ffmpeg";
        bool censor_only = false;

        if (argc > 1 && std::string(argv[1]) == "--help") {
            print_usage(argv[0]);
            return 0;
        }

        if (argc > 1) model_path = argv[1];
        if (argc > 2) audio_path = argv[2];
        if (argc > 3) ffmpeg_path = argv[3];
        if (argc > 4 && std::string(argv[4]) == "--censor-only") {
            censor_only = true;
        }

        // Initialize transcriber
        log_info("Initializing Whisper transcriber...");
        WhisperEngine whisper(model_path);

        if (!whisper.is_loaded()) {
            throw std::runtime_error("Failed to load Whisper model");
        }

        // Transcribe audio
        log_info("Transcribing audio...");
        WhisperResult transcription = whisper.transcribe_audio_file(audio_path, ffmpeg_path);

        // Initialize detoxifier
        log_info("Analyzing content for toxic language...");
        nlp::DetoxificationOptions detox_options;
        detox_options.censor_only = censor_only;
        nlp::TextDetoxifier detoxifier(detox_options);

        // Process transcription for toxicity
        auto detoxified = detoxifier.detoxify(transcription.full_text);

        // Output results
        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "TRANSCRIPTION RESULTS\n";
        std::cout << std::string(60, '=') << "\n\n";

        std::cout << "Original Transcription:\n";
        std::cout << transcription.full_text << "\n\n";

        std::cout << std::string(60, '-') << "\n";
        std::cout << "TOXICITY ANALYSIS\n";
        std::cout << std::string(60, '-') << "\n\n";

        std::cout << detoxified.report;

        if (detoxified.original_toxicity != nlp::ToxicityLevel::CLEAN) {
            std::cout << "\nDetoxified Text:\n";
            std::cout << detoxified.detoxified << "\n";
        } else {
            std::cout << "\nNo toxic content detected - text is clean.\n";
        }

        std::cout << "\n" << std::string(60, '=') << "\n";
        std::cout << "SEGMENT ANALYSIS\n";
        std::cout << std::string(60, '=') << "\n\n";

        for (const auto& seg : transcription.segments) {
            auto seg_analysis = detoxifier.detoxify(seg.text);

            std::cout << "[" << std::setw(6) << seg.t0_ms << " ms -> "
                     << std::setw(6) << seg.t1_ms << " ms] ";

            if (seg_analysis.original_toxicity == nlp::ToxicityLevel::CLEAN) {
                std::cout << "✓ CLEAN";
            } else {
                std::cout << "⚠ " << (seg_analysis.replacements_made + seg_analysis.censored_words)
                         << " issue(s)";
            }

            std::cout << "\n  Original: " << seg.text << "\n";
            if (seg_analysis.original_toxicity != nlp::ToxicityLevel::CLEAN) {
                std::cout << "  Cleaned:  " << seg_analysis.detoxified << "\n";
            }
            std::cout << "\n";
        }

        std::cout << std::string(60, '=') << "\n";
        std::cout << "PROCESSING COMPLETE\n";
        std::cout << std::string(60, '=') << "\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }
}