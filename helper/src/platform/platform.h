// Platform shell: everything OS-specific the helper needs, behind six small
// functions. Windows gets the tray icon, single-instance mutex, log-file
// redirect, and MessageBox fatals; macOS is a plain console app (Ctrl+C to
// quit) — a menu-bar item can slot in later without touching the engine.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>

namespace webamp::platform {

inline constexpr int kUiPort = 43718;
inline constexpr const char* kUiUrl = "http://127.0.0.1:43718";

// Set false by the shell (tray Quit on Windows, SIGINT on mac) to end the
// main meter loop.
inline std::atomic<bool> gRunning{true};
// Set in main once the engine is up; called from the shell (tray menu).
inline std::function<void()> gToggleMute;

// Directory containing the running executable.
std::filesystem::path exeDir();

// Open a URL in the default browser.
void openUrl(const char* url);

// Surface a fatal error to the user (GUI box on Windows; stderr already has
// the message on both platforms, so mac needs nothing extra).
void fatalAlert(const std::string& msg);

// One-time process setup. Returns false if another instance is already
// running (Windows: the UI was opened for it, caller should exit 0).
// Windows also redirects stdout/stderr to a log file next to the exe here
// (no console in the WINDOWS subsystem); mac keeps console output and
// installs a SIGINT handler that clears gRunning.
bool initApp();

// Start/stop the shell UI (Windows: tray icon thread; mac: nothing).
void startShell();
void stopShell();

}  // namespace webamp::platform
