// Pairing (P4f): the local UI (helper-served page, localhost dev) is trusted
// automatically; any other origin — the future hosted webamp.com — must prove
// user consent once by entering a pairing code the helper prints on THIS
// machine. Paired origins persist (webamp-paired.json) so it's a one-time step.
//
// The code is the secret: only someone at the local console/log can read it, so
// a drive-by website can open the loopback socket but can't get past pairing.
// Wrong attempts are capped per connection by the caller.
#pragma once

#include <fstream>
#include <mutex>
#include <random>
#include <set>
#include <string>

#include "json.hpp"
#include "engine.h"  // webamp::lower, fs

namespace webamp {

// Empty origin = native tools / same-machine scripts. localhost / 127.0.0.1 =
// the helper's own page and local dev. All trusted without pairing.
inline bool isLocalOrigin(const std::string& origin) {
    if (origin.empty()) return true;
    const std::string o = lower(origin);
    for (const char* prefix : {"http://localhost", "https://localhost", "http://127.0.0.1",
                               "https://127.0.0.1"})
        if (o.rfind(prefix, 0) == 0) return true;
    return false;
}

struct Pairing {
    fs::path file;                 // webamp-paired.json
    std::string code;              // 6 digits, generated once at startup
    std::set<std::string> paired;  // origins the user has approved
    std::mutex mx;

    void init(const fs::path& path) {
        file = path;
        code = generateCode();
        load();
    }

    // A trusted origin needs no pairing: local, or previously paired.
    bool trusted(const std::string& origin) {
        if (isLocalOrigin(origin)) return true;
        std::lock_guard<std::mutex> lk(mx);
        return paired.count(lower(origin)) > 0;
    }

    bool codeMatches(const std::string& attempt) const { return !attempt.empty() && attempt == code; }

    void approve(const std::string& origin) {
        std::lock_guard<std::mutex> lk(mx);
        paired.insert(lower(origin));
        save();
    }

private:
    static std::string generateCode() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<int> d(0, 999999);
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%06d", d(gen));
        return buf;
    }

    void load() {
        try {
            std::ifstream f(file);
            if (!f) return;
            json j;
            f >> j;
            if (j.is_array())
                for (const auto& o : j)
                    if (o.is_string()) paired.insert(lower(o.get<std::string>()));
        } catch (...) {}
    }

    // Caller holds mx.
    void save() {
        try {
            json j = json::array();
            for (const auto& o : paired) j.push_back(o);
            std::ofstream f(file);
            f << j.dump(2);
        } catch (...) {}
    }
};

}  // namespace webamp
