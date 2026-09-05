// Windows shell: tray icon, single-instance mutex, log redirect, MessageBox
// fatals. Tray + icon code moved verbatim from the pre-refactor helper.cpp.
#include "platform.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <optional>
#include <string>
#include <thread>

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>

namespace webamp::platform {
namespace {

constexpr UINT kTrayMsg = WM_APP + 1;
constexpr UINT kCmdOpen = 1, kCmdMute = 2, kCmdQuit = 3;

LRESULT CALLBACK trayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kTrayMsg) {
        if (lp == WM_LBUTTONDBLCLK) ShellExecuteA(nullptr, "open", kUiUrl, nullptr, nullptr, SW_SHOWNORMAL);
        else if (lp == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuA(menu, MF_STRING, kCmdOpen, "Open RiffAmp");
            AppendMenuA(menu, MF_STRING, kCmdMute, "Toggle mute");
            AppendMenuA(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuA(menu, MF_STRING, kCmdQuit, "Quit");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);  // required or the menu won't dismiss
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
        }
        return 0;
    }
    if (msg == WM_COMMAND) {
        switch (LOWORD(wp)) {
            case kCmdOpen: ShellExecuteA(nullptr, "open", kUiUrl, nullptr, nullptr, SW_SHOWNORMAL); break;
            case kCmdMute: if (gToggleMute) gToggleMute(); break;
            case kCmdQuit: gRunning.store(false); PostQuitMessage(0); break;
        }
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// Renders "♪" into a 32x32 ARGB icon (UI-accent amber) — no .ico file needed.
HICON makeNoteIcon() {
    const int S = 32;
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = S;
    bi.bV5Height = -S;  // top-down
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_RGB;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS,
                                   &bits, nullptr, 0);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    HGDIOBJ oldBmp = SelectObject(dc, dib);

    RECT rc{0, 0, S, S};
    SetBkColor(dc, RGB(0, 0, 0));
    FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    HFONT font = CreateFontW(34, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             DEFAULT_PITCH, L"Segoe UI Symbol");
    HGDIOBJ oldFont = SelectObject(dc, font);
    SetTextColor(dc, RGB(255, 255, 255));
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, L"♪", 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    GdiFlush();

    // Luminance -> alpha, tinted to the UI accent (blue-500 #3b82f6), premultiplied.
    auto* px = static_cast<unsigned char*>(bits);
    for (int i = 0; i < S * S; ++i) {
        unsigned char* p = px + i * 4;  // BGRA
        const unsigned a = std::max({p[0], p[1], p[2]});
        p[0] = static_cast<unsigned char>(0xF6 * a / 255);  // B
        p[1] = static_cast<unsigned char>(0x82 * a / 255);  // G
        p[2] = static_cast<unsigned char>(0x3B * a / 255);  // R
        p[3] = static_cast<unsigned char>(a);
    }
    SelectObject(dc, oldFont);
    DeleteObject(font);
    SelectObject(dc, oldBmp);
    DeleteDC(dc);

    HBITMAP mask = CreateBitmap(S, S, 1, 1, nullptr);
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = dib;
    ii.hbmMask = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(dib);
    DeleteObject(mask);
    return icon ? icon : LoadIconA(nullptr, IDI_APPLICATION);
}

void trayThread() {
    WNDCLASSA wc{};
    wc.lpfnWndProc = trayWndProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "riffampTray";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("riffampTray", "", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                              wc.hInstance, nullptr);
    NOTIFYICONDATAA nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayMsg;
    nid.hIcon = makeNoteIcon();
    lstrcpynA(nid.szTip, "RiffAmp engine — double-click to open", sizeof(nid.szTip));
    Shell_NotifyIconA(NIM_ADD, &nid);

    MSG msg;
    while (GetMessageA(&msg, nullptr, 0, 0) > 0 && gRunning.load()) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

std::thread gTray;

}  // namespace

std::filesystem::path exeDir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return std::filesystem::path(buf).parent_path();
}

std::filesystem::path dataDir() {
    // The installer targets a user-writable dir, so config lives beside the exe.
    return exeDir();
}

void tickSleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void openUrl(const char* url) {
    ShellExecuteA(nullptr, "open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

std::optional<std::string> httpGet(const std::string& url, const std::string& userAgent) {
    // url/userAgent are ASCII (a hardcoded GitHub URL), so a widen-by-copy is safe.
    const std::wstring wurl(url.begin(), url.end());
    const std::wstring wua(userAgent.begin(), userAgent.end());

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 255;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2047;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) return std::nullopt;
    host[uc.dwHostNameLength] = 0;

    auto close = [](HINTERNET h) { if (h) WinHttpCloseHandle(h); };
    HINTERNET session = WinHttpOpen(wua.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return std::nullopt;
    WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);
    HINTERNET connect = WinHttpConnect(session, host, uc.nPort, 0);
    HINTERNET request =
        connect ? WinHttpOpenRequest(connect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                     WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0)
                : nullptr;
    std::optional<std::string> result;
    if (request) {
        WinHttpAddRequestHeaders(request, L"Accept: application/vnd.github+json\r\n",
                                 static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
        if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0,
                               0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD status = 0, len = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
            std::string body;
            DWORD avail = 0;
            do {
                if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), avail, &read)) break;
                chunk.resize(read);
                body += chunk;
            } while (avail > 0);
            if (status >= 200 && status < 300) result = std::move(body);
        }
    }
    close(request);
    close(connect);
    close(session);
    return result;
}

void fatalAlert(const std::string& msg) {
    MessageBoxA(nullptr, msg.c_str(), "RiffAmp engine", MB_ICONERROR | MB_OK);
}

bool initApp() {
    // Single instance: a second launch just opens the UI of the running one.
    // (Without this, Windows' SO_REUSEADDR port-sharing lets clones pile up.)
    CreateMutexA(nullptr, TRUE, "Local\\riffamp-helper-single-instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        openUrl(kUiUrl);
        return false;
    }

    // No console in WIN32 subsystem: log to a file next to the exe.
    const std::filesystem::path logPath = exeDir() / "riffamp-helper.log";
    freopen(logPath.string().c_str(), "w", stdout);
    freopen(logPath.string().c_str(), "a", stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    return true;
}

void startShell() { gTray = std::thread(trayThread); }

void stopShell() {
    if (gTray.joinable()) gTray.join();
}

}  // namespace webamp::platform
