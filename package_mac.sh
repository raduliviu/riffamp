#!/usr/bin/env bash
# Build the macOS RiffAmp helper and package it as an (unsigned) .app in a .dmg.
#
# BETA / unsigned: this is NOT notarized, so macOS Gatekeeper will warn about an
# unidentified developer — testers open it via System Settings → Privacy &
# Security → "Open Anyway" (once). Notarization (Apple Developer Program, $99/yr)
# is a later step that slots into this same script.
#
# Output: dist/riffamp-<version>.dmg  (drag RiffAmp.app to Applications)
set -euo pipefail

VERSION="0.2.1"  # keep in step with installer/riffamp.iss (AppVersion)
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

# --- toolchain: recent Node for the web build; libc++ workaround for C++ ---
if [ -x "$HOME/.nvm/versions/node/v24.15.0/bin/node" ]; then
  export PATH="$HOME/.nvm/versions/node/v24.15.0/bin:$PATH"
fi
export CXXFLAGS="-cxx-isystem $(xcrun --show-sdk-path)/usr/include/c++/v1 ${CXXFLAGS:-}"

# arm64 by default; set ARCHS="arm64;x86_64" for a universal build (needs all
# deps to build universal — verify before relying on it).
ARCHS="${ARCHS:-$(uname -m)}"

echo "==> Building embedded web UI"
pnpm --dir web build

echo "==> Configuring + building helper ($ARCHS)"
cmake -S helper -B helper/build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="$ARCHS" >/dev/null
cmake --build helper/build --target riffamp-helper -j"$(sysctl -n hw.ncpu)"
BIN="helper/build/riffamp-helper"
test -f "$BIN" || { echo "helper binary missing" >&2; exit 1; }

# --- assemble the .app bundle ---
APP="dist/RiffAmp.app"
echo "==> Assembling $APP"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS/assets" "$APP/Contents/Resources"

cp "$BIN" "$APP/Contents/MacOS/riffamp-helper"
# Starter pack (found by default at exeDir/assets; the scanner ignores non-nam/wav).
cp helper/starter/* "$APP/Contents/MacOS/assets/"
sed "s/__VERSION__/$VERSION/g" installer/mac/Info.plist > "$APP/Contents/Info.plist"

# --- icon: riffamp.ico -> riffamp.icns (best-effort) ---
if command -v iconutil >/dev/null && sips -s format png helper/resources/riffamp.ico \
      --out /tmp/riffamp-icon.png >/dev/null 2>&1; then
  ICONSET=/tmp/riffamp.iconset
  rm -rf "$ICONSET"; mkdir -p "$ICONSET"
  for s in 16 32 64 128 256 512; do
    sips -z "$s" "$s" /tmp/riffamp-icon.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null 2>&1 || true
    d=$((s*2))
    sips -z "$d" "$d" /tmp/riffamp-icon.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null 2>&1 || true
  done
  iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/riffamp.icns" 2>/dev/null \
    && echo "    icon: riffamp.icns" || echo "    icon: skipped (iconutil failed)"
else
  echo "    icon: skipped (no converter) — app gets the generic icon"
fi

# --- ad-hoc sign so TCC remembers the mic grant across launches ---
# (Not a Developer ID signature; Gatekeeper still warns on first open.)
codesign --force --deep --sign - "$APP" 2>/dev/null \
  && echo "==> Ad-hoc signed" || echo "==> Ad-hoc sign skipped"

# --- build the .dmg (drag-to-Applications) ---
DMG="dist/riffamp-$VERSION.dmg"
echo "==> Building $DMG"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$DMG"
hdiutil create -volname "RiffAmp $VERSION" -srcfolder "$STAGE" \
  -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGE"

echo
echo "Done: $DMG"
echo "Test: open it, drag RiffAmp to Applications, launch it (first run:"
echo "System Settings → Privacy & Security → Open Anyway), then it opens the UI."
