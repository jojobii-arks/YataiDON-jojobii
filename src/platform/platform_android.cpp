#include "platform_android.h"

#include <spdlog/spdlog.h>
#include <csignal>
#include <filesystem>
#include <optional>
#include <unistd.h>

#include <cpr/cpr.h>

std::filesystem::path android_get_working_directory() {
    std::filesystem::path exe_dir("/sdcard/YataiDON");
    std::error_code ec;
    std::filesystem::create_directories(exe_dir, ec);
    std::filesystem::current_path(exe_dir, ec);
    if (ec) {
        spdlog::error("Failed to set working directory to {}: {}", exe_dir.string(), ec.message());
    } else {
        spdlog::info("Working directory set to: {}", exe_dir.string());
    }
    return exe_dir;
}

void android_install_crash_handlers() {
    // Android uses its own crash handling (tombstones, logcat)
    // We just set up basic signal handlers
    signal(SIGINT, [](int) { _exit(0); });
}

// Static storage for the async app-update checksum fetch.
static std::optional<cpr::AsyncResponse> g_pending_update_checksum;
static bool g_android_update_checked = false;

void android_check_and_install_update() {
    if (g_android_update_checked) return;
    g_android_update_checked = true;

    constexpr char kUpdateChecksumUrl[] =
        "https://github.com/yonokid/YataiDON/releases/latest/download/checksums-android.sha256";

    g_pending_update_checksum = cpr::GetAsync(cpr::Url{kUpdateChecksumUrl}, cpr::Timeout{5000});
}
