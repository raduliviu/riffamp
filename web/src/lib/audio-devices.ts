// ASIO is duplex — one driver serves both input and output — so a legal device
// pair is either "same ASIO device on both sides" or "neither side ASIO".
// When the user changes one side, coerce the other to keep the pair legal.
// Ported from the legacy applyDeviceSelection. On macOS (CoreAudio only) no
// device reports "ASIO", so every branch is skipped and the pair passes through.

import type { AudioState, DeviceInfo } from "@/engine/protocol"

export type ChangedSide = "in" | "out"

const apiOf = (devs: DeviceInfo[], i: number) =>
  devs.find((d) => d.index === i)?.api
const nameOf = (devs: DeviceInfo[], i: number) =>
  devs.find((d) => d.index === i)?.name
const byNameApi = (devs: DeviceInfo[], name: string | undefined, api: string) =>
  devs.find((d) => d.name === name && d.api === api)?.index ?? null
const firstNonAsio = (devs: DeviceInfo[]) =>
  devs.find((d) => d.api !== "ASIO")?.index ?? null

export function coerceDeviceSelection(
  audio: AudioState,
  changed: ChangedSide,
  inIdx: number,
  outIdx: number
): { input: number; output: number } {
  const inDevs = audio.inputDevices
  const outDevs = audio.outputDevices
  const inApi = apiOf(inDevs, inIdx)
  const outApi = apiOf(outDevs, outIdx)
  let m: number | null

  if (changed === "in" && inApi === "ASIO") {
    if ((m = byNameApi(outDevs, nameOf(inDevs, inIdx), "ASIO")) != null)
      outIdx = m
  } else if (changed === "out" && outApi === "ASIO") {
    if ((m = byNameApi(inDevs, nameOf(outDevs, outIdx), "ASIO")) != null)
      inIdx = m
  } else if (changed === "in" && outApi === "ASIO") {
    m = byNameApi(outDevs, nameOf(inDevs, inIdx), inApi ?? "")
    if (m == null) m = firstNonAsio(outDevs)
    if (m != null) outIdx = m
  } else if (changed === "out" && inApi === "ASIO") {
    m = byNameApi(inDevs, nameOf(outDevs, outIdx), outApi ?? "")
    if (m == null) m = firstNonAsio(inDevs)
    if (m != null) inIdx = m
  }
  return { input: inIdx, output: outIdx }
}
