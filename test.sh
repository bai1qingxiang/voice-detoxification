#!/bin/bash
# Test script for Voice Detoxification System

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
BIN_DIR="$BUILD_DIR/bin"
APP="$BIN_DIR/voice_detox_app.exe"

if [ ! -f "$APP" ]; then
    echo "Error: Application not found at $APP"
    echo "Please build the project first: mkdir build && cd build && cmake .. && cmake --build ."
    exit 1
fi

echo "=========================================="
echo "Voice Detoxification System - Test Suite"
echo "=========================================="
echo ""

# Test 1: Check if application runs with --help
echo "[Test 1] Checking if application responds to --help"
if $APP --help > /dev/null 2>&1; then
    echo "✓ PASS: Application help works"
else
    echo "✗ FAIL: Application help failed"
fi

echo ""
echo "=========================================="
echo "All tests completed"
echo "=========================================="
echo ""
echo "To test with actual audio:"
echo "1. Place audio files in: $PROJECT_ROOT/tests/inputs/"
echo "2. Download a Whisper model: models/ggml-medium.bin"
echo "3. Run: $APP models/ggml-medium.bin"
