# RiffAmp — tester guide

Thanks for trying RiffAmp! It's a guitar amp you run on your PC and control
from your browser, built for **low latency** so it feels like a real amp.

## What you need

- **Windows 10 or 11**
- **An audio interface with your guitar plugged in** — this is the important
  one. RiffAmp is not a "play through your laptop mic" toy; it needs a real
  interface (Focusrite Scarlett, NI Komplete Audio, Behringer UMC, etc.).
- **Ideally an ASIO driver** for that interface (most come with one). Without
  ASIO it still runs, but with noticeably more latency.
- Headphones or monitors plugged into the interface.

## Installing

1. Run `riffamp-setup-0.2.0.exe`.
2. **Windows will warn you** — *"Windows protected your PC"* / *"unknown
   publisher."* This is expected: the app isn't code-signed yet (that costs
   money/time we're skipping for this test round). Click **More info → Run
   anyway**. It installs just for your user — no admin needed.
3. Launch **RiffAmp** from the Start menu or desktop icon. It runs quietly in
   the system tray (little amber note icon by the clock).

## Playing

1. Open **http://127.0.0.1:43718** in your browser (or right-click the tray
   icon → Open RiffAmp).
2. **The guitar input starts muted** on purpose (so you don't get blasted).
   Click **▶ ENABLE** in the bar at the bottom to turn it on.
3. If you hear nothing, go to **Settings** and pick your interface as the
   input/output device, and your guitar's input channel.
4. It comes loaded with a starter amp + cab, so you should get tone right
   away. Play with the amp knobs, pedals, presets, metronome, drum machine,
   and the picking trainer in the Practice tab.

## What I'd love feedback on

- **Latency** — does it feel tight enough to play to, or is there noticeable
  lag? (Settings shows your buffer size — 64/128/256; smaller = tighter but
  more likely to crackle.)
- **Crackles / dropouts / crashes** — anything that broke.
- **Did it find your interface** automatically, or did you have to set it in
  Settings?
- **The picking trainer** — does the note detection match what you actually
  played?
- Anything confusing, missing, or delightful.

Thank you! 🎸
