# Getting Started with Voice Detoxification

## Quick Start (5 minutes)

### 1. Build the Project

```bash
cd voice-detoxification
mkdir build
cd build
cmake .. -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build . --config Release
cd ..
```

### 2. Download a Whisper Model

Choose a model based on your needs:

- **Tiny** (39M) - Fastest, lower accuracy
  ```bash
  mkdir -p models
  wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin -O models/ggml-tiny.en.bin
  ```

- **Small** (140M) - Good balance
  ```bash
  wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin -O models/ggml-small.bin
  ```

- **Medium** (769M) - Better accuracy
  ```bash
  wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin -O models/ggml-medium.bin
  ```

### 3. Process Your First Audio

```bash
# Create sample directory
mkdir -p tests/inputs
cp your_audio.mp3 tests/inputs/

# Run the detoxification pipeline
./build/bin/voice_detox_app models/ggml-small.bin
```

## Advanced Usage

### Python-Based ML Detection (Optional)

For more sophisticated detection using transformer models:

```bash
# Install Python dependencies
pip install -r scripts/requirements.txt

# Run advanced analysis
python scripts/advanced_detoxify.py --input "your text here" --model michellejieli/NSFW_text_classifier
```

### Censor-Only Mode

Censor instead of replacing:

```bash
./build/bin/voice_detox_app models/ggml-medium.bin audio.mp3 . ffmpeg --censor-only
```

## Project Structure

```
voice-detoxification/
├── src/
│   ├── audio/           # Audio processing (ffmpeg integration)
│   ├── stt/             # Speech-to-text (Whisper integration)
│   ├── nlp/             # NLP processing
│   │   ├── toxicity_detector.h/.cpp      # Main toxicity detection
│   │   └── text_detoxifier.h/.cpp        # Text detoxification
│   └── common/          # Utilities and logging
├── app/
│   └── cli/
│       └── main.cpp     # Main application entry point
├── scripts/
│   ├── advanced_detoxify.py    # Optional ML-based enhancement
│   └── requirements.txt         # Python dependencies
├── third_party/
│   ├── whisper.cpp/            # Whisper STT engine
│   └── piper/                  # Optional TTS engine
├── build/               # Build output (after cmake build)
│   └── bin/
│       └── voice_detox_app     # Main executable
├── CMakeLists.txt       # Build configuration
└── README.md            # Full documentation
```

## Architecture Details

### Toxicity Detection Pipeline

```
Input Text
    ↓
[Normalize] → lowercase, remove special chars
    ↓
[Index Lookup] → O(1) toxic word identification
    ↓
[Pattern Match] → find all occurrences
    ↓
[Categorize] → profanity, slur, threat, etc.
    ↓
[Aggregate] → compute overall toxicity level
    ↓
Toxicity Report
```

### Text Detoxification Options

1. **Replace Mode** (default)
   - "damn" → "darn"
   - "idiot" → "foolish person"
   - Preserves sentence structure

2. **Censor Mode** (--censor-only)
   - "damn" → "****"
   - Indicates removed content

### Toxicity Levels

```
CLEAN       No toxic content
LOW         Minor profanity (e.g., "damn", "hell")
MEDIUM      Moderate issues (e.g., multiple low-severity, some insults)
HIGH        Severe content (slurs, threats, hate speech)
```

## Configuration

### Modify Toxicity Word List

Edit `src/nlp/toxicity_detector.cpp`, function `initialize_toxic_wordlist()`:

```cpp
void ToxicityDetector::initialize_toxic_wordlist() {
    toxic_words_ = {
        {"word", ToxicityLevel::HIGH, "category"},
        // Add more entries...
    };
}
```

### Configure Detoxification

In `src/nlp/text_detoxifier.h`, modify `DetoxificationOptions`:

```cpp
struct DetoxificationOptions {
    bool censor_only = false;           // Use asterisks
    bool remove_profanity = true;       // Filter curse words
    bool remove_hate_speech = true;     // Filter discriminatory content
    bool remove_threats = true;         // Filter violent language
    bool remove_self_harm = true;       // Filter self-harm content
};
```

## Performance Optimization

### Speed Up Transcription

Use smaller Whisper models:
```bash
./build/bin/voice_detox_app models/ggml-tiny.en.bin audio.mp3
```

### Batch Processing

Process multiple files in a loop:
```bash
for file in tests/inputs/*.mp3; do
    ./build/bin/voice_detox_app models/ggml-medium.bin "$file"
done
```

### Parallel Processing

For multiple files:
```bash
ls tests/inputs/*.mp3 | parallel "./build/bin/voice_detox_app models/ggml-small.bin {}"
```

## Troubleshooting

### Build Issues

**Problem**: `whisper.cpp not found`
**Solution**: Ensure whisper.cpp is in `third_party/whisper.cpp`

```bash
git submodule update --init --recursive
```

**Problem**: Missing compiler
**Solution**: Install MinGW (Windows) or GCC (Linux)

### Runtime Issues

**Problem**: `ffmpeg not found`
**Solution**: Install ffmpeg and add to PATH

```bash
# Windows (choco)
choco install ffmpeg

# Linux (apt)
sudo apt-get install ffmpeg

# macOS (brew)
brew install ffmpeg
```

**Problem**: `Model file not found`
**Solution**: Download model to `models/` directory

## Integration with Other Systems

### As a Library

Link against `libvoice_detox_core.a`:

```cpp
#include "nlp/text_detoxifier.h"

nlp::TextDetoxifier detoxifier;
auto result = detoxifier.detoxify("your text here");
```

### REST API (Future)

Plan to add web service wrapper:

```bash
POST /detoxify
{
    "text": "input text",
    "mode": "replace"  // or "censor"
}
```

## Contributing

Areas for enhancement:

- [ ] Multi-language support
- [ ] Custom word list loading
- [ ] ML model integration (in-process)
- [ ] Real-time streaming
- [ ] Audio reconstruction
- [ ] Web UI
- [ ] API wrapper
- [ ] Performance optimization

## Research & References

### Papers

- Whisper: Robust Speech Recognition via Large-Scale Weak Supervision
  https://arxiv.org/abs/2212.04356

- Detecting Online Hate Speech Using Context-Aware Models
  https://arxiv.org/abs/1910.03099

### Models

- Whisper: https://github.com/openai/whisper
- Perspective API: https://www.perspectiveapi.com/
- Detoxify: https://github.com/unitaryai/detoxify

## License

- Project: MIT License
- Whisper.cpp: MIT License
- GGML: MIT License

See individual components for details.

## Support

For issues, questions, or contributions:
- Check documentation in README.md
- Review code comments in src/
- Test with test.sh script
