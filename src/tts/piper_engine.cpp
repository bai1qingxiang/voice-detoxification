#include "tts/piper_engine.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <filesystem>
#include <cmath>

namespace fs = std::filesystem;

namespace tts {

namespace {

std::string create_temp_wav_path() {
    static int counter = 0;
    const fs::path temp_dir = fs::temp_directory_path();
    const fs::path out = temp_dir / ("piper_" + std::to_string(counter++) + ".wav");
    return out.string();
}

void write_wav_header(std::ofstream& file,
                      int sample_rate,
                      int num_samples,
                      int num_channels = 1) {
    int byte_rate = sample_rate * num_channels * 2;
    int block_align = num_channels * 2;
    int subchunk2_size = num_samples * num_channels * 2;
    int chunk_size = 36 + subchunk2_size;

    file.write("RIFF", 4);
    file.write(reinterpret_cast<const char*>(&chunk_size), 4);
    file.write("WAVE", 4);

    file.write("fmt ", 4);
    int subchunk1_size = 16;
    file.write(reinterpret_cast<const char*>(&subchunk1_size), 4);

    short audio_format = 1;  // PCM
    file.write(reinterpret_cast<const char*>(&audio_format), 2);
    file.write(reinterpret_cast<const char*>(&num_channels), 2);
    file.write(reinterpret_cast<const char*>(&sample_rate), 4);
    file.write(reinterpret_cast<const char*>(&byte_rate), 4);
    file.write(reinterpret_cast<const char*>(&block_align), 2);

    short bits_per_sample = 16;
    file.write(reinterpret_cast<const char*>(&bits_per_sample), 2);

    file.write("data", 4);
    file.write(reinterpret_cast<const char*>(&subchunk2_size), 4);
}

std::string quote_arg(const std::string& value) {
    std::string quoted = "\"";
    for (char c : value) {
        if (c == '"') {
            quoted += "\\\"";
        } else {
            quoted += c;
        }
    }
    quoted += "\"";
    return quoted;
}

std::vector<int16_t> read_wav_samples(const std::string& wav_path) {
    std::ifstream file(wav_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open WAV file: " + wav_path);
    }

    char riff[4];
    file.read(riff, 4);

    uint32_t chunk_size;
    file.read(reinterpret_cast<char*>(&chunk_size), 4);

    char wave[4];
    file.read(wave, 4);

    // Skip to data chunk
    bool found_data = false;
    while (!found_data && file) {
        char chunk_id[4];
        file.read(chunk_id, 4);
        if (!file) break;

        uint32_t subchunk_size;
        file.read(reinterpret_cast<char*>(&subchunk_size), 4);

        if (std::string(chunk_id, 4) == "data") {
            found_data = true;
            std::vector<int16_t> samples(subchunk_size / 2);
            file.read(reinterpret_cast<char*>(samples.data()), subchunk_size);
            return samples;
        } else {
            file.seekg(subchunk_size, std::ios::cur);
        }
    }

    throw std::runtime_error("No data chunk found in WAV file.");
}

} // namespace

PiperEngine::PiperEngine(const std::string& model_path)
    : model_path_(model_path) {
    if (!fs::exists(model_path)) {
        throw std::runtime_error("Piper model not found: " + model_path);
    }

    std::cout << "[INFO] Loading Piper TTS model: " << model_path << std::endl;
    loaded_ = true;
}

PiperEngine::~PiperEngine() = default;

bool PiperEngine::is_loaded() const {
    return loaded_;
}

TTSResult PiperEngine::synthesize(const std::string& text) {
    if (!is_loaded()) {
        throw std::runtime_error("Piper model is not loaded.");
    }

    if (text.empty()) {
        throw std::runtime_error("Cannot synthesize empty text.");
    }

    TTSResult result;

    const std::string output_wav = create_temp_wav_path();
    const std::string script_path = output_wav + ".py";
    const std::string text_path = output_wav + ".txt";

    {
        std::ofstream text_file(text_path, std::ios::binary);
        if (!text_file) throw std::runtime_error("Failed to create temporary TTS text file.");
        text_file.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // Create Python script content
    std::ostringstream script;
    script << R"(#!/usr/bin/env python3
import sys
import wave

try:
    try:
        from piper.voice import PiperVoice
    except ImportError:
        from piper import PiperVoice

    model_path = sys.argv[1]
    text_path = sys.argv[2]
    output_wav = sys.argv[3]

    with open(text_path, 'r', encoding='utf-8') as text_file:
        text = text_file.read()

    voice = PiperVoice.load(model_path)

    with wave.open(output_wav, 'wb') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)

        wrote_params = False
        for chunk in voice.synthesize(text):
            if not wrote_params:
                wav_file.setframerate(chunk.sample_rate)
                wrote_params = True

            wav_file.writeframes(chunk.audio_int16_bytes)

    print("SUCCESS")
except Exception as e:
    print(f"ERROR: {e}")
    sys.exit(1)
)";

    // Write script to temp file
    std::ofstream script_file(script_path);
    script_file << script.str();
    script_file.close();

    // Execute Python script
    std::ostringstream cmd;
    cmd << "python " << quote_arg(script_path) << " "
        << quote_arg(model_path_) << " "
        << quote_arg(text_path) << " "
        << quote_arg(output_wav) << " 2>&1";

    std::cout << "[DEBUG] Running: " << cmd.str() << std::endl;

    const int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        fs::remove(script_path);
        fs::remove(text_path);
        throw std::runtime_error("Piper synthesis command failed with code " + std::to_string(rc));
    }

    // Check if output WAV exists
    if (!fs::exists(output_wav)) {
        fs::remove(script_path);
        fs::remove(text_path);
        throw std::runtime_error("Piper synthesis did not create an output WAV file.");
    }

    // Read the generated WAV file
    try {
        result.audio_samples = read_wav_samples(output_wav);
        result.sample_rate = 22050;
        result.num_channels = 1;
        result.duration_seconds = static_cast<float>(result.audio_samples.size()) / result.sample_rate;

        // Clean up
        fs::remove(output_wav);
        fs::remove(script_path);
        fs::remove(text_path);
    } catch (const std::exception& e) {
        fs::remove(script_path);
        fs::remove(text_path);
        fs::remove(output_wav);
        std::cerr << "[ERROR] Failed to read WAV: " << e.what() << std::endl;
        throw;
    }

    return result;
}

std::vector<std::string> PiperEngine::get_available_voices() const {
    return {
        "en_US-libritts-high",
        "en_US-libritts-medium",
        "en_US-glow-tts"
    };
}

} // namespace tts
