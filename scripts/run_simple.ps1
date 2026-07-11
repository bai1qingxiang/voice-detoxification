param(
    [string]$Model = "models\ggml-small.bin",
    [string]$Audio = "tests\inputs\sample.m4a",
    [string]$Output = "simple_output.wav",
    [switch]$CensorOnly,
    [switch]$SkipTts,
    [switch]$RebuildThirdParty
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$ProjectRoot = $ProjectRoot.Path
$ExistingLibDir = Join-Path $ProjectRoot "build\lib"
$ManualBuildDir = Join-Path $ProjectRoot "build_simple_manual"
$ManualApp = Join-Path $ManualBuildDir "bin\voice_detox_app.exe"
$CMakeBuildDir = Join-Path $ProjectRoot "build_simple"
$CMakeApp = Join-Path $CMakeBuildDir "bin\voice_detox_app.exe"

function Test-ExistingWhisperLibraries {
    $required = @(
        "libwhisper.a",
        "ggml.a",
        "ggml-cpu.a",
        "ggml-base.a"
    )

    foreach ($name in $required) {
        if (-not (Test-Path (Join-Path $ExistingLibDir $name))) {
            return $false
        }
    }
    return $true
}

function Build-WithExistingLibraries {
    New-Item -ItemType Directory -Force (Join-Path $ManualBuildDir "bin") | Out-Null

    $root = $ProjectRoot.Replace("\", "/")
    $args = @(
        "-std=c++17",
        "-O2",
        "-DGGML_USE_CPU",
        "-I$root/src",
        "-I$root/third_party/whisper.cpp/src",
        "-I$root/third_party/whisper.cpp/include",
        "-I$root/third_party/whisper.cpp/ggml/include",
        "app/cli/main.cpp",
        "src/audio/audio_decoder.cpp",
        "src/audio/voice_analyzer.cpp",
        "src/audio/wav_writer.cpp",
        "src/common/logger.cpp",
        "src/nlp/text_detoxifier.cpp",
        "src/nlp/toxicity_detector.cpp",
        "src/stt/whisper_engine.cpp",
        "src/tts/piper_engine.cpp",
        "-Lbuild/lib",
        "-lwhisper",
        "-l:ggml.a",
        "-l:ggml-cpu.a",
        "-l:ggml-base.a",
        "-fopenmp",
        "-lws2_32",
        "-lbcrypt",
        "-o",
        "build_simple_manual/bin/voice_detox_app.exe"
    )

    Push-Location $ProjectRoot
    try {
        & g++ @args
    } finally {
        Pop-Location
    }

    return $ManualApp
}

function Build-WithCMake {
    Push-Location $ProjectRoot
    try {
        & cmake -S . -B build_simple -DGGML_CCACHE=OFF -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc -DCMAKE_BUILD_TYPE=Release
        & cmake --build build_simple --config Release --target voice_detox_app --parallel 1
    } finally {
        Pop-Location
    }

    return $CMakeApp
}

if ((Test-ExistingWhisperLibraries) -and -not $RebuildThirdParty) {
    Write-Host "Building app with existing Whisper/ggml libraries..."
    $App = Build-WithExistingLibraries
} else {
    Write-Host "Building app with CMake and ccache disabled..."
    $App = Build-WithCMake
}

if (-not (Test-Path $App)) {
    throw "Application was not built: $App"
}

$runArgs = @("--output", $Output)
if ($CensorOnly) {
    $runArgs += "--censor-only"
}
if ($SkipTts) {
    $runArgs += "--skip-tts"
    $runArgs += "--skip-voice-restore"
}
$runArgs += $Model
$runArgs += $Audio

Push-Location $ProjectRoot
try {
    & $App @runArgs
} finally {
    Pop-Location
}
