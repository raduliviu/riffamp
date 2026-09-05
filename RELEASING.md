# Releasing RiffAmp

How to cut a new release so downloads keep working with **zero website edits**.

## The one rule: stable asset names

The landing page links the downloads as:

- `https://github.com/raduliviu/riffamp/releases/latest/download/riffamp-setup.exe`
- `https://github.com/raduliviu/riffamp/releases/latest/download/riffamp.dmg`

`releases/latest/download/<name>` always resolves to the newest **non-prerelease**
release, but only if an asset with that **exact name** exists on it. So every
release must attach assets named exactly **`riffamp-setup.exe`** and
**`riffamp.dmg`** — no version in the filename. The build configs already produce
these names (`installer/riffamp.iss` → `OutputBaseFilename=riffamp-setup`;
`package_mac.sh` → `dist/riffamp.dmg`). Don't put the version back in the
filenames — the version lives in the git tag + `AppVersion` / `Info.plist`.

If those two names are attached and the release is marked "Latest", the site
needs no change.

## 1. Bump the version (both platforms, keep in sync)

- `installer/riffamp.iss` → `#define AppVersion "X.Y.Z"`
- `package_mac.sh` → `VERSION="X.Y.Z"`

`installer/riffamp.iss` is the single source of truth: CMake reads `AppVersion`
from it and compiles it into the helper (shown in-app and used by the update
check), so the build step below must **re-run CMake configure** — not just
`--build` — for a version bump to reach the binary (`cmake -S helper -B
helper/build` does this; the packaging commands below already reconfigure).

Commit + push the bump.

## 2. Build + package each platform

### Windows (on the PC)

```bat
git pull
:: If the web UI changed, rebuild the embedded single-file first (needs pnpm/Node):
::   pnpm --dir web build      (commit web\dist\index.html)
:: Otherwise the committed web\dist\index.html is used as-is.

"C:\Program Files\CMake\bin\cmake.exe" -S helper -B helper\build
"C:\Program Files\CMake\bin\cmake.exe" --build helper\build --config Release

:: Build the installer (Inno Setup 6):
ISCC.exe installer\riffamp.iss
:: -> dist\riffamp-setup.exe   (stable name)
```

### macOS (on the Mac)

```bash
./package_mac.sh
# -> dist/riffamp.dmg   (stable name; arm64, unsigned/ad-hoc)
```

## 3. Create the GitHub release with both assets

The two binaries come from two machines, so create the release on one and upload
the other's asset from the second machine.

> **Publish only once BOTH assets are attached.** Because the site follows
> `releases/latest/download/…`, the moment a release becomes "Latest" it must
> already have both `riffamp-setup.exe` and `riffamp.dmg`, or the missing
> platform's button 404s. So when the two builds land at different times, create
> the release as a **draft** (`--draft`), upload each asset as it's ready, and
> flip it live only when both are there (`gh release edit <tag> --draft=false`).
> A draft is not "Latest", so the current release keeps serving downloads
> until you publish.

On whichever machine has its binary first (e.g. Windows):

```bash
gh release create vX.Y.Z dist/riffamp-setup.exe \
  --repo raduliviu/riffamp \
  --title "RiffAmp vX.Y.Z (beta)" \
  --notes "What changed…"
```

Then, from the other machine (e.g. Mac):

```bash
gh release upload vX.Y.Z dist/riffamp.dmg --repo raduliviu/riffamp
```

`gh release create` marks the newest release as "Latest" automatically (unless
you pass `--prerelease`/`--draft`). Confirm both assets are attached:

```bash
gh release view vX.Y.Z --repo raduliviu/riffamp --json assets --jq '.assets[].name'
# expect exactly: riffamp-setup.exe  and  riffamp.dmg
```

That's it — the site's download buttons now serve the new version. No site edit,
no redeploy.

## Notes / caveats

- **Unsigned builds.** Windows shows "unknown publisher" (More info → Run
  anyway); macOS shows "unidentified developer" (System Settings → Privacy &
  Security → Open Anyway). The landing page already documents both. Removing
  these warnings needs code signing — Windows: Azure Trusted Signing / SignPath;
  macOS: Apple Developer Program ($99/yr) + notarization.
- **macOS is arm64-only** right now. For a universal build:
  `ARCHS="arm64;x86_64" ./package_mac.sh` (untested — all deps must build
  universal; verify before shipping).
- **The site is auto-deployed** from `master` (Cloudflare Workers git build), so
  a version bump commit redeploys the site on its own; the download links don't
  depend on that though — they follow the GitHub "Latest" release.
- If you ever need to fix asset names on an existing release:
  `gh release upload <tag> <file>` to add, `gh release delete-asset <tag> <name> -y`
  to remove.
