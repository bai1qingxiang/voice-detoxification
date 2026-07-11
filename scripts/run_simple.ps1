param(
    [string]$Model = "models\ggml-small.bin",
    [string]$Audio = "",
    [string]$Output = "",
    [switch]$CensorOnly,
    [switch]$SkipTts,
    [switch]$RestoreVoice,
    [switch]$ChooseAudio,
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

function Select-AudioFile {
    Add-Type -AssemblyName System.Windows.Forms

    $dialog = New-Object System.Windows.Forms.OpenFileDialog
    $dialog.Title = "Choose audio file to detoxify"
    $dialog.Filter = "Audio files (*.mp3;*.wav;*.m4a;*.flac;*.ogg;*.aac;*.wma)|*.mp3;*.wav;*.m4a;*.flac;*.ogg;*.aac;*.wma|All files (*.*)|*.*"
    $dialog.Multiselect = $false

    if ($dialog.ShowDialog() -ne [System.Windows.Forms.DialogResult]::OK) {
        throw "No audio file was selected."
    }

    return $dialog.FileName
}

function Get-DefaultOutputPath {
    param([string]$AudioPath)

    $resolvedAudio = Resolve-Path $AudioPath
    $audioDir = Split-Path -Parent $resolvedAudio.Path
    $audioName = [System.IO.Path]::GetFileNameWithoutExtension($resolvedAudio.Path)
    return Join-Path $audioDir ($audioName + "_clean.wav")
}

function Resolve-AudioInputPath {
    param([string]$AudioPath)

    if ([System.IO.Path]::IsPathRooted($AudioPath)) {
        return (Resolve-Path $AudioPath).Path
    }

    $fromCurrent = Join-Path (Get-Location) $AudioPath
    if (Test-Path $fromCurrent) {
        return (Resolve-Path $fromCurrent).Path
    }

    $fromProject = Join-Path $ProjectRoot $AudioPath
    if (Test-Path $fromProject) {
        return (Resolve-Path $fromProject).Path
    }

    throw "Audio file was not found: $AudioPath"
}

if ($ChooseAudio) {
    $Audio = Select-AudioFile
} elseif ([string]::IsNullOrWhiteSpace($Audio)) {
    $Audio = "tests\inputs\sample.m4a"
}

$Audio = Resolve-AudioInputPath $Audio

if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Get-DefaultOutputPath $Audio
}

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
} elseif (-not $RestoreVoice) {
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
