#!/usr/bin/env python3
"""
Piper TTS Integration Script for Voice Detoxification

This script provides text-to-speech functionality using Piper.
Piper is a fast, local neural text-to-speech engine.

Install Piper:
    pip install piper-tts

Download models from:
    https://huggingface.co/rhasspy/piper-voices/tree/main

Example models (all < 200MB):
    - en_US-libritts-high (recommended)
    - en_US-libritts-medium
    - en_US-glow-tts
"""

import sys
import subprocess
import json
import os
from pathlib import Path


def install_piper():
    """安装Piper TTS"""
    print("Installing Piper TTS...")
    subprocess.run([sys.executable, "-m", "pip", "install", "piper-tts"], check=False)


def download_model(model_name: str, output_dir: str = "models"):
    """下载Piper模型"""
    Path(output_dir).mkdir(exist_ok=True)

    model_url = f"https://huggingface.co/rhasspy/piper-voices/resolve/main/en/en_US/{model_name}/en_US-{model_name}.onnx"
    model_json_url = f"{model_url.replace('.onnx', '.onnx.json')}"

    model_path = Path(output_dir) / f"piper-{model_name}.onnx"
    model_json_path = Path(output_dir) / f"piper-{model_name}.onnx.json"

    if not model_path.exists():
        print(f"Downloading model: {model_name}")
        os.system(f"curl -L {model_url} -o {model_path}")

    if not model_json_path.exists():
        print(f"Downloading model config: {model_name}")
        os.system(f"curl -L {model_json_url} -o {model_json_path}")

    return str(model_path)


def synthesize_text(text: str, model_path: str, output_wav: str):
    """使用Piper合成文本为语音"""
    try:
        from piper import PiperVoice

        print(f"Loading model: {model_path}")
        voice = PiperVoice.load(model_path)

        print(f"Synthesizing text...")
        with open(output_wav, 'wb') as f:
            voice.synthesize(text, f)

        print(f"✓ Synthesized speech saved to: {output_wav}")
        return True

    except ImportError:
        print("Error: Piper not installed. Installing...")
        install_piper()
        print("Please run the script again.")
        return False
    except Exception as e:
        print(f"Error: {e}")
        return False


def main():
    if len(sys.argv) < 3:
        print("Usage: piper_tts.py <model_name> <text> <output_wav>")
        print("\nAvailable models:")
        print("  - libritts-high (recommended, best quality)")
        print("  - libritts-medium (good balance)")
        print("  - glow-tts (fastest)")
        print("\nExample:")
        print("  python piper_tts.py libritts-high \"Hello world\" output.wav")
        sys.exit(1)

    model_name = sys.argv[1]
    text = sys.argv[2]
    output_wav = sys.argv[3]

    # Download model if not exists
    model_path = f"models/piper-{model_name}.onnx"
    if not Path(model_path).exists():
        print(f"Model {model_path} not found. Downloading...")
        model_path = download_model(model_name)

    # Synthesize
    if synthesize_text(text, model_path, output_wav):
        print("SUCCESS")
    else:
        print("FAILED")
        sys.exit(1)


if __name__ == "__main__":
    main()
