// Update check (P6a, phase 1: notify only). Fetches the newest published
// release from the GitHub API and, if it is newer than this build, hands back
// the version, notes, and page URL for the UI to show a banner. It downloads
// and runs NOTHING — phase 1 is purely informational, so it needs no code
// signing or checksum verification (those gate the one-click install, phase 2).
//
// The endpoint is hardcoded to this repo and HTTPS-only: never a user- or
// network-supplied URL, so there is no request-forgery surface. `releases/
// latest` already excludes drafts and prereleases, which is exactly what a
// user should be offered.
//
// Pure parsing/compare here (portable, unit-tested in updates_test.cpp); the
// actual HTTPS GET is platform::httpGet (WinHTTP / NSURLSession).
#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "json.hpp"

namespace webamp {

using nlohmann::json;

// Where the detached check thread deposits its result. A process-lifetime
// singleton (not engine/stack state) so the thread is safe even if the helper
// tears down mid-request — it only ever touches this and its own atomics.
struct UpdateSlot {
    std::mutex mx;
    std::atomic<bool> ready{false};
    std::string version, url, notes;  // guarded by mx
};
inline UpdateSlot& updateSlot() {
    static UpdateSlot s;
    return s;
}

constexpr const char* kReleasesApi =
    "https://api.github.com/repos/raduliviu/riffamp/releases/latest";
// GitHub rejects API requests without a User-Agent; it need only be non-empty.
constexpr const char* kUpdateUserAgent = "RiffAmp-Helper";

struct UpdateInfo {
    std::string version;  // e.g. "0.2.4" (leading 'v' stripped)
    std::string url;      // release page (html_url)
    std::string notes;    // release body (markdown)
};

// Strip a leading 'v'/'V' and split "1.2.3" into numeric parts. A non-numeric
// component (e.g. a "-dev"/"-beta" suffix) stops parsing at that point.
inline std::vector<int> versionParts(const std::string& v) {
    std::vector<int> parts;
    size_t i = (!v.empty() && (v[0] == 'v' || v[0] == 'V')) ? 1 : 0;
    while (i < v.size()) {
        if (v[i] < '0' || v[i] > '9') break;
        int n = 0;
        while (i < v.size() && v[i] >= '0' && v[i] <= '9') n = n * 10 + (v[i++] - '0');
        parts.push_back(n);
        if (i < v.size() && v[i] == '.') ++i; else break;
    }
    return parts;
}

// Numeric, component-wise "is `latest` strictly newer than `current`?" — so
// 0.2.10 > 0.2.9 (a string compare gets this wrong). Missing trailing
// components count as 0 (0.3 == 0.3.0). A build with no numeric version
// (the "0.0.0-dev" fallback, parts {0,0,0}) is older than any real release,
// which is fine: dev builds are ours and skipped by the caller anyway.
inline bool isNewerVersion(const std::string& latest, const std::string& current) {
    const auto a = versionParts(latest);
    const auto b = versionParts(current);
    for (size_t i = 0; i < a.size() || i < b.size(); ++i) {
        const int ai = i < a.size() ? a[i] : 0;
        const int bi = i < b.size() ? b[i] : 0;
        if (ai != bi) return ai > bi;
    }
    return false;
}

// Parse a GitHub `releases/latest` JSON body; return the update only if it is
// newer than `currentVersion`. Any malformed/error response (rate-limit,
// offline stub, etc.) yields nullopt — the check simply does nothing.
inline std::optional<UpdateInfo> parseLatestRelease(const std::string& body,
                                                    const std::string& currentVersion) {
    json j = json::parse(body, nullptr, /*allow_exceptions=*/false);
    if (!j.is_object() || !j.contains("tag_name")) return std::nullopt;
    const std::string tag = j.value("tag_name", std::string());
    if (tag.empty() || !isNewerVersion(tag, currentVersion)) return std::nullopt;

    UpdateInfo u;
    const auto parts = versionParts(tag);
    for (size_t i = 0; i < parts.size(); ++i)
        u.version += (i ? "." : "") + std::to_string(parts[i]);
    if (u.version.empty()) u.version = tag;
    u.url = j.value("html_url", std::string());
    u.notes = j.value("body", std::string());
    return u;
}

}  // namespace webamp
