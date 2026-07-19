# Voice Detoxification System

A complete voice processing pipeline that transcribes audio and removes toxic/offensive content.

## Features

- **Speech-to-Text**: Uses Whisper for English transcription with word timestamps
- **Toxicity Detection**: Multi-layered approach to detect toxic language:
  - Pattern-based toxic word detection
  - Context-aware categorization (profanity, hate speech, threats, slurs)
  - Configurable toxicity levels
- **Audio Detoxification**: Replaces detected toxic speech ranges with silence
- **Original Audio Preservation**: Keeps the original speaker and all audio outside muted ranges
- **Segment Analysis**: Per-segment toxicity analysis
- **Detailed Reporting**: Comprehensive detoxification reports

## Windows Desktop Application

The native desktop application records directly from the system microphone and
runs the existing detoxification pipeline in the background. It does not use a
browser or require the removed `web` directory.

From the project root, double-click `desktop.bat`, or run:

```powershell
.\desktop.bat
```

In the application:

1. Confirm **敏感词静音（保留原音）**.
2. Click **录音** and begin speaking.
3. Click **停止** when finished.
4. Wait for speech recognition and silence redaction.
5. Play, open, or save the generated WAV file from the result panel.

Generated audio is stored under `output\`. The application requires microphone
permission, FFmpeg on `PATH`, and the models already present under `models\`.

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
[Word timestamps] → Map toxic text to exact audio ranges
    ↓
[ffmpeg] → Replace only those ranges with silence
    ↓
Original Audio + Muted Toxic Ranges + Detailed Report
```

## Building

### Simplest Windows Run

From PowerShell in the project root:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\run_simple.ps1
```

This builds the app and runs the sample audio at `tests\inputs\sample.m4a`.
The output audio is written to `simple_output.wav`.
The output keeps the original recording duration and voice. It does not
rewrite or synthesize the transcript.

Shortcut command:

```powershell
.\detox.bat tests\inputs\sample.m4a clean_output.wav
```

You do not need to copy every input audio file into a project folder. You can:

- Double-click `detox.bat` or run `.\detox.bat` to choose an audio file in a file picker.
- Drag an audio file onto `detox.bat`.
- Pass any audio path from the command line.

If you do not provide an output path, the clean audio is written next to the
input file with `_clean.wav` added to the name.
Detected toxic ranges become silence. Non-toxic speech remains unchanged.

If you want to type `detox ...` without `.\detox.bat`, add this project folder
to your Windows `PATH`, or create a desktop shortcut that runs `detox.bat`.

Useful options:

```powershell
# Only transcribe and report, skip audio output
powershell -ExecutionPolicy Bypass -File scripts\run_simple.ps1 -SkipAudio

# Use your own audio
powershell -ExecutionPolicy Bypass -File scripts\run_simple.ps1 -Audio path\to\audio.mp3

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
voice_detox_app [options] [whisper_model] [audio_file]

Arguments:
  whisper_model: Path to Whisper GGML model (default: models/ggml-medium.bin)
  audio_file: Path to audio file or directory (auto-detected if empty)
  --output FILE: Output WAV path
  --skip-audio: Only transcribe and report detected words
  --ffmpeg PATH: Path to ffmpeg executable (default: ffmpeg)
```

### Examples

```bash
# Basic transcription with detoxification
./voice_detox_app models/ggml-medium.bin podcast.mp3

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
- Detection and silence-redaction statistics

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

### Audio Detoxification Strategy

1. Detect toxic English words and phrases in the transcript.
2. Map their text offsets to Whisper token timestamps.
3. Add a short boundary pad and merge overlapping ranges.
4. Use FFmpeg to set only those ranges to zero volume.
5. Preserve the original duration, voice, timing, and non-toxic speech.

## Performance

- Whisper transcription: ~30-60 seconds per minute of audio (varies by model size)
- Toxicity detection: <1ms per 1000 characters
- Total processing: Mostly bound by audio transcription time

## Future Enhancements

- [ ] Multi-language toxicity detection
- [ ] Real-time audio stream processing
- [ ] Custom toxicity word list loading
- [ ] Integration with moderation APIs
- [ ] Web API interface
- [ ] Browser extension

## Research & References

This implementation is based on research in toxic language detection and content moderation:

- Whisper: Large-V3 multilingual speech recognition model
- Toxicity patterns: Based on online content moderation best practices
- Silence redaction: timestamp-based local audio filtering

## License

This project uses:
- Whisper.cpp (MIT License)
- GGML (MIT License)

See individual components for their respective licenses.

## Contact & Contributing

For issues, questions, or contributions, please see the project repository.
