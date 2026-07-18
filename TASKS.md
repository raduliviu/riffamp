# webamp — task list

Guitar amp as a web app: browser page = the whole UI; tiny native helper = the
real-time audio engine (ASIO). Validated by P0 on 2026-07-18: native
WASAPI-exclusive round-trip **14.4 ms** @64 buffer vs **79.6 ms** in-browser
(full research: [`../amp-latency-research/REPORT.md`](../amp-latency-research/REPORT.md)).

Status legend: `[ ]` pending · `[~]` in progress · `[x]` done

## P1 — Native tone proof

- [x] **1. Install C++ toolchain** — VS Build Tools 2022 (17.14, MSVC 14.44) + CMake 4.4.0 via winget, 2026-07-18. *(Machine notes: neither is on the interactive PATH — use full paths `C:\Program Files\CMake\bin\cmake.exe`; interactive shell also resolves `python`/`py` incorrectly — use explicit paths / project venvs.)*
- [x] **2. P1a: C++ ASIO passthrough + RTL measurement** — DONE 2026-07-18. `helper/` builds (CMake + PortAudio master + ASIO SDK via FetchContent, VS generator). **MEASURED on "Komplete Audio ASIO Driver" @48 kHz: 7.62 ms RTL @64 buffer, 10.96 ms @128 — zero jitter across impulses. Target <12 ms BEATEN.** C++ WASAPI-exclusive = 13.6 ms (so Python overhead in P0 was only ~1 ms; ASIO driver removes the rest). Scoreboard: browser 79.6 → ASIO 7.62 ms.
- [x] **3. P1b: Amp tone engine (NAM + cab IR)** — DONE 2026-07-18. Built directly on NeuralAmpModelerCore v0.5.4 + FFTConvolver + dr_wav (`helper/src/amp.cpp`, `p1b bench|run`). SlimmableContainer models work (needed WHOLE_ARCHIVE — NAM's parsers self-register via static initializers). Bench @64: avg 8.2% of budget, 0/22500 blocks over, 12.2× realtime. **User played JCM800 Lead + Greenback IR live — confirmed working.** Deferred into P2: gate + tone-stack EQ stages (current chain: gain → NAM → IR → volume), runtime param/model/IR switching.

## P2 — Control plane

- [x] **4. P2a: WebSocket control server** — DONE 2026-07-18. `webamp-helper.exe --assets <dir>` (`helper/src/helper.cpp` + `dsp_extra.h`): full chain now gain → **gate** → NAM → **tone stack (bass/mid/treble)** → IR → volume, all params atomic + live; model/IR hot-swap via atomic pointer exchange while the stream runs; JSON protocol (hello/state/setParam/setModel/setIr/panic + meters ~15 Hz) over IXWebSocket on 127.0.0.1:43717; Origin allowlist verified (evil origin closed 1008, localhost served). Protocol regression test: `helper/tests/ws_test.py` — 15/15 PASS against the live engine. *Deferred: pairing codes + hosted-origin allowlist → P2c; model/IR upload via binary frames (currently assets-dir listing) → P2c; setDevice/setBufferSize (needs stream restart) → P3.*
- [x] **5. P2b: LNA permission checkpoint** — DONE 2026-07-18. Findings: (a) **Zen (user's daily browser, Firefox fork): no LNA prompt today** — `ws://127.0.0.1` from public HTTPS opens directly (Gecko loopback-WS gating is an open Mozilla bug; expect a prompt in a future release); (b) in-app Chromium 148 (Electron): LNA permissions queryable (`loopback-network` etc., "denied") but not enforced — WS opened anyway; (c) helper's Origin allowlist rejected the public origin with close 1008 in both — server-side defense carries the security regardless of browser behavior; (d) loopback mixed-content exemption confirmed (ws:// from https:// works). (e) **Consumer Chrome, observed by user Jul 18 2026: LNA permission chip appears** for `ws://127.0.0.1` from a public HTTPS origin; on deny/dismiss the handshake aborts browser-side (`ERR_CONNECTION_ABORTED`, close 1006 — never reaches the helper). Allow-path (chip → grant → open → helper origin-check) still worth one observation. P3's hosted-origin UX must explain the chip before connecting and show recovery guidance on denial (re-enable via the padlock/site-settings). Localhost page → helper → live engine state round trip verified (web/index.html).
- [x] **6. P2c: Minimal web UI** — DONE 2026-07-18. `web/index.html` (vanilla JS, no build step, served with any static server on localhost): rotary knobs (drag/wheel/double-click-reset) for gain/gate/bass/mid/treble/volume, amp-channel buttons, IR dropdown (72 cabs), live in/out meters, mute, auto-reconnect + "engine not found" card. Verified end-to-end in-browser: knob drag → engine param confirmed via independent client; model switch persists across page reload (server owns state). Fixed: param flush used requestAnimationFrame, which suspends in background/embedded tabs and silently dropped changes — now setTimeout(33 ms). Protocol suite re-run: 15/15. **MILESTONE HIT: knob on a web page changes the amp at ~7.6 ms.** *Deferred to P3: pairing/hosted-origin flow, model/IR upload from page, presets, metronome, mobile layout.*

## P3 — Productize *(split into real tasks when P2 lands; blocked by 3, 6)*

- [x] **P3a: Tray app** — DONE 2026-07-18. `webamp-helper.exe` is now a self-contained tray app: WIN32 subsystem (no console; logs to `webamp-helper.log` beside the exe), **serves its own UI** at `http://127.0.0.1:43718` (index.html embedded at build time via `cmake/embed_file.cmake`; WS stays on 43717), tray menu Open webamp / Toggle mute / Quit (graceful shutdown), assets default to `<exe>\assets` (`--assets` overrides), MessageBox on fatal errors. Verified: served page byte-identical to source, 404 on other paths, ws_test 15/15 against the tray build, file:// origin rejected. *Manual check pending: tray icon/menu UX (user).* Remaining tray polish → installer task: custom icon (currently stock), autostart option.
- [ ] Installer (Inno Setup) + signing decision (SmartScreen vs Azure Trusted Signing ~$10/mo vs SignPath OSS)
- [ ] Presets; integrate the existing metronome ([`../metronome/`](../metronome/))
- [ ] Real UI design
- [ ] macOS: likely **no helper needed** — in-browser WASM NAM engine (CoreAudio ≈15–25 ms); the web app picks its engine per platform
- [ ] Auto-update strategy

## Resolved de-risking (history)

- [x] Browser latency spike — 79.6 ms Chrome / 110.3 ms Firefox ([`../amp-latency-spike/HANDOFF.md`](../amp-latency-spike/HANDOFF.md))
- [x] Deep feasibility research, pure-browser = dead end on Windows ([`../amp-latency-research/REPORT.md`](../amp-latency-research/REPORT.md))
- [x] Hybrid architecture research (LNA rules, ASIO licensing, DSP licenses, prior art)
- [x] P0: native-path latency proof — WASAPI-exclusive 14.4 ms @64 ([`../amp-helper-p0/`](../amp-helper-p0/))
