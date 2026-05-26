# Voice Detoxification Project - AI Assistant Guide

## Project Overview

Voice Detoxification is a complete pipeline for:
1. Transcribing audio files using Whisper (AI speech-to-text)
2. Detecting toxic/offensive content in transcribed text
3. Cleaning the text by replacing or censoring problematic words
4. Providing detailed analysis reports

## Architecture

### Core Components

- **Audio Decoder** (`src/audio/`) - Converts various audio formats to 16kHz mono WAV using ffmpeg
- **Whisper Engine** (`src/stt/`) - Speech-to-text transcription using whisper.cpp
- **Toxicity Detector** (`src/nlp/toxicity_detector.*`) - Multi-layered toxic content detection
- **Text Detoxifier** (`src/nlp/text_detoxifier.*`) - Text cleaning and replacement logic

### Key Files

- `CMakeLists.txt` - Build configuration
- `app/cli/main.cpp` - Main CLI entry point
- `src/nlp/toxicity_detector.cpp` - Contains the toxicity word list (expandable)
- `scripts/advanced_detoxify.py` - Optional ML-based enhancement

## Important Implementation Notes

### Toxicity Word List

Located in `src/nlp/toxicity_detector.cpp::initialize_toxic_wordlist()`. Each word has:
- Text content
- Severity level (LOW, MEDIUM, HIGH)
- Category (profanity, slur, threat, self_harm, ableist, hate_speech, etc.)

### Detoxification Modes

1. **Replace Mode** - Substitute toxic words with alternatives from the mapping
2. **Censor Mode** - Replace with asterisks (--censor-only flag)

### Performance

- Toxicity detection: <1ms per 1000 chars (heuristic-based, very fast)
- Whisper transcription: Bottleneck (~30-60s per minute of audio)
- Total: Dominated by transcription time

## Build & Development

### Building

```bash
mkdir build && cd build
cmake .. -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build . --config Release
```

### File Structure

- `src/nlp/` - Where to add new detoxification features
- `scripts/` - Python enhancements
- `build/bin/voice_detox_app` - Main executable

## Adding Features

### Add New Toxic Words

1. Edit `src/nlp/toxicity_detector.cpp`
2. Add entry to `toxic_words_` vector in `initialize_toxic_wordlist()`
3. Add alternatives to `alternatives` map in `src/nlp/text_detoxifier.cpp`

Example:
```cpp
{"newword", ToxicityLevel::HIGH, "category"},
```

### Add New Categories

1. Update `ToxicityMatch.category` assignments
2. Add category handling in `TextDetoxifier::detoxify()`

### Multi-language Support

Would require:
- Updating word lists for each language
- Language detection in main.cpp
- Separate `DetoxificationOptions` per language

## Testing

Run: `bash test.sh`

Manual testing:
```bash
./build/bin/voice_detox_app --help
./build/bin/voice_detox_app models/ggml-small.bin tests/inputs/sample.mp3
./build/bin/voice_detox_app models/ggml-small.bin tests/inputs/sample.mp3 . ffmpeg --censor-only
```

## Known Limitations

1. **Heuristic-based detection** - Uses word lists, not ML (can be enhanced with Python module)
2. **English-only** - Current word lists are English
3. **No context awareness** - Simple word matching doesn't understand context
4. **No audio reconstruction** - Detoxifies text only, doesn't regenerate audio
5. **Fixed thread count** - n_threads parameter in whisper_engine.cpp is hard-coded to 1

## Future Enhancement Areas

1. **Add ML models** - Use Python ML module or embed lightweight models
2. **Multi-language** - Expand toxicity detection to multiple languages
3. **TTS integration** - Use Piper (already in third_party) to re-synthesize audio
4. **Real-time processing** - Stream processing instead of batch
5. **Custom word lists** - Load from config files
6. **API wrapper** - REST/gRPC interface
7. **Performance** - GPU acceleration, model quantization

## Dependencies

### C++ Libraries
- whisper.cpp (in third_party/)
- ggml (in third_party/)
- Standard library

### External Tools
- ffmpeg (required for audio processing)
- CMake 3.20+
- C++17 compiler

### Optional
- Python 3.8+ (for advanced_detoxify.py)
- transformers, torch (for ML enhancement)

## Debugging

Enable additional logging:
- Modify `src/common/logger.h` to add more logging levels
- Uncomment DEBUG prints in main.cpp
- Add `std::cout` debugging in specific functions

## Code Style

- C++17 standard
- Namespace organization (nlp::, audio::, etc.)
- No external dependencies for core functionality
- Clear error messages with std::runtime_error

## Performance Profiling

To profile:
1. Add timing code around major sections
2. Use `std::chrono` for measurements
3. Compare with/without GPU in whisper_engine.cpp

## Related Projects

- whisper.cpp: https://github.com/ggerganov/whisper.cpp
- Perspective API: https://www.perspectiveapi.com/
- Detoxify: https://github.com/unitaryai/detoxify

## Quick Commands

```bash
# Build
cd build && cmake --build . --config Release

# Run basic test
./build/bin/voice_detox_app --help

# Run with audio
./build/bin/voice_detox_app models/ggml-small.bin tests/inputs/audio.mp3

# Censor mode
./build/bin/voice_detox_app models/ggml-small.bin tests/inputs/audio.mp3 . ffmpeg --censor-only
```

## Notes for Future Sessions

- This project compiles successfully with MinGW on Windows
- The build system uses CMake and supports cross-platform builds
- The executable is relatively small (~2.4MB without models)
- Models need to be downloaded separately and are large (39MB - 2GB+)
- ffmpeg is required and must be in PATH or specified as argument
