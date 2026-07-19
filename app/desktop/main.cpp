#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <mmsystem.h>
#include <shellapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr UINT WM_APP_AUDIO_LEVEL = WM_APP + 1;
constexpr UINT WM_APP_PROCESS_STATUS = WM_APP + 2;
constexpr UINT WM_APP_PROCESS_FINISHED = WM_APP + 3;
constexpr UINT WM_APP_PLAYBACK_ERROR = WM_APP + 4;
constexpr UINT_PTR RECORD_TIMER_ID = 1;
constexpr int MAX_RECORDING_SECONDS = 120;

constexpr int ID_RECORD_BUTTON = 1001;
constexpr int ID_MODE_COMBO = 1002;
constexpr int ID_PLAY_BUTTON = 1003;
constexpr int ID_OPEN_BUTTON = 1004;
constexpr int ID_SAVE_BUTTON = 1005;
constexpr int ID_PROGRESS = 1006;
constexpr int ID_WAVEFORM = 1007;
constexpr int ID_INPUT_DEVICE_COMBO = 1008;
constexpr int ID_OUTPUT_DEVICE_COMBO = 1009;

constexpr COLORREF COLOR_APP_BACKGROUND = RGB(245, 246, 244);
constexpr COLORREF COLOR_SURFACE = RGB(255, 255, 255);
constexpr COLORREF COLOR_TEXT = RGB(35, 39, 36);
constexpr COLORREF COLOR_MUTED = RGB(105, 112, 107);
constexpr COLORREF COLOR_PRIMARY = RGB(38, 108, 90);
constexpr COLORREF COLOR_DANGER = RGB(194, 70, 61);
constexpr COLORREF COLOR_BORDER = RGB(220, 224, 221);

std::atomic<uint64_t> playback_generation{0};

struct ProcessStatus {
    std::wstring text;
    int progress = 0;
};

struct ProcessFinished {
    bool success = false;
    std::wstring output_path;
    std::wstring message;
};

std::wstring quote_argument(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (wchar_t c : value) {
        if (c == L'\\') {
            ++backslashes;
        } else if (c == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
        } else {
            result.append(backslashes, L'\\');
            backslashes = 0;
            result.push_back(c);
        }
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

fs::path executable_path() {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    return fs::path(std::wstring(buffer.data(), length));
}

fs::path find_project_root() {
    fs::path current = executable_path().parent_path();
    for (int depth = 0; depth < 5; ++depth) {
        if (fs::exists(current / "models" / "ggml-small.bin")) return current;
        if (!current.has_parent_path()) break;
        current = current.parent_path();
    }
    return fs::current_path();
}

std::wstring timestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    wchar_t value[32]{};
    swprintf(value, 32, L"%04d%02d%02d_%02d%02d%02d",
             time.wYear, time.wMonth, time.wDay,
             time.wHour, time.wMinute, time.wSecond);
    return value;
}

std::wstring format_duration(int seconds) {
    wchar_t text[16]{};
    swprintf(text, 16, L"%02d:%02d", seconds / 60, seconds % 60);
    return text;
}

bool write_wav_file(const fs::path& path, const std::vector<int16_t>& samples) {
    std::ofstream stream(path, std::ios::binary);
    if (!stream) return false;

    const uint32_t data_size = static_cast<uint32_t>(samples.size() * sizeof(int16_t));
    const uint32_t riff_size = 36 + data_size;
    const uint32_t sample_rate = 16000;
    const uint16_t channels = 1;
    const uint16_t bits_per_sample = 16;
    const uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
    const uint16_t block_align = channels * bits_per_sample / 8;
    const uint32_t fmt_size = 16;
    const uint16_t format = 1;

    stream.write("RIFF", 4);
    stream.write(reinterpret_cast<const char*>(&riff_size), sizeof(riff_size));
    stream.write("WAVEfmt ", 8);
    stream.write(reinterpret_cast<const char*>(&fmt_size), sizeof(fmt_size));
    stream.write(reinterpret_cast<const char*>(&format), sizeof(format));
    stream.write(reinterpret_cast<const char*>(&channels), sizeof(channels));
    stream.write(reinterpret_cast<const char*>(&sample_rate), sizeof(sample_rate));
    stream.write(reinterpret_cast<const char*>(&byte_rate), sizeof(byte_rate));
    stream.write(reinterpret_cast<const char*>(&block_align), sizeof(block_align));
    stream.write(reinterpret_cast<const char*>(&bits_per_sample), sizeof(bits_per_sample));
    stream.write("data", 4);
    stream.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));
    stream.write(reinterpret_cast<const char*>(samples.data()), data_size);
    return stream.good();
}

bool contains_case_insensitive(std::wstring value, std::wstring needle) {
    std::transform(value.begin(), value.end(), value.begin(), ::towlower);
    std::transform(needle.begin(), needle.end(), needle.begin(), ::towlower);
    return value.find(needle) != std::wstring::npos;
}

struct WavPlaybackData {
    WAVEFORMATEX format{};
    std::vector<char> samples;
};

bool load_wav_for_playback(const fs::path& path, WavPlaybackData& wav) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;

    char riff[4]{};
    uint32_t riff_size = 0;
    char wave[4]{};
    stream.read(riff, 4);
    stream.read(reinterpret_cast<char*>(&riff_size), sizeof(riff_size));
    stream.read(wave, 4);
    if (std::string(riff, 4) != "RIFF" || std::string(wave, 4) != "WAVE") return false;

    bool has_format = false;
    bool has_data = false;
    while (stream && (!has_format || !has_data)) {
        char chunk_id[4]{};
        uint32_t chunk_size = 0;
        stream.read(chunk_id, 4);
        stream.read(reinterpret_cast<char*>(&chunk_size), sizeof(chunk_size));
        if (!stream) break;

        const std::string id(chunk_id, 4);
        if (id == "fmt " && chunk_size >= 16) {
            std::array<char, sizeof(WAVEFORMATEX)> format_bytes{};
            const uint32_t bytes_to_read = std::min<uint32_t>(chunk_size, static_cast<uint32_t>(format_bytes.size()));
            stream.read(format_bytes.data(), bytes_to_read);
            std::memcpy(&wav.format, format_bytes.data(), bytes_to_read);
            if (chunk_size > bytes_to_read) stream.seekg(chunk_size - bytes_to_read, std::ios::cur);
            has_format = true;
        } else if (id == "data") {
            wav.samples.resize(chunk_size);
            stream.read(wav.samples.data(), chunk_size);
            has_data = stream.good() || stream.gcount() == static_cast<std::streamsize>(chunk_size);
        } else {
            stream.seekg(chunk_size, std::ios::cur);
        }
        if (chunk_size & 1U) stream.seekg(1, std::ios::cur);
    }

    return has_format && has_data && wav.format.wFormatTag == WAVE_FORMAT_PCM && !wav.samples.empty();
}

void post_playback_error(HWND window, const wchar_t* message) {
    auto* text = new std::wstring(message);
    if (!PostMessageW(window, WM_APP_PLAYBACK_ERROR, 0, reinterpret_cast<LPARAM>(text))) delete text;
}

void play_wav_async(HWND window, const fs::path& path, UINT device_id, uint64_t generation) {
    std::thread([window, path, device_id, generation]() {
        WavPlaybackData wav;
        if (!load_wav_for_playback(path, wav)) {
            post_playback_error(window, L"无法读取输出音频文件。");
            return;
        }

        HWAVEOUT output = nullptr;
        if (waveOutOpen(&output, device_id, &wav.format, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
            post_playback_error(window, L"无法打开所选播放设备。");
            return;
        }

        WAVEHDR header{};
        header.lpData = wav.samples.data();
        header.dwBufferLength = static_cast<DWORD>(wav.samples.size());
        if (waveOutPrepareHeader(output, &header, sizeof(header)) != MMSYSERR_NOERROR ||
            waveOutWrite(output, &header, sizeof(header)) != MMSYSERR_NOERROR) {
            waveOutClose(output);
            post_playback_error(window, L"音频播放启动失败。");
            return;
        }

        while ((header.dwFlags & WHDR_DONE) == 0 && playback_generation.load() == generation) {
            Sleep(20);
        }
        if (playback_generation.load() != generation) waveOutReset(output);
        waveOutUnprepareHeader(output, &header, sizeof(header));
        waveOutClose(output);
    }).detach();
}

class AudioRecorder {
public:
    bool start(HWND notify_window, UINT device_id, std::wstring& error) {
        if (recording_) return false;
        notify_window_ = notify_window;
        samples_.clear();

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 1;
        format.nSamplesPerSec = 16000;
        format.wBitsPerSample = 16;
        format.nBlockAlign = format.nChannels * format.wBitsPerSample / 8;
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        MMRESULT result = waveInOpen(
            &handle_, device_id, &format,
            reinterpret_cast<DWORD_PTR>(&AudioRecorder::wave_callback),
            reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
        if (result != MMSYSERR_NOERROR) {
            error = L"无法打开麦克风，请检查系统录音权限。";
            handle_ = nullptr;
            return false;
        }

        recording_ = true;
        for (size_t index = 0; index < headers_.size(); ++index) {
            headers_[index] = {};
            headers_[index].lpData = buffers_[index].data();
            headers_[index].dwBufferLength = static_cast<DWORD>(buffers_[index].size());
            if (waveInPrepareHeader(handle_, &headers_[index], sizeof(WAVEHDR)) != MMSYSERR_NOERROR ||
                waveInAddBuffer(handle_, &headers_[index], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
                error = L"初始化录音缓冲区失败。";
                stop_internal();
                return false;
            }
        }

        if (waveInStart(handle_) != MMSYSERR_NOERROR) {
            error = L"麦克风录音启动失败。";
            stop_internal();
            return false;
        }
        return true;
    }

    bool stop_and_save(const fs::path& path, std::wstring& error) {
        if (!handle_) return false;
        recording_ = false;
        waveInStop(handle_);
        waveInReset(handle_);

        for (auto& header : headers_) {
            if (header.dwFlags & WHDR_PREPARED) {
                waveInUnprepareHeader(handle_, &header, sizeof(WAVEHDR));
            }
        }
        waveInClose(handle_);
        handle_ = nullptr;

        std::vector<int16_t> captured;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            captured = samples_;
        }
        if (captured.size() < 1600) {
            error = L"录音时间太短，请至少录制 0.1 秒。";
            return false;
        }
        int peak = 0;
        long double square_sum = 0.0;
        for (int16_t sample : captured) {
            peak = std::max(peak, std::abs(static_cast<int>(sample)));
            const long double normalized = static_cast<long double>(sample) / 32768.0L;
            square_sum += normalized * normalized;
        }
        const long double rms = std::sqrt(square_sum / static_cast<long double>(captured.size()));
        if (peak < 64 || rms < 0.0005L) {
            error = L"没有检测到有效声音。请选择正确的麦克风，并确认设备没有静音。";
            return false;
        }
        if (!write_wav_file(path, captured)) {
            error = L"无法保存录音文件。";
            return false;
        }
        return true;
    }

    bool is_recording() const { return recording_; }

private:
    static void CALLBACK wave_callback(HWAVEIN, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR) {
        if (message != WIM_DATA || instance == 0) return;
        auto* recorder = reinterpret_cast<AudioRecorder*>(instance);
        recorder->on_audio(reinterpret_cast<WAVEHDR*>(param1));
    }

    void on_audio(WAVEHDR* header) {
        if (header->dwBytesRecorded > 0) {
            const auto* input = reinterpret_cast<const int16_t*>(header->lpData);
            const size_t sample_count = header->dwBytesRecorded / sizeof(int16_t);
            if (recording_) {
                std::lock_guard<std::mutex> lock(mutex_);
                samples_.insert(samples_.end(), input, input + sample_count);
            }

            double sum = 0.0;
            for (size_t i = 0; i < sample_count; ++i) {
                const double normalized = static_cast<double>(input[i]) / 32768.0;
                sum += normalized * normalized;
            }
            const double rms = sample_count ? std::sqrt(sum / sample_count) : 0.0;
            const int level = std::clamp(static_cast<int>(rms * 650.0), 0, 100);
            PostMessageW(notify_window_, WM_APP_AUDIO_LEVEL, static_cast<WPARAM>(level), 0);
        }

        if (recording_ && handle_) {
            header->dwBytesRecorded = 0;
            waveInAddBuffer(handle_, header, sizeof(WAVEHDR));
        }
    }

    void stop_internal() {
        recording_ = false;
        if (!handle_) return;
        waveInReset(handle_);
        for (auto& header : headers_) {
            if (header.dwFlags & WHDR_PREPARED) waveInUnprepareHeader(handle_, &header, sizeof(WAVEHDR));
        }
        waveInClose(handle_);
        handle_ = nullptr;
    }

    HWAVEIN handle_ = nullptr;
    HWND notify_window_ = nullptr;
    std::atomic<bool> recording_{false};
    std::mutex mutex_;
    std::vector<int16_t> samples_;
    std::array<std::array<char, 4096>, 4> buffers_{};
    std::array<WAVEHDR, 4> headers_{};
};

struct AppContext {
    HWND window = nullptr;
    HWND waveform = nullptr;
    HWND record_button = nullptr;
    HWND record_time = nullptr;
    HWND record_hint = nullptr;
    HWND mode_combo = nullptr;
    HWND mode_label = nullptr;
    HWND input_device_combo = nullptr;
    HWND input_device_label = nullptr;
    HWND output_device_combo = nullptr;
    HWND output_device_label = nullptr;
    HWND result_title = nullptr;
    HWND result_detail = nullptr;
    HWND progress = nullptr;
    HWND output_path = nullptr;
    HWND play_button = nullptr;
    HWND open_button = nullptr;
    HWND save_button = nullptr;
    HFONT title_font = nullptr;
    HFONT body_font = nullptr;
    HFONT small_font = nullptr;
    HBRUSH window_brush = nullptr;
    HBRUSH surface_brush = nullptr;
    AudioRecorder recorder;
    std::atomic<bool> processing{false};
    int seconds = 0;
    int audio_level = 0;
    fs::path project_root;
    fs::path input_path;
    fs::path output_file;
    std::vector<UINT> input_device_ids;
    std::vector<UINT> output_device_ids;
};

AppContext app;

void set_font(HWND control, HFONT font) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void populate_input_devices() {
    app.input_device_ids.clear();
    SendMessageW(app.input_device_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(app.input_device_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"系统默认录音设备"));
    app.input_device_ids.push_back(WAVE_MAPPER);

    int preferred_index = 0;
    const UINT count = waveInGetNumDevs();
    for (UINT device_id = 0; device_id < count; ++device_id) {
        WAVEINCAPSW capabilities{};
        if (waveInGetDevCapsW(device_id, &capabilities, sizeof(capabilities)) != MMSYSERR_NOERROR) continue;
        const std::wstring name = capabilities.szPname;
        SendMessageW(app.input_device_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        app.input_device_ids.push_back(device_id);

        const bool physical = contains_case_insensitive(name, L"realtek") ||
                              contains_case_insensitive(name, L"麦克风阵列") ||
                              contains_case_insensitive(name, L"microphone array");
        const bool virtual_device = contains_case_insensitive(name, L"virtual") ||
                                    contains_case_insensitive(name, L"streaming") ||
                                    contains_case_insensitive(name, L"todesk") ||
                                    contains_case_insensitive(name, L"steam");
        if (preferred_index == 0 && physical && !virtual_device) {
            preferred_index = static_cast<int>(app.input_device_ids.size() - 1);
        }
    }
    SendMessageW(app.input_device_combo, CB_SETCURSEL, preferred_index, 0);
    SendMessageW(app.input_device_combo, CB_SETDROPPEDWIDTH, 420, 0);
}

void populate_output_devices() {
    app.output_device_ids.clear();
    SendMessageW(app.output_device_combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(app.output_device_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"系统默认播放设备"));
    app.output_device_ids.push_back(WAVE_MAPPER);

    int preferred_index = 0;
    const UINT count = waveOutGetNumDevs();
    for (UINT device_id = 0; device_id < count; ++device_id) {
        WAVEOUTCAPSW capabilities{};
        if (waveOutGetDevCapsW(device_id, &capabilities, sizeof(capabilities)) != MMSYSERR_NOERROR) continue;
        const std::wstring name = capabilities.szPname;
        SendMessageW(app.output_device_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        app.output_device_ids.push_back(device_id);

        const bool preferred = contains_case_insensitive(name, L"realtek") ||
                               contains_case_insensitive(name, L"扬声器") ||
                               contains_case_insensitive(name, L"speakers");
        const bool virtual_device = contains_case_insensitive(name, L"virtual") ||
                                    contains_case_insensitive(name, L"streaming") ||
                                    contains_case_insensitive(name, L"todesk") ||
                                    contains_case_insensitive(name, L"steam");
        if (preferred_index == 0 && preferred && !virtual_device) {
            preferred_index = static_cast<int>(app.output_device_ids.size() - 1);
        }
    }
    SendMessageW(app.output_device_combo, CB_SETCURSEL, preferred_index, 0);
    SendMessageW(app.output_device_combo, CB_SETDROPPEDWIDTH, 420, 0);
}

UINT selected_device(HWND combo, const std::vector<UINT>& device_ids) {
    const LRESULT selection = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (selection == CB_ERR || static_cast<size_t>(selection) >= device_ids.size()) return WAVE_MAPPER;
    return device_ids[static_cast<size_t>(selection)];
}

void show_result_buttons(bool show) {
    const int command = show ? SW_SHOW : SW_HIDE;
    ShowWindow(app.play_button, command);
    ShowWindow(app.open_button, command);
    ShowWindow(app.save_button, command);
    ShowWindow(app.output_path, command);
}

void set_recording_ui(bool recording) {
    SetWindowTextW(app.record_button, recording ? L"停止" : L"录音");
    SetWindowTextW(app.record_hint, recording ? L"再次点击结束录音并开始处理" : L"点击按钮开始讲话");
    EnableWindow(app.mode_combo, !recording);
    EnableWindow(app.input_device_combo, !recording);
    EnableWindow(app.output_device_combo, !recording);
    InvalidateRect(app.record_button, nullptr, TRUE);
}

void set_processing_ui() {
    app.processing = true;
    EnableWindow(app.record_button, FALSE);
    EnableWindow(app.mode_combo, FALSE);
    EnableWindow(app.input_device_combo, FALSE);
    EnableWindow(app.output_device_combo, FALSE);
    SetWindowTextW(app.result_title, L"正在处理");
    SetWindowTextW(app.result_detail, L"正在读取录音...");
    SendMessageW(app.progress, PBM_SETPOS, 5, 0);
    ShowWindow(app.progress, SW_SHOW);
    show_result_buttons(false);
}

void post_status(HWND window, const wchar_t* text, int progress) {
    auto* update = new ProcessStatus{text, progress};
    if (!PostMessageW(window, WM_APP_PROCESS_STATUS, 0, reinterpret_cast<LPARAM>(update))) delete update;
}

void run_pipeline_async(HWND window, fs::path input_path, fs::path output_path) {
    std::thread([window, input_path = std::move(input_path), output_path = std::move(output_path)]() {
        auto* finished = new ProcessFinished{};
        finished->output_path = output_path.wstring();

        const fs::path cli_path = executable_path().parent_path() / "voice_detox_app.exe";
        const fs::path model_path = app.project_root / "models" / "ggml-small.bin";

        if (!fs::exists(cli_path) || !fs::exists(model_path)) {
            finished->message = L"缺少处理程序或模型文件，请重新运行 desktop.bat 构建。";
            PostMessageW(window, WM_APP_PROCESS_FINISHED, 0, reinterpret_cast<LPARAM>(finished));
            return;
        }

        std::error_code remove_error;
        fs::remove(output_path, remove_error);
        fs::create_directories(output_path.parent_path());

        std::wstring command = quote_argument(cli_path.wstring()) +
            L" --output " + quote_argument(output_path.wstring()) +
            L" ";
        command += quote_argument(model_path.wstring()) + L" " + quote_argument(input_path.wstring());

        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        HANDLE read_pipe = nullptr;
        HANDLE write_pipe = nullptr;
        if (!CreatePipe(&read_pipe, &write_pipe, &security, 0)) {
            finished->message = L"无法启动处理管线。";
            PostMessageW(window, WM_APP_PROCESS_FINISHED, 0, reinterpret_cast<LPARAM>(finished));
            return;
        }
        SetHandleInformation(read_pipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = write_pipe;
        startup.hStdError = write_pipe;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        std::wstring working_directory = app.project_root.wstring();

        const BOOL created = CreateProcessW(
            nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, working_directory.c_str(), &startup, &process);
        CloseHandle(write_pipe);

        if (!created) {
            CloseHandle(read_pipe);
            finished->message = L"处理程序启动失败。";
            PostMessageW(window, WM_APP_PROCESS_FINISHED, 0, reinterpret_cast<LPARAM>(finished));
            return;
        }

        std::string output;
        std::array<char, 4096> buffer{};
        DWORD bytes_read = 0;
        int announced_stage = 0;
        while (ReadFile(read_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr) && bytes_read > 0) {
            output.append(buffer.data(), bytes_read);
            if (announced_stage < 1 && output.find("STAGE 1") != std::string::npos) {
                announced_stage = 1;
                post_status(window, L"正在识别语音", 22);
            }
            if (announced_stage < 2 && output.find("STAGE 2") != std::string::npos) {
                announced_stage = 2;
                post_status(window, L"正在检测并处理不当表达", 55);
            }
            if (announced_stage < 3 && output.find("STAGE 3") != std::string::npos) {
                announced_stage = 3;
                post_status(window, L"正在将敏感词替换为静音", 78);
            }
        }
        CloseHandle(read_pipe);

        WaitForSingleObject(process.hProcess, INFINITE);
        DWORD exit_code = 1;
        GetExitCodeProcess(process.hProcess, &exit_code);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);

        finished->success = exit_code == 0 && fs::exists(output_path);
        if (!finished->success) {
            finished->message = exit_code == 0
                ? L"处理已完成，但未生成静音处理后的音频。"
                : L"语音处理失败，请确认模型与 FFmpeg 可用。";
        }
        PostMessageW(window, WM_APP_PROCESS_FINISHED, 0, reinterpret_cast<LPARAM>(finished));
    }).detach();
}

void begin_recording(HWND window) {
    ++playback_generation;
    std::wstring error;
    const UINT input_device = selected_device(app.input_device_combo, app.input_device_ids);
    if (!app.recorder.start(window, input_device, error)) {
        MessageBoxW(window, error.c_str(), L"无法录音", MB_OK | MB_ICONERROR);
        return;
    }
    app.seconds = 0;
    SetWindowTextW(app.record_time, L"00:00");
    SetTimer(window, RECORD_TIMER_ID, 1000, nullptr);
    set_recording_ui(true);
    SetWindowTextW(app.result_title, L"正在录音");
    SetWindowTextW(app.result_detail, L"完成后点击左侧录音按钮");
    show_result_buttons(false);
}

void finish_recording(HWND window) {
    KillTimer(window, RECORD_TIMER_ID);
    wchar_t temp_directory[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp_directory);
    app.input_path = fs::path(temp_directory) / (L"clearvoice_" + timestamp() + L".wav");

    std::wstring error;
    if (!app.recorder.stop_and_save(app.input_path, error)) {
        set_recording_ui(false);
        MessageBoxW(window, error.c_str(), L"录音失败", MB_OK | MB_ICONERROR);
        SetWindowTextW(app.result_title, L"等待语音输入");
        SetWindowTextW(app.result_detail, L"录音完成后，处理结果会显示在这里");
        return;
    }

    set_recording_ui(false);
    set_processing_ui();
    const fs::path output_directory = app.project_root / "output";
    app.output_file = output_directory / (L"clean_" + timestamp() + L".wav");
    run_pipeline_async(window, app.input_path, app.output_file);
}

void save_output_copy(HWND window) {
    if (app.output_file.empty() || !fs::exists(app.output_file)) return;
    wchar_t file_name[MAX_PATH]{};
    wcscpy_s(file_name, L"clean_voice.wav");
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window;
    dialog.lpstrFilter = L"WAV 音频 (*.wav)\0*.wav\0所有文件 (*.*)\0*.*\0";
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = MAX_PATH;
    dialog.lpstrDefExt = L"wav";
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameW(&dialog)) {
        std::error_code error;
        fs::copy_file(app.output_file, fs::path(file_name), fs::copy_options::overwrite_existing, error);
        if (error) MessageBoxW(window, L"保存文件失败。", L"保存", MB_OK | MB_ICONERROR);
    }
}

void layout_controls(int width, int height) {
    const int margin = 28;
    const int gap = 14;
    const int header_height = 76;
    const int settings_top = 88;
    const int content_top = 196;
    const int content_height = std::max(390, height - content_top - margin);
    const int panel_width = std::max(300, (width - margin * 2 - gap) / 2);

    HWND header_panel = GetDlgItem(app.window, 2000);
    HWND left_group = GetDlgItem(app.window, 2001);
    HWND right_group = GetDlgItem(app.window, 2002);
    HWND left_label = GetDlgItem(app.window, 2003);
    HWND right_label = GetDlgItem(app.window, 2004);
    MoveWindow(header_panel, 0, 0, width, header_height, TRUE);
    MoveWindow(left_group, margin, content_top, panel_width, content_height, TRUE);
    MoveWindow(right_group, margin + panel_width + gap, content_top, panel_width, content_height, TRUE);
    MoveWindow(left_label, margin + 18, content_top + 13, 140, 22, TRUE);
    MoveWindow(right_label, margin + panel_width + gap + 18, content_top + 13, 140, 22, TRUE);

    const int inner_width = panel_width - 36;
    const int left_x = margin;
    const int right_x = margin + panel_width + gap;
    MoveWindow(app.waveform, left_x + 18, content_top + 46, inner_width, 86, TRUE);
    MoveWindow(app.record_button, left_x + (panel_width - 82) / 2, content_top + 157, 82, 82, TRUE);
    MoveWindow(app.record_time, left_x + 18, content_top + 250, inner_width, 25, TRUE);
    MoveWindow(app.record_hint, left_x + 18, content_top + 280, inner_width, 20, TRUE);

    const int settings_inner_x = margin + 18;
    const int settings_inner_width = width - margin * 2 - 36;
    const int settings_gap = 16;
    const int settings_column = (settings_inner_width - settings_gap * 2) / 3;
    MoveWindow(app.mode_label, settings_inner_x, settings_top + 14, settings_column, 18, TRUE);
    SetWindowPos(app.mode_combo, HWND_TOP, settings_inner_x, settings_top + 35, settings_column, 220, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    MoveWindow(app.input_device_label, settings_inner_x + settings_column + settings_gap, settings_top + 14, settings_column, 18, TRUE);
    SetWindowPos(app.input_device_combo, HWND_TOP, settings_inner_x + settings_column + settings_gap, settings_top + 35, settings_column, 220, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    MoveWindow(app.output_device_label, settings_inner_x + (settings_column + settings_gap) * 2, settings_top + 14, settings_column, 18, TRUE);
    SetWindowPos(app.output_device_combo, HWND_TOP, settings_inner_x + (settings_column + settings_gap) * 2, settings_top + 35, settings_column, 220, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RedrawWindow(app.mode_combo, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    RedrawWindow(app.input_device_combo, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
    RedrawWindow(app.output_device_combo, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);

    MoveWindow(app.result_title, right_x + 24, content_top + 76, panel_width - 48, 30, TRUE);
    MoveWindow(app.result_detail, right_x + 24, content_top + 112, panel_width - 48, 42, TRUE);
    MoveWindow(app.progress, right_x + 24, content_top + 167, panel_width - 48, 8, TRUE);
    MoveWindow(app.output_path, right_x + 24, content_top + 194, panel_width - 48, 46, TRUE);
    const int button_width = (panel_width - 64) / 3;
    MoveWindow(app.play_button, right_x + 24, content_top + content_height - 52, button_width, 34, TRUE);
    MoveWindow(app.open_button, right_x + 32 + button_width, content_top + content_height - 52, button_width, 34, TRUE);
    MoveWindow(app.save_button, right_x + 40 + button_width * 2, content_top + content_height - 52, button_width, 34, TRUE);
}

LRESULT CALLBACK waveform_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        FillRect(dc, &rect, app.surface_brush);
        const int level = static_cast<int>(GetWindowLongPtrW(window, GWLP_USERDATA));
        const int bars = 38;
        const int gap = 4;
        const int bar_width = std::max(2, static_cast<int>((rect.right - gap * (bars - 1)) / bars));
        HBRUSH bar_brush = CreateSolidBrush(level > 0 ? COLOR_PRIMARY : RGB(178, 190, 184));
        for (int index = 0; index < bars; ++index) {
            const double wave = 0.35 + 0.65 * std::abs(std::sin(index * 0.68));
            const int base = 8 + static_cast<int>((level / 100.0) * 54.0 * wave);
            const int bar_height = level > 0 ? base : 8 + (index * 7 % 13);
            const int x = index * (bar_width + gap);
            const int center = rect.bottom / 2;
            RECT bar{x, center - bar_height / 2, x + bar_width, center + bar_height / 2};
            FillRect(dc, &bar, bar_brush);
        }
        DeleteObject(bar_brush);
        EndPaint(window, &paint);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: {
        app.window = window;
        app.project_root = find_project_root();
        app.window_brush = CreateSolidBrush(COLOR_APP_BACKGROUND);
        app.surface_brush = CreateSolidBrush(COLOR_SURFACE);
        app.title_font = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        app.body_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        app.small_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");

        const COLORREF caption_color = RGB(255, 255, 255);
        const COLORREF caption_text_color = RGB(0, 0, 0);
        DwmSetWindowAttribute(window, DWMWA_CAPTION_COLOR, &caption_color, sizeof(caption_color));
        DwmSetWindowAttribute(window, DWMWA_TEXT_COLOR, &caption_text_color, sizeof(caption_text_color));

        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_WHITERECT, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(2000), nullptr, nullptr);
        HWND heading = CreateWindowW(L"STATIC", L"语音去毒", WS_CHILD | WS_VISIBLE, 28, 14, 300, 34, window, reinterpret_cast<HMENU>(3001), nullptr, nullptr);
        HWND subtitle = CreateWindowW(L"STATIC", L"录音结束后，自动生成处理后的完整语音。", WS_CHILD | WS_VISIBLE, 28, 46, 420, 20, window, reinterpret_cast<HMENU>(3002), nullptr, nullptr);
        set_font(heading, app.title_font);
        set_font(subtitle, app.small_font);

        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_WHITERECT, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(2001), nullptr, nullptr);
        CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | SS_WHITERECT, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(2002), nullptr, nullptr);
        HWND left_label = CreateWindowW(L"STATIC", L"语音输入", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(2003), nullptr, nullptr);
        HWND right_label = CreateWindowW(L"STATIC", L"处理结果", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(2004), nullptr, nullptr);
        set_font(left_label, app.body_font);
        set_font(right_label, app.body_font);

        app.waveform = CreateWindowW(L"ClearVoiceWaveform", nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_WAVEFORM), nullptr, nullptr);
        app.record_button = CreateWindowW(L"BUTTON", L"录音", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_RECORD_BUTTON), nullptr, nullptr);
        app.record_time = CreateWindowW(L"STATIC", L"00:00", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        app.record_hint = CreateWindowW(L"STATIC", L"点击按钮开始讲话", WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        app.mode_label = CreateWindowW(L"STATIC", L"净化方式", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        app.mode_combo = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_MODE_COMBO), nullptr, nullptr);
        SendMessageW(app.mode_combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"敏感词静音（保留原音）"));
        SendMessageW(app.mode_combo, CB_SETCURSEL, 0, 0);
        app.input_device_label = CreateWindowW(L"STATIC", L"录音设备", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        app.input_device_combo = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_INPUT_DEVICE_COMBO), nullptr, nullptr);

        app.result_title = CreateWindowW(L"STATIC", L"等待语音输入", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        app.result_detail = CreateWindowW(L"STATIC", L"录音完成后，处理进度和结果会显示在这里。", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        app.progress = CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_SMOOTH, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_PROGRESS), nullptr, nullptr);
        SendMessageW(app.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
        SendMessageW(app.progress, PBM_SETSTATE, PBST_NORMAL, 0);
        app.output_path = CreateWindowW(L"STATIC", L"", WS_CHILD | SS_LEFT, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        app.output_device_label = CreateWindowW(L"STATIC", L"播放设备", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 0, 0, window, nullptr, nullptr, nullptr);
        app.output_device_combo = CreateWindowExW(WS_EX_CLIENTEDGE, WC_COMBOBOXW, nullptr, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | CBS_HASSTRINGS, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_OUTPUT_DEVICE_COMBO), nullptr, nullptr);
        app.play_button = CreateWindowW(L"BUTTON", L"播放", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_PLAY_BUTTON), nullptr, nullptr);
        app.open_button = CreateWindowW(L"BUTTON", L"打开文件夹", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_OPEN_BUTTON), nullptr, nullptr);
        app.save_button = CreateWindowW(L"BUTTON", L"另存为", WS_CHILD | BS_PUSHBUTTON, 0, 0, 0, 0, window, reinterpret_cast<HMENU>(ID_SAVE_BUTTON), nullptr, nullptr);

        for (HWND control : {app.record_button, app.record_time, app.mode_combo, app.input_device_combo, app.output_device_combo, app.result_title, app.play_button, app.open_button, app.save_button}) set_font(control, app.body_font);
        for (HWND control : {app.record_hint, app.result_detail, app.output_path, app.mode_label, app.input_device_label, app.output_device_label}) set_font(control, app.small_font);
        populate_input_devices();
        populate_output_devices();
        show_result_buttons(false);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT client{};
        GetClientRect(window, &client);
        RECT settings_rect{28, 88, client.right - 28, 180};
        FillRect(dc, &settings_rect, app.surface_brush);
        HBRUSH border = CreateSolidBrush(RGB(135, 140, 136));
        FrameRect(dc, &settings_rect, border);
        DeleteObject(border);
        EndPaint(window, &paint);
        return 0;
    }
    case WM_SIZE:
        layout_controls(LOWORD(lparam), HIWORD(lparam));
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = 840;
        limits->ptMinTrackSize.y = 680;
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case ID_RECORD_BUTTON:
            if (app.processing) return 0;
            if (app.recorder.is_recording()) finish_recording(window);
            else begin_recording(window);
            return 0;
        case ID_PLAY_BUTTON:
            if (!app.output_file.empty()) {
                const uint64_t generation = ++playback_generation;
                const UINT output_device = selected_device(app.output_device_combo, app.output_device_ids);
                play_wav_async(window, app.output_file, output_device, generation);
            }
            return 0;
        case ID_OPEN_BUTTON:
            if (!app.output_file.empty()) ShellExecuteW(window, L"open", app.output_file.parent_path().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        case ID_SAVE_BUTTON:
            save_output_copy(window);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wparam == RECORD_TIMER_ID && app.recorder.is_recording()) {
            ++app.seconds;
            SetWindowTextW(app.record_time, format_duration(app.seconds).c_str());
            if (app.seconds >= MAX_RECORDING_SECONDS) finish_recording(window);
        }
        return 0;
    case WM_APP_AUDIO_LEVEL:
        app.audio_level = static_cast<int>(wparam);
        SetWindowLongPtrW(app.waveform, GWLP_USERDATA, app.audio_level);
        InvalidateRect(app.waveform, nullptr, FALSE);
        return 0;
    case WM_APP_PROCESS_STATUS: {
        auto* update = reinterpret_cast<ProcessStatus*>(lparam);
        if (update) {
            SetWindowTextW(app.result_detail, update->text.c_str());
            SendMessageW(app.progress, PBM_SETPOS, update->progress, 0);
            delete update;
        }
        return 0;
    }
    case WM_APP_PROCESS_FINISHED: {
        auto* result = reinterpret_cast<ProcessFinished*>(lparam);
        app.processing = false;
        EnableWindow(app.record_button, TRUE);
        EnableWindow(app.mode_combo, TRUE);
        EnableWindow(app.input_device_combo, TRUE);
        EnableWindow(app.output_device_combo, TRUE);
        ShowWindow(app.progress, SW_HIDE);
        SetWindowLongPtrW(app.waveform, GWLP_USERDATA, 0);
        InvalidateRect(app.waveform, nullptr, TRUE);
        if (result && result->success) {
            SendMessageW(app.progress, PBM_SETPOS, 100, 0);
            SetWindowTextW(app.result_title, L"处理完成");
            SetWindowTextW(app.result_detail, L"可以播放、打开文件夹或另存音频。\n");
            SetWindowTextW(app.output_path, result->output_path.c_str());
            app.output_file = result->output_path;
            show_result_buttons(true);
        } else {
            SetWindowTextW(app.result_title, L"未生成音频");
            SetWindowTextW(app.result_detail, result ? result->message.c_str() : L"处理失败。请重试。");
            show_result_buttons(false);
        }
        if (!app.input_path.empty()) {
            std::error_code error;
            fs::remove(app.input_path, error);
        }
        delete result;
        return 0;
    }
    case WM_APP_PLAYBACK_ERROR: {
        auto* error = reinterpret_cast<std::wstring*>(lparam);
        if (error) {
            MessageBoxW(window, error->c_str(), L"播放失败", MB_OK | MB_ICONERROR);
            delete error;
        }
        return 0;
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
        if (item && item->CtlID == ID_RECORD_BUTTON) {
            const bool recording = app.recorder.is_recording();
            const bool disabled = (item->itemState & ODS_DISABLED) != 0;
            HBRUSH brush = CreateSolidBrush(disabled ? RGB(168, 177, 172) : recording ? COLOR_DANGER : COLOR_PRIMARY);
            HPEN pen = CreatePen(PS_SOLID, 1, disabled ? RGB(168, 177, 172) : recording ? COLOR_DANGER : COLOR_PRIMARY);
            HGDIOBJ old_brush = SelectObject(item->hDC, brush);
            HGDIOBJ old_pen = SelectObject(item->hDC, pen);
            Ellipse(item->hDC, item->rcItem.left + 2, item->rcItem.top + 2, item->rcItem.right - 2, item->rcItem.bottom - 2);
            SelectObject(item->hDC, old_brush);
            SelectObject(item->hDC, old_pen);
            DeleteObject(brush);
            DeleteObject(pen);

            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, RGB(255, 255, 255));
            HFONT previous_font = reinterpret_cast<HFONT>(SelectObject(item->hDC, app.body_font));
            std::wstring label = recording ? L"停止" : L"录音";
            DrawTextW(item->hDC, label.data(), static_cast<int>(label.size()), &item->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(item->hDC, previous_font);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, COLOR_TEXT);
        SetBkColor(dc, COLOR_SURFACE);
        return reinterpret_cast<LRESULT>(app.surface_brush);
    }
    case WM_CLOSE:
        if (app.processing) {
            MessageBoxW(window, L"语音仍在处理中，请等待完成后关闭。", L"voice-detoxification", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
        if (app.recorder.is_recording()) {
            const int choice = MessageBoxW(window, L"正在录音，确定要退出吗？", L"voice-detoxification", MB_YESNO | MB_ICONQUESTION);
            if (choice != IDYES) return 0;
            std::wstring ignored;
            wchar_t temp_directory[MAX_PATH]{};
            GetTempPathW(MAX_PATH, temp_directory);
            const fs::path discard_path = fs::path(temp_directory) / L"clearvoice_discard.wav";
            app.recorder.stop_and_save(discard_path, ignored);
            std::error_code remove_error;
            fs::remove(discard_path, remove_error);
        }
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        ++playback_generation;
        DeleteObject(app.title_font);
        DeleteObject(app.body_font);
        DeleteObject(app.small_font);
        DeleteObject(app.window_brush);
        DeleteObject(app.surface_brush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

} // namespace

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int show_command) {
    INITCOMMONCONTROLSEX controls{sizeof(INITCOMMONCONTROLSEX), ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);

    WNDCLASSEXW waveform_class{};
    waveform_class.cbSize = sizeof(WNDCLASSEXW);
    waveform_class.hInstance = instance;
    waveform_class.lpfnWndProc = waveform_proc;
    waveform_class.lpszClassName = L"ClearVoiceWaveform";
    waveform_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassExW(&waveform_class);

    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(WNDCLASSEXW);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = window_proc;
    window_class.lpszClassName = L"ClearVoiceDesktop";
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    if (!RegisterClassExW(&window_class)) return 1;

    HWND window = CreateWindowExW(
        0, window_class.lpszClassName, L"语音去毒",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 720,
        nullptr, nullptr, instance, nullptr);
    if (!window) return 1;

    ShowWindow(window, show_command);
    UpdateWindow(window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
