// The helper's release (marketing) version — distinct from kVersion in
// engine.h, which is the WS *protocol* generation. RIFFAMP_VERSION is injected
// by CMake, read from installer/riffamp.iss (the single source of truth), so a
// release bump there flows into the binary on the next configure. The fallback
// marks an un-versioned local/dev build, which the updater treats as "never
// out of date" (see updates.h) so developers are not nagged.
#pragma once

#ifndef RIFFAMP_VERSION
#define RIFFAMP_VERSION "0.0.0-dev"
#endif

namespace webamp {
inline constexpr const char* kAppVersion = RIFFAMP_VERSION;
}  // namespace webamp
