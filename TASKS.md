# webamp — task list

Guitar amp as a web app: browser page = the whole UI; tiny native helper = the
real-time audio engine (ASIO). Validated by P0 on 2026-07-18: native
WASAPI-exclusive round-trip **14.4 ms** @64 buffer vs **79.6 ms** in-browser
(full research: [`../amp-latency-research/REPORT.md`](../amp-latency-research/REPORT.md)).

Status legend: `[ ]` pending · `[~]` in progress · `[x]` done

## P1 — Native tone proof

- [ ] **1. Install C++ toolchain** — VS Build Tools 2022 (MSVC x64 + Windows SDK) + CMake + Ninja. Needs admin, ~2 GB. Verify `cl` and `cmake` from a fresh shell. *(Machine note: interactive shell resolves `python`/`py` incorrectly — always use explicit paths / project venvs in scripts.)*
- [ ] **2. P1a: C++ ASIO passthrough + RTL measurement** *(blocked by 1)* — Scaffold `helper/` (CMake, **GPLv3** — rides Steinberg's 2025 ASIO GPLv3 relicense). PortAudio with ASIO host API. Port the impulse RTL method from [`../amp-helper-p0/p0.py`](../amp-helper-p0/p0.py); measure "Komplete Audio ASIO" @ 64/128 buffers, 48 kHz, loopback cable. Expected ~6–9 ms (KA6 MkII same driver family: 4.3–5.6 ms, SOS-measured). Also re-measure WASAPI-exclusive in C++ to isolate Python-callback overhead from the 14.4 ms P0 figure. **Milestone: the definitive latency number.**
- [ ] **3. P1b: Amp tone engine (NAM + cab IR)** *(blocked by 2)* — Build-vs-fork decision first: [stompbox](https://github.com/mikeoliphant/stompbox) (GPL-3, headless pedalboard w/ remote protocol) vs building on [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT) + [FFTConvolver](https://github.com/HiFi-LoFi/FFTConvolver) (MIT), optionally via [NeuralAudio](https://github.com/mikeoliphant/NeuralAudio); [NeuralRack](https://github.com/brummer10/NeuralRack) (BSD-3) is the PortAudio+NAM reference to crib from. Chain: gain → gate → NAM → tone stack → cab IR → volume, hardcoded files first. Verify: guitar playable, no xruns @64–128, CPU measured. **Milestone: real amp tone at <12 ms.**

## P2 — Control plane

- [ ] **4. P2a: WebSocket control server** *(blocked by 2)* — 127.0.0.1-only bind, fixed port + fallback range. JSON protocol: hello/version, listDevices, setDevice/setBufferSize, setParam (lock-free FIFO to audio thread), loadModel/loadIR as binary frames, meters ~15 Hz, panic. Security (Zoom-2019 lessons): Origin allowlist, 6-digit first-connect pairing, no GET side effects, helper stays fully offline.
- [ ] **5. P2b: LNA permission checkpoint** *(blocked by 4)* — From a real HTTPS origin, verify Chrome's Local Network Access prompt flow to `ws://127.0.0.1` (one-time per-origin since Chrome 142/147), `navigator.permissions.query({name:'loopback-network'})`, denial handling. 30-minute check BEFORE building the UI around the connection. Sanity-check Firefox.
- [ ] **6. P2c: Minimal web UI** *(blocked by 4, 5)* — Plain page: helper detection + pairing UX (explains the LNA prompt), knobs → setParam, NAM/IR file pickers → binary frames, live meters, "download the engine" card when absent. **Milestone: turn a knob on a web page, hear the amp change, <12 ms.**

## P3 — Productize *(split into real tasks when P2 lands; blocked by 3, 6)*

- [ ] Tray app (icon, autostart option, clean uninstall)
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
