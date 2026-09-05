// Offline check of the demo's onset detector (the TS port of flux.h) against a
// real capture. Prints one onset per line in ms, in picking_test's format, so
// the same scoring scripts apply and the two detectors can be diffed:
//
//   node tests/onset_eval.ts <capture.wav> [minGapMs=114] [sens=0.5]
//   (Node ≥ 23.6 runs TypeScript directly; the detector has no DOM deps.)
//
// Fidelity target: identical onset lists to
//   picking_test.exe <capture.wav> <sens> <minGapMs> <bpm> <target>
// up to float32-vs-double rounding at threshold boundaries.
import { readFileSync } from "node:fs"
import { FluxOnsetDetector } from "../src/engine/demo/onset-detector.ts"

function readWav(path: string): { x: Float32Array; sr: number } {
  const b = readFileSync(path)
  if (b.toString("ascii", 0, 4) !== "RIFF") throw new Error("not a RIFF wav")
  let p = 12
  let fmt = 3
  let ch = 1
  let sr = 48000
  let bits = 32
  let data: Buffer | null = null
  while (p + 8 <= b.length) {
    const id = b.toString("ascii", p, p + 4)
    const n = b.readUInt32LE(p + 4)
    const body = b.subarray(p + 8, p + 8 + n)
    if (id === "fmt ") {
      fmt = body.readUInt16LE(0)
      ch = body.readUInt16LE(2)
      sr = body.readUInt32LE(4)
      bits = body.readUInt16LE(14)
    } else if (id === "data") data = body
    p += 8 + n + (n & 1)
  }
  if (!data) throw new Error("no data chunk")
  const frames = Math.floor(data.length / (bits / 8) / ch)
  const x = new Float32Array(frames)
  for (let i = 0; i < frames; i++) {
    const off = i * ch * (bits / 8)
    if (fmt === 3 && bits === 32) x[i] = data.readFloatLE(off)
    else if (fmt === 1 && bits === 16) x[i] = data.readInt16LE(off) / 32768
    else if (fmt === 1 && bits === 32) x[i] = data.readInt32LE(off) / 2 ** 31
    else throw new Error(`unsupported wav fmt=${fmt} bits=${bits}`)
  }
  return { x, sr }
}

const [wav, gapArg, sensArg] = process.argv.slice(2)
if (!wav) {
  console.error("usage: node tests/onset_eval.ts <capture.wav> [minGapMs] [sens]")
  process.exit(2)
}
const { x, sr } = readWav(wav)
const det = new FluxOnsetDetector(sr)
det.setSensitivity(sensArg ? Number(sensArg) : 0.5)
det.setMinGap((gapArg ? Number(gapArg) : 114) / 1000)
const out: number[] = []
// Feed in 128-sample blocks like the worklet does.
for (let i = 0; i < x.length; i += 128) det.push(x.subarray(i, Math.min(x.length, i + 128)), out)
det.flush(out)
for (let i = 0; i < out.length; i++) {
  const ms = (1000 * out[i]) / sr
  const ioi = i === 0 ? 0 : (1000 * (out[i] - out[i - 1])) / sr
  console.log(`onset ${String(i + 1).padStart(3)}  t=${ms.toFixed(1).padStart(8)} ms  ioi=${ioi.toFixed(1).padStart(7)} ms`)
}
console.log(`total onsets: ${out.length}`)
