// macOS shell (Objective-C++). RiffAmp runs as a background agent (LSUIElement)
// with a menu-bar item — the mac equivalent of the Windows tray. CoreAudio does
// the low-latency work. There's no NSApplication run loop of our own: main()
// owns the thread and runs the meter loop, so we service the menu by pumping the
// run loop from tickSleep() on each ~40 ms tick (see helper.cpp).
#include "platform.h"

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#include <mach-o/dyld.h>

#import <Cocoa/Cocoa.h>

// Menu-action target. Kept alive for the process lifetime (assigned to a static
// below and never released — deliberate for a singleton with no teardown need).
@interface RiffAmpMenu : NSObject
@end
@implementation RiffAmpMenu
- (void)openUI:(id)sender {
    (void)sender;
    webamp::platform::openUrl(webamp::platform::kUiUrl);
}
- (void)quit:(id)sender {
    (void)sender;
    webamp::platform::gRunning.store(false);
    CFRunLoopStop(CFRunLoopGetCurrent());  // return from the current tickSleep now
}
@end

namespace webamp::platform {

static NSStatusItem* gStatusItem = nil;
static RiffAmpMenu* gMenuTarget = nil;

std::filesystem::path exeDir() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);  // query required size
    std::string buf(size, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return std::filesystem::current_path();
    std::error_code ec;
    const auto canon = std::filesystem::canonical(buf.c_str(), ec);  // resolve symlinks
    return (ec ? std::filesystem::path(buf.c_str()) : canon).parent_path();
}

std::filesystem::path dataDir() {
    // ~/Library/Application Support/RiffAmp — writable regardless of where the
    // .app lives (the bundle itself may be read-only, e.g. in /Applications).
    const char* home = std::getenv("HOME");
    if (!home || !*home) return exeDir();  // fallback for odd environments
    const std::filesystem::path dir =
        std::filesystem::path(home) / "Library" / "Application Support" / "RiffAmp";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return ec ? exeDir() : dir;
}

void openUrl(const char* url) {
    const std::string cmd = std::string("open '") + url + "'";
    std::system(cmd.c_str());
}

void fatalAlert(const std::string&) {
    // Console app: the caller already printed the message to stderr.
}

static bool isBundled() {
    return exeDir().string().find(".app/Contents/MacOS") != std::string::npos;
}

// Build the menu-bar item. Called early (from initApp) so the icon appears at
// launch, before the microphone gate — not after audio opens. Must run on the
// main thread with NSApp finished-launching, or the item won't render.
static void setupMenuBar() {
    NSApplication* app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyAccessory];

    gMenuTarget = [[RiffAmpMenu alloc] init];
    gStatusItem = [[[NSStatusBar systemStatusBar]
        statusItemWithLength:NSVariableStatusItemLength] retain];
    if (gStatusItem.button) {
        gStatusItem.button.title = @"🎵";  // matches the Windows tray icon
        gStatusItem.button.toolTip = @"RiffAmp engine";
    }

    NSMenu* menu = [[NSMenu alloc] init];
    NSMenuItem* open = [menu addItemWithTitle:@"Open RiffAmp"
                                       action:@selector(openUI:)
                                keyEquivalent:@""];
    open.target = gMenuTarget;
    [menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* quit = [menu addItemWithTitle:@"Quit RiffAmp"
                                       action:@selector(quit:)
                                keyEquivalent:@"q"];
    quit.target = gMenuTarget;
    gStatusItem.menu = menu;

    // Without [NSApp run] we must finish launching ourselves so the app
    // connects to the window server and the status item actually appears.
    [app finishLaunching];
}

bool initApp() {
    // Line-buffer stdout so logs (and the pairing code) appear promptly even
    // when redirected to a file; the default block buffering hides them until
    // the buffer fills or the process exits.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::signal(SIGINT, [](int) { gRunning.store(false); });
    std::signal(SIGTERM, [](int) { gRunning.store(false); });
    // Menu bar only when launched as a .app (a GUI session); dev/terminal runs
    // stay a plain console app quit with Ctrl+C. Created here (before the audio
    // mic gate) so the icon is visible immediately.
    if (isBundled()) setupMenuBar();
    return true;
}

// Advance the main loop's tick. In a .app we own the main thread (there's no
// [NSApp run]), so we drain and dispatch pending NSApp events for the interval
// — this is what keeps the menu-bar item drawn and its menu clickable. Non-app
// runs have no status item and just sleep.
void tickSleep(int ms) {
    if (!gStatusItem) {
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return;
    }
    @autoreleasepool {
        NSDate* until = [NSDate dateWithTimeIntervalSinceNow:ms / 1000.0];
        for (;;) {
            NSEvent* ev = [NSApp nextEventMatchingMask:NSEventMaskAny
                                             untilDate:until
                                                inMode:NSDefaultRunLoopMode
                                               dequeue:YES];
            if (!ev) break;  // timed out — the full interval elapsed
            [NSApp sendEvent:ev];
            if ([until timeIntervalSinceNow] <= 0) break;
        }
    }
}

void startShell() {
    // Menu bar is already up (initApp). Now that the servers are listening,
    // open the UI in the browser on launch.
    if (isBundled()) openUrl(kUiUrl);
}

void stopShell() {
    if (gStatusItem) {
        [[NSStatusBar systemStatusBar] removeStatusItem:gStatusItem];
        [gStatusItem release];
        gStatusItem = nil;
    }
}

}  // namespace webamp::platform
