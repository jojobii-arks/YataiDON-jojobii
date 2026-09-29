#include "platform_windows.h"

#include <windows.h>
#include <dbghelp.h>
#include <portaudio.h>
#include <spdlog/spdlog.h>
#include <atomic>
#include <csignal>
#include <string>
#include <filesystem>
#include <cstdint>

namespace {

// dbghelp is not reentrant/thread-safe; serialize entry so two threads
// crashing concurrently don't call into it at the same time.
static std::atomic_flag g_dbghelp_lock = ATOMIC_FLAG_INIT;

static void win_write(const char* data, std::size_t len) {
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    DWORD written = 0;
    if (h != nullptr && h != INVALID_HANDLE_VALUE) WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr);
}
static void win_write(const char* msg) { win_write(msg, strlen(msg)); }

static void win_write_hex(std::uintptr_t value) {
    char buf[2 + sizeof(value) * 2];
    buf[0] = '0';
    buf[1] = 'x';
    for (std::size_t i = 0; i < sizeof(value) * 2; i++) {
        int nibble = (value >> (4 * (sizeof(value) * 2 - 1 - i))) & 0xF;
        buf[2 + i] = nibble < 10 ? char('0' + nibble) : char('a' + nibble - 10);
    }
    win_write(buf, sizeof(buf));
}

static void log_trace_from_context(CONTEXT* ctx) {
    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    while (g_dbghelp_lock.test_and_set(std::memory_order_acquire)) {}
    struct DbghelpLockGuard {
        ~DbghelpLockGuard() { g_dbghelp_lock.clear(std::memory_order_release); }
    } dbghelp_lock_guard;

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    const bool sym_initialized = SymInitialize(process, nullptr, TRUE);
    struct SymCleanupGuard {
        HANDLE process;
        bool active;
        ~SymCleanupGuard() { if (active) SymCleanup(process); }
    } sym_cleanup_guard{process, sym_initialized};
    if (!sym_initialized) {
        win_write("SymInitialize failed; stack trace may lack symbols\n");
    }

    CONTEXT ctx_copy = *ctx;
    STACKFRAME64 sf  = {};
    sf.AddrPC.Mode      = AddrModeFlat;
    sf.AddrStack.Mode   = AddrModeFlat;
    sf.AddrFrame.Mode   = AddrModeFlat;
#if defined(_M_X64)
    sf.AddrPC.Offset    = ctx_copy.Rip;
    sf.AddrStack.Offset = ctx_copy.Rsp;
    sf.AddrFrame.Offset = ctx_copy.Rbp;
    const DWORD machine_type = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
    sf.AddrPC.Offset    = ctx_copy.Pc;
    sf.AddrStack.Offset = ctx_copy.Sp;
    sf.AddrFrame.Offset = ctx_copy.Fp;
    const DWORD machine_type = IMAGE_FILE_MACHINE_ARM64;
#elif defined(_M_IX86)
    sf.AddrPC.Offset    = ctx_copy.Eip;
    sf.AddrStack.Offset = ctx_copy.Esp;
    sf.AddrFrame.Offset = ctx_copy.Ebp;
    const DWORD machine_type = IMAGE_FILE_MACHINE_I386;
#else
#error "Unsupported architecture for Windows stack walking"
#endif

    // We can't easily use cpptrace here without pulling it in, so we just use dbghelp directly
    // This is a simplified version - in practice you might want to use cpptrace
    try {
        while (StackWalk64(
            machine_type, process, thread, &sf, &ctx_copy,
            nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr
        )) {
            if (sf.AddrPC.Offset == 0) break;
            win_write("  0x");
            win_write_hex(sf.AddrPC.Offset);
            win_write("\n");
        }
    } catch (...) {
        win_write("(stack walk failed)\n");
        return;
    }
}

static LONG WINAPI crash_exception_filter(EXCEPTION_POINTERS* ep) {
    const char* exc_name = "Unknown exception";
    switch (ep->ExceptionRecord->ExceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:    exc_name = "Access violation"; break;
        case EXCEPTION_STACK_OVERFLOW:      exc_name = "Stack overflow"; break;
        case EXCEPTION_ILLEGAL_INSTRUCTION: exc_name = "Illegal instruction"; break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:  exc_name = "FP divide by zero"; break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:  exc_name = "Int divide by zero"; break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: exc_name = "Array bounds exceeded"; break;
    }
    win_write("Crash: ");
    win_write(exc_name);
    win_write(" (code ");
    win_write_hex(static_cast<std::uintptr_t>(ep->ExceptionRecord->ExceptionCode));
    win_write(")\n");
    log_trace_from_context(ep->ContextRecord);
    std::_Exit(1);
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

std::filesystem::path win32_path_from_utf8(const std::string& utf8_str) {
    if (utf8_str.empty()) return {};
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                utf8_str.c_str(), -1, nullptr, 0);
    if (n <= 0) {
        cp = CP_ACP;
        n = MultiByteToWideChar(CP_ACP, 0, utf8_str.c_str(), -1, nullptr, 0);
    }
    if (n <= 1) return {};
    std::wstring wide(static_cast<size_t>(n - 1), L'\0');
    if (MultiByteToWideChar(cp, 0, utf8_str.c_str(), -1, wide.data(), n) <= 0) return {};
    return std::filesystem::path(wide);
}

std::filesystem::path win32_path_from_encoded(const std::string& path_str, const std::string& encoding, const std::filesystem::path& base_path) {
    int codepage = (encoding.find("utf-8") != std::string::npos) ? 65001 : 932;
    int wlen = MultiByteToWideChar(codepage, 0, path_str.c_str(), -1, nullptr, 0);
    if (wlen <= 0) return base_path / path_str;
    std::wstring wide(wlen, 0);
    MultiByteToWideChar(codepage, 0, path_str.c_str(), -1, wide.data(), wlen);
    return base_path / wide;
}

std::filesystem::path win32_get_executable_dir() {
    wchar_t buffer[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) {
        spdlog::error("Failed to get executable path (error {}), keeping current working directory", GetLastError());
        return {};
    }
    buffer[n] = L'\0';
    std::filesystem::path exe_path(buffer);
    return exe_path.parent_path();
}

void win32_install_crash_handlers() {
    std::signal(SIGINT, [](int) { std::_Exit(0); });
    SetUnhandledExceptionFilter(crash_exception_filter);
}

bool win32_is_key_down_native(int raylib_key) {
    int vk_code = 0;

    // Handle letters (A-Z)
    if (raylib_key >= 65 && raylib_key <= 90) {
        vk_code = raylib_key;
    }
    // Handle numbers (0-9)
    else if (raylib_key >= 48 && raylib_key <= 57) {
        vk_code = raylib_key;
    }
    // Handle special keys
    else {
        switch (raylib_key) {
            case 32: vk_code = VK_SPACE; break;
            case 256: vk_code = VK_ESCAPE; break;
            case 257: vk_code = VK_RETURN; break;
            case 258: vk_code = VK_TAB; break;
            case 259: vk_code = VK_BACK; break;
            case 260: vk_code = VK_INSERT; break;
            case 261: vk_code = VK_DELETE; break;
            case 262: vk_code = VK_RIGHT; break;
            case 263: vk_code = VK_LEFT; break;
            case 264: vk_code = VK_DOWN; break;
            case 265: vk_code = VK_UP; break;
            case 266: vk_code = VK_PRIOR; break; // Page Up
            case 267: vk_code = VK_NEXT; break;  // Page Down
            case 268: vk_code = VK_HOME; break;
            case 269: vk_code = VK_END; break;
            case 280: vk_code = VK_CAPITAL; break;
            case 281: vk_code = VK_SCROLL; break;
            case 282: vk_code = VK_NUMLOCK; break;
            case 283: vk_code = VK_SNAPSHOT; break; // Print Screen
            case 284: vk_code = VK_PAUSE; break;
            case 290: vk_code = VK_F1; break;
            case 291: vk_code = VK_F2; break;
            case 292: vk_code = VK_F3; break;
            case 293: vk_code = VK_F4; break;
            case 294: vk_code = VK_F5; break;
            case 295: vk_code = VK_F6; break;
            case 296: vk_code = VK_F7; break;
            case 297: vk_code = VK_F8; break;
            case 298: vk_code = VK_F9; break;
            case 299: vk_code = VK_F10; break;
            case 300: vk_code = VK_F11; break;
            case 301: vk_code = VK_F12; break;
            case 340: vk_code = VK_LSHIFT; break;
            case 341: vk_code = VK_LCONTROL; break;
            case 342: vk_code = VK_LMENU; break; // Left Alt
            case 343: vk_code = VK_LWIN; break;
            case 344: vk_code = VK_RSHIFT; break;
            case 345: vk_code = VK_RCONTROL; break;
            case 346: vk_code = VK_RMENU; break; // Right Alt
            case 347: vk_code = VK_RWIN; break;
            case 348: vk_code = VK_APPS; break;   // KB menu
            // Punctuation
            case 39:  vk_code = VK_OEM_7; break;  // '
            case 44:  vk_code = VK_OEM_COMMA; break;
            case 45:  vk_code = VK_OEM_MINUS; break;
            case 46:  vk_code = VK_OEM_PERIOD; break;
            case 47:  vk_code = VK_OEM_2; break;  // /
            case 59:  vk_code = VK_OEM_1; break;  // ;
            case 61:  vk_code = VK_OEM_PLUS; break; // =
            case 91:  vk_code = VK_OEM_4; break;  // [
            case 92:  vk_code = VK_OEM_5; break;  // backslash
            case 93:  vk_code = VK_OEM_6; break;  // ]
            case 96:  vk_code = VK_OEM_3; break;  // `
            // Numpad
            case 320: vk_code = VK_NUMPAD0; break;
            case 321: vk_code = VK_NUMPAD1; break;
            case 322: vk_code = VK_NUMPAD2; break;
            case 323: vk_code = VK_NUMPAD3; break;
            case 324: vk_code = VK_NUMPAD4; break;
            case 325: vk_code = VK_NUMPAD5; break;
            case 326: vk_code = VK_NUMPAD6; break;
            case 327: vk_code = VK_NUMPAD7; break;
            case 328: vk_code = VK_NUMPAD8; break;
            case 329: vk_code = VK_NUMPAD9; break;
            case 330: vk_code = VK_DECIMAL; break;
            case 331: vk_code = VK_DIVIDE; break;
            case 332: vk_code = VK_MULTIPLY; break;
            case 333: vk_code = VK_SUBTRACT; break;
            case 334: vk_code = VK_ADD; break;
            case 335: vk_code = VK_RETURN; break; // numpad enter (Windows has no separate VK)
            default: return false;
        }
    }

    if (vk_code == 0) return false;

    // Note: Window focus check should be done by caller if needed
    return (GetAsyncKeyState(vk_code) & 0x8000) != 0;
}

std::string win32_path_to_string(const std::filesystem::path& path) {
    std::wstring wpath = path.wstring();
    int size = WideCharToMultiByte(932, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size > 0) {
        std::string result(size - 1, '\0');
        WideCharToMultiByte(932, 0, wpath.c_str(), -1, &result[0], size, nullptr, nullptr);
        return result;
    }
    return path.string(); // fallback
}

// PortAudio WDM-KS backend
static PaStream* g_pa_stream = nullptr;

int pa_stream_callback(const void* /*inputBuffer*/, void* outputBuffer,
                       unsigned long framesPerBuffer,
                       const PaStreamCallbackTimeInfo* /*timeInfo*/,
                       PaStreamCallbackFlags /*statusFlags*/, void* userData) {
    // This will be connected to the AudioEngine::mix function
    // For now, we just silence the buffer
    float* out = static_cast<float*>(outputBuffer);
    for (unsigned long i = 0; i < framesPerBuffer * 2; ++i) {
        out[i] = 0.0f;
    }
    return paContinue;
}

bool win32_init_portaudio_wdmks(double target_sample_rate, unsigned long buffer_size) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        spdlog::error("Failed to initialize PortAudio: {}", Pa_GetErrorText(err));
        return false;
    }

    PaHostApiIndex host_index = Pa_HostApiTypeIdToHostApiIndex(paWDMKS);
    if (host_index < 0) {
        spdlog::error("PortAudio WDM-KS host API not available");
        Pa_Terminate();
        return false;
    }

    const PaHostApiInfo* host_info = Pa_GetHostApiInfo(host_index);
    if (!host_info || host_info->defaultOutputDevice == paNoDevice) {
        spdlog::error("No output device found for PortAudio WDM-KS");
        Pa_Terminate();
        return false;
    }

    PaStreamParameters out_params{};
    out_params.device                    = host_info->defaultOutputDevice;
    out_params.channelCount              = 2;
    out_params.sampleFormat              = paFloat32;
    out_params.suggestedLatency          = Pa_GetDeviceInfo(out_params.device)->defaultLowOutputLatency;
    out_params.hostApiSpecificStreamInfo = nullptr;

    err = Pa_OpenStream(&g_pa_stream, nullptr, &out_params, target_sample_rate,
                         buffer_size, paNoFlag,
                         &pa_stream_callback, nullptr);
    if (err != paNoError) {
        spdlog::error("Failed to open WDM-KS stream: {}", Pa_GetErrorText(err));
        Pa_Terminate();
        return false;
    }

    err = Pa_StartStream(g_pa_stream);
    if (err != paNoError) {
        spdlog::error("Failed to start WDM-KS stream: {}", Pa_GetErrorText(err));
        Pa_CloseStream(g_pa_stream);
        g_pa_stream = nullptr;
        Pa_Terminate();
        return false;
    }

    const PaDeviceInfo* dev_info = Pa_GetDeviceInfo(out_params.device);
    spdlog::info("Audio Device initialized successfully");
    spdlog::info("    > Backend:       PortAudio | WDM-KS");
    spdlog::info("    > Device:        {}", dev_info ? dev_info->name : "unknown");
    spdlog::info("    > Format:        Float32");
    spdlog::info("    > Channels:      2");
    spdlog::info("    > Sample rate:   {} Hz", target_sample_rate);
    spdlog::info("    > Buffer size:   {} frames (requested)", buffer_size);
    return true;
}

bool win32_init_portaudio_mme(double target_sample_rate, unsigned long buffer_size) {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        spdlog::error("Failed to initialize PortAudio: {}", Pa_GetErrorText(err));
        return false;
    }

    PaHostApiIndex host_index = Pa_HostApiTypeIdToHostApiIndex(paMME);
    if (host_index < 0) {
        spdlog::error("PortAudio MME host API not available");
        Pa_Terminate();
        return false;
    }

    const PaHostApiInfo* host_info = Pa_GetHostApiInfo(host_index);
    if (!host_info || host_info->defaultOutputDevice == paNoDevice) {
        spdlog::error("No output device found for PortAudio MME");
        Pa_Terminate();
        return false;
    }

    PaStreamParameters out_params{};
    out_params.device                    = host_info->defaultOutputDevice;
    out_params.channelCount              = 2;
    out_params.sampleFormat              = paFloat32;
    out_params.suggestedLatency          = Pa_GetDeviceInfo(out_params.device)->defaultLowOutputLatency;
    out_params.hostApiSpecificStreamInfo = nullptr;

    err = Pa_OpenStream(&g_pa_stream, nullptr, &out_params, target_sample_rate,
                         buffer_size, paNoFlag,
                         &pa_stream_callback, nullptr);
    if (err != paNoError) {
        spdlog::error("Failed to open MME stream: {}", Pa_GetErrorText(err));
        Pa_Terminate();
        return false;
    }

    err = Pa_StartStream(g_pa_stream);
    if (err != paNoError) {
        spdlog::error("Failed to start MME stream: {}", Pa_GetErrorText(err));
        Pa_CloseStream(g_pa_stream);
        g_pa_stream = nullptr;
        Pa_Terminate();
        return false;
    }

    const PaDeviceInfo* dev_info = Pa_GetDeviceInfo(out_params.device);
    spdlog::info("Audio Device initialized successfully");
    spdlog::info("    > Backend:       PortAudio | MME");
    spdlog::info("    > Device:        {}", dev_info ? dev_info->name : "unknown");
    spdlog::info("    > Format:        Float32");
    spdlog::info("    > Channels:      2");
    spdlog::info("    > Sample rate:   {} Hz", target_sample_rate);
    spdlog::info("    > Buffer size:   {} frames (requested)", buffer_size);
    return true;
}

void win32_close_portaudio() {
    if (g_pa_stream != nullptr) {
        Pa_StopStream(g_pa_stream);
        Pa_CloseStream(g_pa_stream);
        g_pa_stream = nullptr;
        Pa_Terminate();
    }
}

PaStream* win32_get_pa_stream() {
    return g_pa_stream;
}

void win32_set_pa_stream_userdata(void* userdata) {
    // This is a workaround since the callback is static
    // The actual userdata is passed during Pa_OpenStream
}