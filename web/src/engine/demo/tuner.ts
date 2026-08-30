// Guitar tuner for the demo: autocorrelation pitch detection on the dry input,
// with a parabolic-interpolation refinement. Returns -1 when the signal is too
// quiet or too noisy to call a pitch. Standard 12-TET, A4 = 440 Hz.

const NOTE_NAMES = [
  "C",
  "C#",
  "D",
  "D#",
  "E",
  "F",
  "F#",
  "G",
  "G#",
  "A",
  "A#",
  "B",
]

export function detectPitch(buf: Float32Array, sampleRate: number): number {
  const n = buf.length

  // Gate on level — silence has no pitch.
  let rms = 0
  for (let i = 0; i < n; i++) rms += buf[i] * buf[i]
  rms = Math.sqrt(rms / n)
  if (rms < 0.008) return -1

  // Full autocorrelation up to the lowest note we care about (~55 Hz), then
  // the classic octave-safe pick: walk down to the first local minimum before
  // hunting the peak, so harmonics can't pull us onto a subharmonic lag.
  const maxLag = Math.min(n - 1, Math.floor(sampleRate / 55))
  const minLag = Math.floor(sampleRate / 1320) // up to ~E6
  const c = new Float32Array(new ArrayBuffer((maxLag + 1) * 4))
  for (let lag = 0; lag <= maxLag; lag++) {
    let s = 0
    for (let i = 0; i < n - lag; i++) s += buf[i] * buf[i + lag]
    c[lag] = s
  }
  if (c[0] <= 0) return -1

  let d = 0
  while (d < maxLag && c[d] > c[d + 1]) d++

  let best = -Infinity
  let pos = -1
  for (let i = Math.max(d, minLag); i <= maxLag; i++) {
    if (c[i] > best) {
      best = c[i]
      pos = i
    }
  }
  if (pos < 1) return -1
  if (best / c[0] < 0.3) return -1 // too little periodicity to call a pitch

  // Parabolic interpolation around the peak for sub-sample precision.
  const x0 = c[pos - 1]
  const x1 = c[pos]
  const x2 = pos + 1 <= maxLag ? c[pos + 1] : c[pos]
  const denom = 2 * (2 * x1 - x0 - x2)
  const shift = denom !== 0 ? (x2 - x0) / denom : 0
  return sampleRate / (pos + shift)
}

export function freqToNote(freq: number): { note: string; cents: number } {
  const midi = 69 + 12 * Math.log2(freq / 440)
  const rounded = Math.round(midi)
  const cents = Math.round((midi - rounded) * 100)
  const name = NOTE_NAMES[((rounded % 12) + 12) % 12]
  const octave = Math.floor(rounded / 12) - 1
  return { note: `${name}${octave}`, cents }
}
