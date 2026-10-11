#pragma once

#ifndef YATAIDON_PLATFORM_UNIX_H
#define YATAIDON_PLATFORM_UNIX_H

#include <filesystem>
#include <string>

// Unix/Linux-specific platform services.

// Gets the directory containing the current executable.
// Works on Linux (via /proc/self/exe) and other Unix systems.
std::filesystem::path unix_get_executable_dir();

// Initializes Unix-specific crash handlers (signal handlers with altstack for stack overflow protection).
void unix_install_crash_handlers();

void unix_close_window();

#endif // YATAIDON_PLATFORM_UNIX_H
