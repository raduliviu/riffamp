// Offline tests for the update-check parse/compare (engine/updates.h). The
// HTTPS GET itself (platform::httpGet) is verified by running the helper; this
// covers the logic most likely to have a bug: semver ordering and JSON parsing.
#include <cstdio>
#include <string>

#include "updates.h"

static int failures = 0;
static void check(const std::string& name, bool ok) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (!ok) ++failures;
}

int main() {
    using namespace webamp;

    // Numeric, not lexical, ordering.
    check("0.2.10 > 0.2.9", isNewerVersion("0.2.10", "0.2.9"));
    check("v0.2.4 > 0.2.3 (leading v)", isNewerVersion("v0.2.4", "0.2.3"));
    check("0.2.3 not newer than 0.2.3", !isNewerVersion("0.2.3", "0.2.3"));
    check("0.2.3 not newer than 0.2.4", !isNewerVersion("0.2.3", "0.2.4"));
    check("1.0.0 > 0.9.9", isNewerVersion("1.0.0", "0.9.9"));
    check("0.3 == 0.3.0 (missing components = 0)", !isNewerVersion("0.3", "0.3.0"));
    check("0.3.1 > 0.3", isNewerVersion("0.3.1", "0.3"));
    // A dev build is behind any real release.
    check("0.2.4 > 0.0.0-dev", isNewerVersion("0.2.4", "0.0.0-dev"));

    // Parse: newer release yields the info.
    {
        const std::string body =
            R"({"tag_name":"v0.2.4","html_url":"https://github.com/raduliviu/riffamp/releases/tag/v0.2.4","body":"Notes here"})";
        auto u = parseLatestRelease(body, "0.2.3");
        check("parse newer -> some", u.has_value());
        if (u) {
            check("parse version stripped to 0.2.4", u->version == "0.2.4");
            check("parse url carried", u->url.find("v0.2.4") != std::string::npos);
            check("parse notes carried", u->notes == "Notes here");
        }
    }
    // Parse: same/older release yields nothing.
    {
        const std::string body = R"({"tag_name":"v0.2.3","html_url":"x","body":"y"})";
        check("parse same -> none", !parseLatestRelease(body, "0.2.3").has_value());
    }
    // Parse: garbage/error response yields nothing (never throws).
    check("parse rate-limit json -> none",
          !parseLatestRelease(R"({"message":"API rate limit exceeded"})", "0.2.3").has_value());
    check("parse non-json -> none", !parseLatestRelease("<html>502</html>", "0.2.3").has_value());
    check("parse empty -> none", !parseLatestRelease("", "0.2.3").has_value());

    std::printf(failures ? "\n%d FAILURE(S)\n" : "\nALL PASS\n", failures);
    return failures ? 1 : 0;
}
