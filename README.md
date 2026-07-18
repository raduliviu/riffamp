# webamp

A guitar amp simulator you open in a browser: plug your guitar into your audio
interface, navigate to a URL, play.

Because browser audio capture on Windows is irreparably high-latency
(~80 ms measured; [research report](../amp-latency-research/REPORT.md)), the
architecture is a **hybrid**:

- **`web/`** — the entire user experience: amp UI, knobs, NAM model browser,
  presets, metronome. A plain HTTPS web page.
- **`helper/`** — a tiny native Windows tray app doing all real-time audio
  (ASIO capture → NAM amp DSP → cab IR → output, target <12 ms round-trip),
  remote-controlled by the page over `ws://127.0.0.1`. GPLv3 (ASIO SDK rides
  Steinberg's 2025 GPLv3 relicense).
- **`docs/`** — protocol and design notes.

On macOS the page may run its amp engine fully in-browser (WASM NAM,
CoreAudio ≈15–25 ms) — the helper is the Windows answer.

## Status

Feasibility fully de-risked (July 2026): native path measured at **14.4 ms**
round-trip on the target hardware (NI Komplete Audio 1) before ASIO, vs
**79.6 ms** in-browser. See [TASKS.md](TASKS.md) for the roadmap — currently
at P1 (toolchain + C++ ASIO passthrough).

## Prior work (sibling folders)

- [`../amp-latency-spike/`](../amp-latency-spike/) — browser round-trip measurement tool + findings
- [`../amp-latency-research/`](../amp-latency-research/) — the full cited feasibility report
- [`../amp-helper-p0/`](../amp-helper-p0/) — P0 native latency proof (Python/PortAudio)
- [`../metronome/`](../metronome/) — the metronome web app (to be integrated in P3)
