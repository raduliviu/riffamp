// macOS shell: a plain console app. CoreAudio does the low-latency work (no
// ASIO story needed); Ctrl+C quits via a SIGINT handler that clears gRunning.
// Single-instance is enforced downstream by the control-port bind failing.
// A menu-bar item (NSStatusItem) can replace this later without engine changes.
#include "platform.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <mach-o/dyld.h>

namespace webamp::platform {

std::filesystem::path exeDir() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);  // query required size
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return std::filesystem::current_path();
    std::error_code ec;
    const auto canon = std::filesystem::canonical(buf.c_str(), ec);  // resolve symlinks
    return (ec ? std::filesystem::path(buf.c_str()) : canon).parent_path();
}

void openUrl(const char* url) {
    const std::string cmd = std::string("open '") + url + "'";
    std::system(cmd.c_str());
}

void fatalAlert(const std::string&) {
    // Console app: the caller already printed the message to stderr.
}

bool initApp() {
    std::signal(SIGINT, [](int) { gRunning.store(false); });
    std::signal(SIGTERM, [](int) { gRunning.store(false); });
    return true;
}

void startShell() {}
void stopShell() {}

}  // namespace webamp::platform
