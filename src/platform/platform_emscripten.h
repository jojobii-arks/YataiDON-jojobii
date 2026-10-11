#pragma once

#include <filesystem>

// Emscripten/WebAssembly-specific platform services.

// Gets the working directory for Emscripten (uses virtual filesystem root).
std::filesystem::path emscripten_get_working_directory();

// Initializes Emscripten-specific crash handlers.
void emscripten_install_crash_handlers();

// Emscripten main loop.
// Runs the frame function using emscripten_set_main_loop.
// Returns -1 if not on Emscripten, otherwise doesn't return (blocks forever).
int emscripten_run_main_loop(void (*run_frame)());

// Emscripten-specific shutdown/cleanup.
void emscripten_shutdown();

// Checks if we're on Emscripten (for compile-time platform detection).
constexpr bool emscripten_is_emscripten_platform() { return true; }

// Emscripten filesystem sync (persist IndexedDB changes).
void emscripten_sync_filesystem();

