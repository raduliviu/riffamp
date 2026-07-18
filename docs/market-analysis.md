# webamp — Market Analysis & Product Strategy

**Date:** July 18, 2026. Sources: three parallel research sweeps (browser-based amps, native amp-sim market, practice-tool space), each with primary citations; see the session research or ask for the full agent reports.

## 1. The three markets webamp touches

**Browser amps** (TONE3000 Live Input, LA Studio, FUKKAUDIO text-to-tone, openDAW+TONE3000, AmpSim3, BandLab/Soundtrap guitar tracks): all free-entry, all built on getUserMedia + WASM/AudioWorklet, all stuck with browser round-trip latency (only Soundtrap and TONE3000 are honest about it; Soundtrap disables Windows monitoring entirely). **Nobody in this segment ships a tuner or a looper. Almost nobody ships a pedalboard** (only BandLab's chain editor and the academic WAM PedalBoard).

**Native amp sims** (TONEX, Neural DSP, BIAS FX 2, AmpliTube 5, Guitar Rig 7, Helix Native, free NAM stack): this is where user expectations are set. Post-NAM-A2, **sound quality is commoditized** (A2 won a 1,000-participant blind test vs TONEX V2 and Neural DSP V2 — and webamp runs A2). Differentiation is workflow: FX chains, presets/cloud sharing, capture pipelines. Realistic street price for a full product: $99–149 perpetual.

**Practice tools** (Spark ecosystem, Fender Studio, Moises, Yousician, JustinGuitar PA): the stickiness market. Spark is Positive Grid's best-selling product ever because it's a *companion*, not an amp. Evidence: friction kills practice (cutting ~20 s of setup moved players from 3 to 5+ sessions/week); play-along and drums-not-click are the fun multipliers; recording-for-self-review is universally endorsed and universally skipped due to friction.

## 2. Where webamp stands today

| Market table stakes (2026) | webamp status |
|---|---|
| NAM capture support / model library | ✅ native NAMCore v0.5.4 incl. A2/Slimmable; TONE3000's 275K library is our format |
| Noise gate, EQ | ✅ engine stages |
| **Full FX chain: comp, drives, chorus/phaser/trem, tap delay, reverb — reorderable** | ❌ **the gap** ("great amp, afterthought effects" is the market's #1 complaint about TONEX/NAM) |
| IR loader (third-party IRs) | ✅ (assets folder); ❌ upload from page; ❌ mic-position UX |
| Tuner | ✅ — and NO browser competitor has one |
| **Presets (save/recall), sharing** | ❌ (roadmap) |
| Standalone low-latency app | ✅ — uniquely: native latency *with* a web UI |
| Metronome | ✅ engine-clocked, full UX — rare in ALL segments |
| Recording | ❌ |

## 3. Catch-up opportunities (ranked)

1. **Pedalboard / FX chain** — the single biggest gap vs. any comparison. Minimum credible set: compressor, 1–2 drives (Tube-Screamer-style), chorus, tap-tempo delay, reverb — added to existing gate + tone stack, **reorderable pre/post amp**. This directly kills the "afterthought effects" complaint that dogs TONEX and vanilla NAM. Skip parallel paths (premium-only feature; BIAS/Helix territory). All classic DSP, well within our engine budget (currently at 8% CPU).
2. **Presets** — save/recall named rigs (model + IR + chain + knobs). Table stakes everywhere; the NAM community's #1 wishlist item. Later: shareable as URLs/files (web-native advantage).
3. **Model/IR upload from the page + TONE3000 browsing** — remove the assets-folder coupling; TONE3000 has a public API and free 275K library; a browse-and-load integration would give us TONEX-MAX-scale library access for free, in-app.
4. **One-click recording** (see also differentiators — riff history is the better version).
5. Cab/mic-position UX for the IR pack (we have 72 IRs whose names encode position — a visual picker is cheap and mimics AmpliTube VIR's appeal at 1% of the depth).

## 4. Differentiator opportunities (ranked by defensibility)

1. **"Native latency, web everything" — the structural moat.** Browser competitors physically cannot match 7.6 ms on Windows (Chromium capture path, researched to death in `../amp-latency-research/`); desktop competitors don't have a web UI. webamp is alone in this quadrant. Marketing line writes itself: *the honesty* (driver-reported + future measurement wizard) *is also a differentiator* — LA Studio fabricates latency claims; we measure.
2. **The practice-station wedge.** Tuner + metronome + amp on one instant page is already shipped and *no amp sim in any segment bundles this well*. Double down with the two white-space features nobody ships:
   - **Auto practice log** — the engine sees the audio stream; log playing time automatically, show an ambient heatmap/streak. Every standalone tracker dies from manual entry; we get it for free. Keep it gentle (no Yousician-style nagging).
   - **Riff history / retrospective capture** — rolling buffer + "save the last 30 s." Recording-for-review is proven pedagogy that everyone skips; retrospective capture exists only in DAWs. Near-free for us (audio already in hand).
3. **Phone as remote, zero pairing.** Every competitor's phone control is Bluetooth with a documented support-ticket tail (Spark's dual-BT confusion is notorious). For us it's: bind the helper to LAN, open a URL on the phone. No pairing, no app store, works for a teacher/second screen too. Cheap and directly attacks the incumbent's weakest UX.
4. **Looper with drum grooves** — bridges catch-up and differentiation: headline feature of Spark 2, absent from every browser amp and most desktop sims. Quantized (bar-snap) looping removes the timing barrier (Quantiloop's insight); pair with the metronome's drum-style evolution. Medium effort (loop buffer in engine + UI).
5. **Free & open (GPLv3)** in a market with pricing fatigue (per-artist plugin buying, IK's tier maze). Aligns us with the NAM/TONE3000 community ethos, which is where our users are.

## 5. Strategic take

**Don't fight TONEX/Neural/BIAS on rig depth** — parallel paths, artist packs, capture training are years of work and someone else's moat. **Win the daily-practice session.** The evidence says stickiness = (a) shortest path from "I have 15 minutes" to playing with a good tone — that's our architecture; (b) making solo play feel accompanied — metronome ✅, looper + grooves next, backing tracks later. Our structural advantages (native latency + web UI + audio stream in hand) make the practice-station features cheap for us and awkward for everyone else.

**Suggested sequencing** (feeds TASKS.md):
1. Pedalboard v1 (comp/drive/chorus/delay/reverb, reorderable) — credibility gap, well-understood DSP
2. Presets (with the chain) — table stakes, prerequisite for sharing
3. Riff history + auto practice log — cheap, unique, sticky
4. Model/IR upload + TONE3000 browse — library scale for free
5. Looper v1 (quantized, 60 s, click/groove) — the practice centerpiece
6. Phone-remote polish (LAN bind option + pairing security) — marketing gold, low effort
7. Hosted page, macOS WASM engine, measurement wizard — per existing roadmap

## 6. Notable market intel (for future reference)

- **TONE3000** is friendly territory, not a competitor: they host the models we play, their Live Input is latency-bound where we aren't, they have a public API, and they open-sourced their WASM engine (useful for our macOS path). Integration > competition.
- **FUKKAUDIO** (Mar 2026) got Guitar World/MusicRadar/KVR coverage — but the hook was the **AI text-to-tone gimmick** riding the AI news cycle, not the browser delivery (the browser made the story instantly demoable, which helped). Lesson: guitar press needs a *player-facing* hook; "native-latency web amp" is an HN/tech story. Our press-able hooks are the practice-station angles: riff history ("the amp that remembers what you played"), auto practice log, zero-pairing phone remote.
- **openDAW** ships TONE3000 as a native device — browser DAWs are becoming NAM hosts; our helper could someday serve them as a low-latency backend (speculative).
- **Neural PCOM / Helix hardware-preset portability** are the strongest lock-in moats in the native market — nothing for us to copy near-term, but preset portability *between webamp installs* via URL is our cheap analog.
