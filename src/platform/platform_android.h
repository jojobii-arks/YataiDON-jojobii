#pragma once

#ifndef YATAIDON_PLATFORM_ANDROID_H
#define YATAIDON_PLATFORM_ANDROID_H

#include <filesystem>
#include <string>

// Android-specific platform services.

// Gets the directory for the app's working directory on Android.
// Typically /sdcard/YataiDON or similar.
std::filesystem::path android_get_working_directory();

// Initializes Android-specific crash handlers.
void android_install_crash_handlers();

// Android-specific network services.

// Checks for app updates and installs them if available.
// Runs asynchronously.
void android_check_and_install_update();

#endif // YATAIDON_PLATFORM_ANDROID_H
