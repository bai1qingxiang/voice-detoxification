# Voice Detoxification 完整集成指南

本项目现在按四个阶段完成一条端到端的语音净化流水线：

```text
原始音频
  -> 1. 语音转文字 STT
  -> 2. 文本去毒 Detoxification
  -> 3. 文字转语音 TTS
  -> 4. 音色还原 Voice Restoration
  -> 净化后的语音文件
```

核心目标是：保留原始语音的大致说话特征，同时把转写文本中的攻击性、辱骂性或其他有害表达替换或屏蔽，再重新合成为干净语音。

---

## 1. 语音转文字

### 作用

把输入的 MP3、WAV、M4A、FLAC、OGG、AAC、WMA 等音频转成文本，并保留 Whisper 分段时间戳。

### 实现位置

- `src/audio/audio_decoder.h/cpp`
- `src/stt/whisper_engine.h/cpp`
- `app/cli/main.cpp`

### 流程

```text
输入音频
  -> ffmpeg 转成 16 kHz、mono、PCM 16-bit WAV
  -> whisper.cpp 加载 GGML 模型
  -> Whisper 输出完整文本和分段文本
```

### 关键点

- ffmpeg 负责统一音频格式，避免 Whisper 直接处理各种容器格式。
- Whisper 模型路径通过第一个位置参数传入。
- 如果没有指定音频文件，程序会自动在 `tests/inputs/` 或 `tests/input/` 中查找第一个支持的音频。
- 线程数会根据机器 CPU 自动选择，避免固定单线程拖慢转写。

---

## 2. 去毒

### 作用

检测转写文本里的毒性词汇，并按配置执行替换或打码。

### 实现位置

- `src/nlp/toxicity_detector.h/cpp`
- `src/nlp/text_detoxifier.h/cpp`
- `scripts/advanced_detoxify.py`

### 流程

```text
Whisper 文本
  -> 标准化文本
  -> 匹配毒性词表
  -> 标注严重程度和类别
  -> 替换为温和表达，或用星号打码
  -> 输出净化文本和分析报告
```

### 支持的处理模式

- 默认替换模式：把有害词替换为更温和的表达。
- `--censor-only`：只用星号屏蔽，不做语义替换。

### 当前类别

- profanity：脏话
- insult：辱骂
- slur：歧视性用语
- hate_speech：仇恨言论
- threat：威胁
- self_harm：自伤相关
- ableist：残障歧视相关

### 扩展方式

新增词汇：

```cpp
{"newword", ToxicityLevel::HIGH, "category"},
```

新增替代表达：

```cpp
{"newword", {"alternative one", "alternative two"}},
```

---

## 3. 文字转语音

### 作用

把净化后的文本重新合成为语音。

### 实现位置

- `src/tts/piper_engine.h/cpp`
- `scripts/piper_tts.py`
- `app/cli/main.cpp`

### 流程

```text
净化文本
  -> Piper TTS 加载 ONNX 声音模型
  -> 合成 PCM 16-bit 音频采样
  -> 写入 WAV 文件
```

### 模型配置

默认模型：

```text
models/piper-en_US-libritts-high.onnx
```

也可以通过参数指定：

```bash
./build/bin/voice_detox_app.exe \
  --tts-model models/piper-en_US-libritts-medium.onnx \
  models/ggml-small.bin \
  tests/inputs/sample.mp3
```

### 依赖

```bash
pip install -r scripts/requirements.txt
pip install piper-tts
```

Piper 模型通常需要 `.onnx` 和配套 `.onnx.json` 同目录存在。

---

## 4. 音色还原

### 作用

从原始音频中提取基础说话特征，并应用到 TTS 合成结果上，让最终语音尽量贴近原始声音的音高和能量。

### 实现位置

- `src/audio/voice_analyzer.h/cpp`
- `src/audio/wav_writer.h/cpp`
- `app/cli/main.cpp`

### 流程

```text
原始音频
  -> ffmpeg 转 16 kHz mono WAV
  -> 提取平均基频、基频变化、能量、过零率、频谱质心
  -> 对 TTS 音频做能量调整和音高调整
  -> 输出最终 WAV
```

### 当前能力边界

当前实现是轻量级音色迁移，不是深度学习声纹克隆。它可以还原一些基本听感特征，例如音高和响度，但不会完整复制原说话人的音色、口音、情绪和发声细节。

如果未来要做更强的音色克隆，可以接入：

- speaker embedding 模型
- voice conversion 模型
- neural vocoder
- 带参考音频的多说话人 TTS

---

## 快速开始

### 1. 编译

```bash
mkdir build
cd build
cmake .. -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build . --config Release
cd ..
```

### 2. 准备模型

创建模型目录：

```bash
mkdir -p models
```

下载 Whisper 模型，任选一个：

```bash
# 快速，体积小
wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.en.bin \
  -O models/ggml-tiny.en.bin

# 推荐，质量和速度较平衡
wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small.bin \
  -O models/ggml-small.bin

# 质量更高，速度更慢
wget https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium.bin \
  -O models/ggml-medium.bin
```

下载 Piper 模型：

```bash
wget https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts/high/en_US-libritts-high.onnx \
  -O models/piper-en_US-libritts-high.onnx

wget https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts/high/en_US-libritts-high.onnx.json \
  -O models/piper-en_US-libritts-high.onnx.json
```

Windows PowerShell 可以用：

```powershell
New-Item -ItemType Directory -Force models

Invoke-WebRequest `
  -Uri "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts/high/en_US-libritts-high.onnx" `
  -OutFile "models/piper-en_US-libritts-high.onnx"

Invoke-WebRequest `
  -Uri "https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/libritts/high/en_US-libritts-high.onnx.json" `
  -OutFile "models/piper-en_US-libritts-high.onnx.json"
```

### 3. 安装运行依赖

```bash
pip install -r scripts/requirements.txt
pip install piper-tts
```

确保 ffmpeg 可用：

```bash
ffmpeg -version
```

### 4. 运行完整流水线

```bash
./build/bin/voice_detox_app.exe \
  models/ggml-small.bin \
  tests/inputs/sample.mp3
```

指定输出文件：

```bash
./build/bin/voice_detox_app.exe \
  --output clean_output.wav \
  models/ggml-small.bin \
  tests/inputs/sample.mp3
```

只打码不替换：

```bash
./build/bin/voice_detox_app.exe \
  --censor-only \
  models/ggml-small.bin \
  tests/inputs/sample.mp3
```

跳过 TTS，只做文本分析：

```bash
./build/bin/voice_detox_app.exe \
  --skip-tts \
  models/ggml-small.bin \
  tests/inputs/sample.mp3
```

跳过音色还原：

```bash
./build/bin/voice_detox_app.exe \
  --skip-voice-restore \
  models/ggml-small.bin \
  tests/inputs/sample.mp3
```

---

## CLI 参数

```text
Usage: voice_detox_app [options] [whisper_model] [audio_file]

Options:
  --help                 显示帮助
  --censor-only          用星号屏蔽毒性词，不做替换
  --output FILE          输出 WAV 文件，默认 output.wav
  --tts-model FILE       指定 Piper ONNX 模型
  --skip-tts             跳过文字转语音
  --skip-voice-restore   跳过音色还原
  --ffmpeg PATH          指定 ffmpeg 路径
```

---

## 输出示例

```text
STAGE 1: SPEECH-TO-TEXT TRANSCRIPTION
[OK] Transcription complete
Original text:
This is damn good work, you stupid idiot!

STAGE 2: TOXICITY DETECTION & TEXT DETOXIFICATION
Detoxification Report:
  Original toxicity: HIGH
  Toxic words found: 3
  Words replaced: 3

Detoxified text:
This is darn good work, you unwise foolish person!

STAGE 3: TEXT-TO-SPEECH SYNTHESIS
[OK] Speech synthesis complete

STAGE 4: VOICE CHARACTERISTIC RESTORATION
[OK] Voice characteristic restoration complete

PROCESSING COMPLETE
  [OK] Output: output.wav
```

---

## 项目结构

```text
voice-detoxification/
  app/cli/
    main.cpp                  # 四阶段 CLI 流水线
  src/
    audio/
      audio_decoder.*         # ffmpeg 音频解码
      voice_analyzer.*        # 原始音频特征提取和音色迁移
      wav_writer.*            # WAV 读写
    nlp/
      toxicity_detector.*     # 毒性检测
      text_detoxifier.*       # 文本替换和打码
    stt/
      whisper_engine.*        # Whisper 语音转文字
    tts/
      piper_engine.*          # Piper 文字转语音
    common/
      logger.*                # 日志
  scripts/
    advanced_detoxify.py      # 可选 ML 去毒增强
    piper_tts.py              # Piper 脚本辅助
    requirements.txt
  third_party/
    whisper.cpp/
    piper/
  models/
    ggml-small.bin
    piper-en_US-libritts-high.onnx
    piper-en_US-libritts-high.onnx.json
```

---

## 故障排查

### Whisper 模型加载失败

检查模型路径是否正确：

```bash
dir models
```

或换成明确路径：

```bash
./build/bin/voice_detox_app.exe C:/path/to/ggml-small.bin tests/inputs/sample.mp3
```

### ffmpeg 不可用

安装 ffmpeg，或用 `--ffmpeg` 指定路径：

```bash
./build/bin/voice_detox_app.exe \
  --ffmpeg C:/ffmpeg/bin/ffmpeg.exe \
  models/ggml-small.bin \
  tests/inputs/sample.mp3
```

### Piper 合成失败

确认 Python 包和模型文件都存在：

```bash
pip show piper-tts
dir models\piper*.onnx*
```

### 输出声音像静音

通常是 Piper 调用失败后进入了占位输出。优先检查：

- `piper-tts` 是否安装到当前 Python 环境。
- `.onnx` 和 `.onnx.json` 是否同名且同目录。
- `--tts-model` 是否指向真实存在的 `.onnx` 文件。

---

## 后续增强方向

- 更完整的多语言毒性词表
- 自定义词表配置文件
- 更强的上下文毒性检测
- 真正的神经音色克隆
- Web UI
- REST 或 gRPC API
- GPU 加速
- 实时流式处理
