# webamp

A guitar amp simulator you drive from your browser: plug your guitar into your
audio interface, open a local page, and play — with real-time (single-digit
millisecond) latency.

## Architecture

Browser audio capture on Windows is irreparably high-latency (~80 ms measured —
Chrome's `getUserMedia` path is hardcoded to shared-mode WASAPI, and the
low-latency-input tracking issue was closed Won't-Fix). So webamp is a
**hybrid**: the browser is only the UI, and a tiny native helper does all the
real-time audio.

- **`helper/`** — a native Windows tray app that owns the audio path:
  ASIO capture → noise gate → pedals → NAM amp model → tone stack → cabinet IR →
  pedals → output, plus a metronome, tuner, and drum machine mixed in. It serves
  the UI at `http://127.0.0.1:43718` and takes commands over a JSON WebSocket at
  `ws://127.0.0.1:43717`. C++ (PortAudio + NeuralAmpModelerCore + FFTConvolver +
  IXWebSocket). GPLv3 — the ASIO SDK rides Steinberg's 2025 GPLv3 relicense.
- **`web/`** — the entire user experience as one dependency-free
  `index.html` (embedded into the helper at build time): amp knobs, cabinet/model
  pickers, pedalboard, presets, metronome, tuner, and the drum machine.
- **`docs/`** — product and market-analysis notes.

On macOS the page can instead run its amp engine fully in-browser (WASM NAM over
CoreAudio, ≈15–25 ms); the native helper is the Windows answer.

## Why a native helper (the latency story)

The whole project exists because pure-browser was measured to be a dead end on
Windows, and the native path was measured to work — on the target hardware
(NI Komplete Audio 1, 48 kHz):

| Path | Round-trip latency |
| --- | --- |
| In-browser (Chrome, `getUserMedia`) | 79.6 ms |
| Native, WASAPI shared | 42 ms |
| Native, WASAPI exclusive | 14.4 ms |
| **Native, ASIO @64-sample buffer** | **7.6 ms** |

The helper ships defaulting to a 128-sample buffer (~11 ms, xrun-safe under CPU
load); 64 and 256 are selectable.

## What works today

- **Amp engine** — NAM model + cabinet IR convolution, input gain, noise gate,
  3-band tone stack, hot-swappable model/IR while the stream runs.
- **Pedalboard** — 5 pedals (compressor, overdrive, chorus, delay, reverb), each
  placeable pre/post-amp, reorderable, bypassable; real-time-safe.
- **Presets** — save/recall named rigs (model + IR + knobs + full pedal chain).
- **Metronome** — sample-accurate click in the audio callback, tap tempo, accents,
  visual beat dots.
- **Tuner** — YIN pitch detection with a cents needle.
- **Drum machine** — step sequencer with 5 synthesized voices
  (kick/snare/crash/hihat/ride), selectable time signature (3/4 or 4/4), 1–4 bars,
  and resolution (quarter → 32nd). Patterns persist, and a **named groove library**
  lets you save and switch between beats.
- **Device & I/O** — ASIO/WASAPI/WDM-KS device + input-channel + buffer selection,
  persisted across restarts.
- **Packaging** — self-contained tray app + per-user Inno Setup installer.

See [TASKS.md](TASKS.md) for the full roadmap and history.

## Build & run

Requires Windows 10+, an ASIO-capable audio interface, CMake, and the
Visual Studio 2022 Build Tools (MSVC). From the repo root:

```bash
cmake -S helper -B helper/build
cmake --build helper/build --config Release
```

Run `helper/build/Release/webamp-helper.exe` and open
<http://127.0.0.1:43718>. Amp models (`.nam`) and cabinet impulse responses
(`.wav`) are loaded from an `assets/` folder next to the exe (override with
`--assets <dir>`).

To build the installer, compile with Inno Setup 6:
`ISCC.exe installer/webamp.iss` → `dist/webamp-setup-*.exe`.

## License

GPLv3. See the helper source headers; the ASIO SDK dependency requires GPLv3
distribution.
