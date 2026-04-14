#include <exception>
#include <iostream>
#include <string>

#include "common/logger.h"
#include "stt/whisper_engine.h"

int main(int argc, char** argv) {
    try {
        log_info("voice_detox_app started.");

        const std::string model_path =
            (argc > 1) ? argv[1] : "models/ggml-medium.bin";

        const std::string audio_path =
            (argc > 2) ? argv[2] : "";

        const std::string ffmpeg_path =
            (argc > 3) ? argv[3] : "ffmpeg";

        WhisperEngine whisper(model_path);
        WhisperResult result = whisper.transcribe_audio_file(audio_path, ffmpeg_path);

        std::cout << "\n=== Full Text ===\n";
        std::cout << result.full_text << "\n";

        std::cout << "\n=== Segments ===\n";
        for (const auto& seg : result.segments) {
            std::cout
                << "[" << seg.t0_ms << " ms -> " << seg.t1_ms << " ms] "
                << seg.text << "\n";
        }

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }
}