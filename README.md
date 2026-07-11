# Voice Detoxification System

A complete voice processing pipeline that transcribes audio and removes toxic/offensive content.

## Features

- **Speech-to-Text**: Uses Whisper for accurate multi-language transcription
- **Toxicity Detection**: Multi-layered approach to detect toxic language:
  - Pattern-based toxic word detection
  - Context-aware categorization (profanity, hate speech, threats, slurs)
  - Configurable toxicity levels
- **Text Detoxification**: Multiple modes:
  - Replace with alternatives (contextual substitutions)
  - Censor with asterisks
  - Category-based filtering
- **Segment Analysis**: Per-segment toxicity analysis
- **Detailed Reporting**: Comprehensive detoxification reports

## Architecture

```
Audio Input
    ↓
[ffmpeg] → Normalize to 16kHz mono WAV
    ↓
[Whisper] → Speech-to-Text Transcription
    ↓
[Toxicity Detector] → Analyze for toxic content
    ↓
[Text Detoxifier] → Replace/Censor/Filter
    ↓
Clean Text Output + Detailed Report
```

## Building

### Simplest Windows Run

From PowerShell in the project root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_simple.ps1
```

This builds the app and runs the sample audio at `tests\inputs\sample.m4a`.
The output audio is written to `simple_output.wav`.

Useful options:

```powershell
# Only transcribe and detoxify text, skip TTS
powershell -ExecutionPolicy Bypass -File scripts\run_simple.ps1 -SkipTts

# Censor toxic words with asterisks
powershell -ExecutionPolicy Bypass -File scripts\run_simple.ps1 -CensorOnly -SkipTts

# Use your own audio
powershell -ExecutionPolicy Bypass -File scripts\run_simple.ps1 -Audio path\to\audio.mp3 -Output clean.wav
```

### Requirements

- C++17 compiler (g++, clang, MSVC)
- CMake 3.20+
- ffmpeg (for audio processing)
- whisper.cpp (included in third_party)

### Build Instructions

```bash
mkdir build
cd build
cmake .. -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build . --config Release
```

The executable will be at: `build/bin/voice_detox_app.exe` (Windows) or `build/bin/voice_detox_app` (Linux/Mac)

## Usage

### Basic Usage

```bash
./voice_detox_app models/ggml-medium.bin audio.mp3
```

### Command Line Arguments

```
voice_detox_app [whisper_model] [audio_file] [ffmpeg_path] [--censor-only]

Arguments:
  whisper_model: Path to Whisper GGML model (default: models/ggml-medium.bin)
  audio_file: Path to audio file or directory (auto-detected if empty)
  ffmpeg_path: Path to ffmpeg executable (default: ffmpeg)
  --censor-only: Censor toxic words with * instead of replacing them
```

### Examples

```bash
# Basic transcription with detoxification
./voice_detox_app models/ggml-medium.bin podcast.mp3

# Censor mode
./voice_detox_app models/ggml-medium.bin input.wav . ffmpeg --censor-only

# Auto-detect audio file in tests/inputs
./voice_detox_app models/ggml-medium.bin

# Show help
./voice_detox_app --help
```

## Supported Audio Formats

- MP3
- WAV
- M4A
- FLAC
- OGG
- AAC
- WMA

(ffmpeg will automatically convert to the required 16kHz mono PCM WAV)

## Toxicity Categories

The system detects and categorizes:

1. **Profanity** - General curse words and vulgarities
2. **Insults** - Derogatory and demeaning language
3. **Slurs** - Ethnic, gender, or identity-based slurs
4. **Hate Speech** - Discriminatory language
5. **Threats** - Language threatening violence
6. **Self-Harm** - Content promoting self-injury
7. **Ableist** - Derogatory language about disabilities

## Toxicity Levels

- **CLEAN**: No toxic content
- **LOW**: Minor profanity or mild insults
- **MEDIUM**: Moderate toxicity or multiple low-severity issues
- **HIGH**: Severe content including slurs, threats, or hate speech

## Output

The application generates a detailed report including:

- Original transcription
- Detected toxic content with categories and severity
- Detoxified text
- Per-segment analysis
- Replacement and censoring statistics

Example output:
```
============================================================
TRANSCRIPTION RESULTS
============================================================

Original Transcription:
This is damn good work, you idiot...

============================================================
TOXICITY ANALYSIS
============================================================

Detoxification Report:
  Original toxicity: HIGH
  Toxic words found: 2
  Words replaced: 1
  Words censored: 1
  Detected issues:
    - [profanity] "damn" (LOW)
    - [insult] "idiot" (HIGH)

Detoxified Text:
This is darn good work, you ****...
```

## Python Enhancement Module (Optional)

For more advanced ML-based detoxification, see `scripts/advanced_detoxify.py`:

```bash
python scripts/advanced_detoxify.py --input original_text.txt --model-type transformers
```

Supports:
- HuggingFace toxicity models
- Perspective API integration
- Custom model fine-tuning

## Detoxification Accuracy Evaluation

Use `scripts/evaluate_detox_accuracy.py` to score detoxification quality with an
independent ML toxicity model. The default evaluator is Detoxify `unbiased`
(`roberta-base` trained on the Jigsaw unintended-bias toxicity task).

Single pair:
```bash
python scripts/evaluate_detox_accuracy.py \
  --original original_transcript.txt \
  --detoxified detoxified_transcript.txt
```

Batch CSV/JSONL/JSON:
```bash
python scripts/evaluate_detox_accuracy.py \
  --pairs detox_results.csv \
  --original-column original \
  --detoxified-column detoxified \
  --output detox_eval.json
```

Key metrics:
- **Detox success rate**: originally toxic text that falls below the toxicity threshold after detoxification
- **Residual toxic rate**: originally toxic text that remains above the threshold
- **Average toxicity reduction**: original toxicity score minus detoxified toxicity score
- **Regression rate**: cases where detoxification increased the toxicity score

## Implementation Details

### Toxicity Detection Algorithm

1. **Normalization**: Convert text to lowercase, remove special characters
2. **Word Indexing**: Create index of toxic words for O(1) lookup
3. **Pattern Matching**: Find all occurrences of toxic words
4. **Context Analysis**: Evaluate severity based on:
   - Word category (profanity, slur, threat, etc.)
   - Number of matches
   - Cumulative toxicity score
5. **Result Aggregation**: Compute overall toxicity level

### Text Detoxification Strategy

1. **Alternative Suggestions**: Each toxic word has contextually appropriate alternatives
2. **Replacement Logic**: Replace with best alternative when available
3. **Censoring Fallback**: Use asterisks when no good alternative exists
4. **Category Filtering**: Respect user preferences for which categories to remove

## Performance

- Whisper transcription: ~30-60 seconds per minute of audio (varies by model size)
- Toxicity detection: <1ms per 1000 characters
- Total processing: Mostly bound by audio transcription time

## Future Enhancements

- [ ] Multi-language toxicity detection
- [ ] Context-aware replacement using language models
- [ ] Real-time audio stream processing
- [ ] Custom toxicity word list loading
- [ ] Integration with moderation APIs
- [ ] Audio reconstruction from cleaned text
- [ ] Web API interface
- [ ] Browser extension

## Research & References

This implementation is based on research in toxic language detection and content moderation:

- Whisper: Large-V3 multilingual speech recognition model
- Toxicity patterns: Based on online content moderation best practices
- Alternative suggestions: Community standards and accessibility guidelines

## License

This project uses:
- Whisper.cpp (MIT License)
- GGML (MIT License)

See individual components for their respective licenses.

## Contact & Contributing

For issues, questions, or contributions, please see the project repository.
