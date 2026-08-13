"""Protocol test for webamp-helper: hello, setParam, setModel/setIr, meters, errors."""
import json
import time

import websocket

FAILURES = []


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  [{detail}]" if detail else ""))
    if not cond:
        FAILURES.append(name)


ws = websocket.create_connection("ws://127.0.0.1:43717", timeout=10,
                                 origin="http://localhost:8090")


def rpc(msg, expect_type):
    ws.send(json.dumps(msg))
    while True:
        reply = json.loads(ws.recv())
        if reply.get("type") == "meters" and expect_type != "meters":
            continue  # meters interleave with replies
        return reply


# 1. hello -> state
state = rpc({"type": "hello"}, "state")
check("hello->state", state.get("type") == "state", f"model={state.get('model')}")
check("models listed", len(state.get("models", [])) == 3, str(state.get("models")))
check("irs listed", len(state.get("irs", [])) == 72)
check("engine info", state["engine"]["buffer"] in (64, 128, 256) and state["engine"]["sampleRate"] == 48000)
check("input muted at startup (safety gate)", state["params"]["mute"] is True)

# 2. setParam
state = rpc({"type": "setParam", "id": "bass", "value": 6.0}, "state")
check("setParam bass", abs(state["params"]["bass"] - 6.0) < 1e-6)
state = rpc({"type": "setParam", "id": "gainOut", "value": 0.7}, "state")
check("setParam gainOut", abs(state["params"]["gainOut"] - 0.7) < 1e-6)
state = rpc({"type": "setParam", "id": "gainOut", "value": 99.0}, "state")
check("param clamping", abs(state["params"]["gainOut"] - 4.0) < 1e-6, "99 -> 4.0 max")

# 3. model & IR switching (live swap while stream runs)
target_model = state["models"][0]
state = rpc({"type": "setModel", "name": target_model}, "state")
check("setModel", state["model"] == target_model, target_model)
target_ir = state["irs"][5]
state = rpc({"type": "setIr", "name": target_ir}, "state")
check("setIr", state["ir"] == target_ir, target_ir)

# 4. errors
err = rpc({"type": "setParam", "id": "nonsense", "value": 1}, "error")
check("unknown param -> error", err.get("type") == "error")
err = rpc({"type": "setModel", "name": "../../../etc/passwd"}, "error")
check("path traversal rejected", err.get("type") == "error")
err = rpc({"type": "bogus"}, "error")
check("unknown type -> error", err.get("type") == "error")

# 4b. tuner: toggles + streams analysis messages (freq -1 on silent input)
state = rpc({"type": "setParam", "id": "tunerOn", "value": 1}, "state")
check("tunerOn set", state["params"]["tunerOn"] is True)
got_tuner = None
ws.settimeout(3)
t0 = time.time()
while time.time() - t0 < 2.5:
    m = json.loads(ws.recv())
    if m.get("type") == "tuner":
        got_tuner = m
        break
check("tuner messages stream", got_tuner is not None,
      f"freq={got_tuner and got_tuner.get('freq')}")
state = rpc({"type": "setParam", "id": "tunerOn", "value": 0}, "state")
check("tunerOn cleared", state["params"]["tunerOn"] is False)

# 4c. pedalboard: state carries 5 pedals; setPedal toggles/params round-trip
state = rpc({"type": "hello"}, "state")
peds = {p["type"]: p for p in state.get("pedals", [])}
check("5 pedals in state", set(peds) == {"comp", "drive", "chorus", "delay", "reverb"}, str(list(peds)))
check("drive defaults pre-amp", peds.get("drive", {}).get("placement") == "pre")
check("reverb defaults post-amp", peds.get("reverb", {}).get("placement") == "post")
# All can be disabled (order-independent — the shared dev engine may carry state)
for _pt in ("comp", "drive", "chorus", "delay", "reverb"):
    state = rpc({"type": "setPedal", "pedal": _pt, "field": "enabled", "value": 0}, "state")
check("all pedals disable", all(not p["enabled"] for p in state["pedals"]))

state = rpc({"type": "setPedal", "pedal": "drive", "field": "enabled", "value": 1}, "state")
dr = next(p for p in state["pedals"] if p["type"] == "drive")
check("drive enabled via setPedal", dr["enabled"] is True)
state = rpc({"type": "setPedal", "pedal": "drive", "field": "drive", "value": 0.8}, "state")
dr = next(p for p in state["pedals"] if p["type"] == "drive")
check("drive param set", abs(dr["params"]["drive"] - 0.8) < 1e-6)
state = rpc({"type": "setPedal", "pedal": "reverb", "field": "placement", "value": 0}, "state")
rv = next(p for p in state["pedals"] if p["type"] == "reverb")
check("reverb moved pre-amp", rv["placement"] == "pre")
err = rpc({"type": "setPedal", "pedal": "drive", "field": "bogus", "value": 1}, "error")
check("unknown pedal field -> error", err.get("type") == "error")
err = rpc({"type": "setPedal", "pedal": "nope", "field": "enabled", "value": 1}, "error")
check("unknown pedal -> error", err.get("type") == "error")
# restore defaults for a clean slate
rpc({"type": "setPedal", "pedal": "drive", "field": "enabled", "value": 0}, "state")
rpc({"type": "setPedal", "pedal": "reverb", "field": "placement", "value": 1}, "state")

# 4d. audio I/O: device lists + input channel selection
state = rpc({"type": "hello"}, "state")
audio = state.get("audio", {})
check("audio state present", bool(audio.get("inputDevices")) and bool(audio.get("outputDevices")),
      f"{len(audio.get('inputDevices', []))} in / {len(audio.get('outputDevices', []))} out")
check("current input device set", audio.get("inputDevice", -1) >= 0)
check("inChannels reported", audio.get("inChannels", 0) >= 1)
state = rpc({"type": "setParam", "id": "inCh", "value": 1}, "state")
check("input channel set to 1", state["audio"]["inCh"] == 1)
state = rpc({"type": "setParam", "id": "inCh", "value": 2}, "state")
check("input channel set to 2", state["audio"]["inCh"] == 2)
# same-device reopen is a safe no-op (must not error or drop audio)
ci, co = audio["inputDevice"], audio["outputDevice"]
state = rpc({"type": "setAudioDevice", "input": ci, "output": co}, "state")
check("same-device reopen is a no-op", state["type"] == "state" and state["audio"]["inputDevice"] == ci)
# buffer size: reported, changeable (pending until restart), validated
cur_buf = audio["buffer"]
check("buffer reported", cur_buf in (64, 128, 256))
other = 256 if cur_buf != 256 else 128
state = rpc({"type": "setBuffer", "value": other}, "state")
check("setBuffer marks pending", state["audio"].get("pending", {}).get("buffer") == other)
state = rpc({"type": "setBuffer", "value": cur_buf}, "state")  # back to current clears pending
check("setBuffer to current clears pending", "buffer" not in state["audio"].get("pending", {}))
err = rpc({"type": "setBuffer", "value": 100}, "error")
check("invalid buffer -> error", err.get("type") == "error")

# 4e. presets: save current, mutate, load-restores, delete
state = rpc({"type": "setParam", "id": "treble", "value": 5.5}, "state")
state = rpc({"type": "savePreset", "name": "  ws-test-rig  "}, "state")
check("preset saved (name trimmed)", "ws-test-rig" in state.get("presets", []), str(state.get("presets")))
rpc({"type": "setParam", "id": "treble", "value": -5.0}, "state")  # change it
state = rpc({"type": "loadPreset", "name": "ws-test-rig"}, "state")
check("preset load restores param", abs(state["params"]["treble"] - 5.5) < 1e-4, str(state["params"]["treble"]))
err = rpc({"type": "loadPreset", "name": "does-not-exist"}, "error")
check("load unknown preset -> error", err.get("type") == "error")
err = rpc({"type": "savePreset", "name": "   "}, "error")
check("save blank name -> error", err.get("type") == "error")
state = rpc({"type": "deletePreset", "name": "ws-test-rig"}, "state")
check("preset deleted", "ws-test-rig" not in state.get("presets", []))
rpc({"type": "setParam", "id": "treble", "value": 0.0}, "state")  # restore neutral

# 4f. drum machine: 5 voices, selectable grid (time sig / bars / resolution)
state = rpc({"type": "setDrumGrid", "beatsPerBar": 4, "bars": 1, "subdiv": 4}, "state")
dm = state["drums"]
check("drums: 5 voices, 16 steps @ 4/4 1bar 16th", len(dm["pattern"]) == 5 and dm["stepCount"] == 16,
      f"steps={dm['stepCount']}")
state = rpc({"type": "setDrumGrid", "beatsPerBar": 4, "bars": 4, "subdiv": 8}, "state")
check("4 bars 4/4 32nd -> 128 steps", state["drums"]["stepCount"] == 128)
state = rpc({"type": "setDrumGrid", "beatsPerBar": 3, "bars": 2, "subdiv": 4}, "state")
check("3/4 2bar 16th -> 24 steps", state["drums"]["stepCount"] == 24)
state = rpc({"type": "setDrumCell", "voice": 0, "step": 4, "on": True}, "state")
check("drum cell set", state["drums"]["pattern"][0][4] == 1)
state = rpc({"type": "setDrumCell", "voice": 0, "step": 4, "on": False}, "state")
check("drum cell cleared", state["drums"]["pattern"][0][4] == 0)
rpc({"type": "setDrumCell", "voice": 0, "step": 0, "on": True}, "state")
state = rpc({"type": "setDrumGrid", "beatsPerBar": 4, "bars": 1, "subdiv": 4}, "state")
check("changing grid clears pattern", not any(any(row) for row in state["drums"]["pattern"]))
state = rpc({"type": "setDrumGrid", "beatsPerBar": 4, "bars": 1, "subdiv": 5}, "state")
check("bad subdiv clamps", state["drums"]["subdiv"] == 4)
state = rpc({"type": "setParam", "id": "drumVol", "value": 0.9}, "state")
check("drumVol set", abs(state["params"]["drumVol"] - 0.9) < 1e-4)

# 4g. groove library: save current grid+pattern, mutate, load-restores, delete
state = rpc({"type": "setDrumGrid", "beatsPerBar": 3, "bars": 2, "subdiv": 2}, "state")  # 12 steps
rpc({"type": "setDrumCell", "voice": 4, "step": 3, "on": True}, "state")  # ride on step 3
state = rpc({"type": "saveGroove", "name": "  ws-test-groove  "}, "state")
check("groove saved (name trimmed)", "ws-test-groove" in state.get("grooves", []), str(state.get("grooves")))
# change layout + pattern, then load must restore both
rpc({"type": "setDrumGrid", "beatsPerBar": 4, "bars": 1, "subdiv": 4}, "state")
state = rpc({"type": "loadGroove", "name": "ws-test-groove"}, "state")
dm = state["drums"]
check("groove load restores grid", dm["beatsPerBar"] == 3 and dm["bars"] == 2 and dm["subdiv"] == 2,
      f"{dm['beatsPerBar']}/{dm['bars']}/{dm['subdiv']} steps={dm['stepCount']}")
check("groove load restores pattern", dm["pattern"][4][3] == 1)
err = rpc({"type": "loadGroove", "name": "does-not-exist"}, "error")
check("load unknown groove -> error", err.get("type") == "error")
err = rpc({"type": "saveGroove", "name": "   "}, "error")
check("save blank groove -> error", err.get("type") == "error")
state = rpc({"type": "deleteGroove", "name": "ws-test-groove"}, "state")
check("groove deleted", "ws-test-groove" not in state.get("grooves", []))
err = rpc({"type": "deleteGroove", "name": "ws-test-groove"}, "error")
check("delete unknown groove -> error", err.get("type") == "error")
rpc({"type": "setDrumGrid", "beatsPerBar": 4, "bars": 1, "subdiv": 4}, "state")  # restore default grid

# 5. meters flowing
got_meters = 0
ws.settimeout(2)
t0 = time.time()
while time.time() - t0 < 1.5:
    try:
        m = json.loads(ws.recv())
        if m.get("type") == "meters":
            got_meters += 1
    except Exception:
        break
check("meters ~15Hz", got_meters >= 10, f"{got_meters} in 1.5s")

# 6. panic / input gate
state = rpc({"type": "panic"}, "state")
check("panic mutes", state["params"]["mute"] is True)
state = rpc({"type": "setParam", "id": "mute", "value": 0}, "state")
check("unmute (enable input)", state["params"]["mute"] is False)
state = rpc({"type": "setParam", "id": "mute", "value": 1}, "state")  # restore muted default
check("re-mute", state["params"]["mute"] is True)

# 6b. picking trainer (P5a): params round-trip and the stats stream flows.
state = rpc({"type": "setParam", "id": "pickOn", "value": 1}, "state")
check("pickOn set", state["params"]["pickOn"] is True)
state = rpc({"type": "setParam", "id": "pickSens", "value": 0.8}, "state")
check("pickSens set", abs(state["params"]["pickSens"] - 0.8) < 1e-6)
got_picking = None
deadline = time.time() + 2.0
while time.time() < deadline:
    m = json.loads(ws.recv())
    if m.get("type") == "picking":
        got_picking = m
        break
check("picking stream flows", got_picking is not None)
if got_picking:
    check("picking message shape",
          "n" in got_picking and "onsets" in got_picking and "clicks" in got_picking
          and "beatMs" in got_picking,
          str({k: got_picking[k] for k in ("n", "beatMs")}))
    check("picking: silence -> no onsets", got_picking["n"] == 0,
          f"n={got_picking['n']}")
state = rpc({"type": "setParam", "id": "pickOn", "value": 0}, "state")
check("pickOn cleared", state["params"]["pickOn"] is False)

# 6c. pick run (P5b): validation, one-click start (metronome + detector), a
# real short run end-to-end (result arrives, metronome auto-stops), cancel.
err = rpc({"type": "startPickRun", "bars": 0, "countIn": 1}, "error")
check("run: bad bars -> error", err.get("type") == "error")
err = rpc({"type": "startPickRun", "bars": 1, "countIn": 5}, "error")
check("run: bad countIn -> error", err.get("type") == "error")

prev_bpm = state["params"]["metroBpm"]
prev_beats = state["params"]["metroBeats"]
rpc({"type": "setParam", "id": "metroBpm", "value": 360}, "state")
rpc({"type": "setParam", "id": "metroBeats", "value": 3}, "state")
state = rpc({"type": "startPickRun", "bars": 1, "countIn": 1}, "state")
check("run starts metronome + detector",
      state["params"]["metroOn"] is True and state["params"]["pickOn"] is True)
check("run active in state", state.get("pickRunActive") is True)
# 1 count-in bar + 1 bar of 3 beats @360 bpm + boundary ≈ 1.2 s; wait for result.
got_status = None
got_result = None
ws.settimeout(6)
deadline = time.time() + 5.0
while time.time() < deadline and not got_result:
    m = json.loads(ws.recv())
    if m.get("type") == "pickRun":
        got_status = m
    elif m.get("type") == "pickRunResult":
        got_result = m
check("run progress streams", got_status is not None,
      str(got_status and got_status.get("phase")))
check("run result arrives", got_result is not None)
if got_result:
    check("result grid: bars*beats+1 clicks", len(got_result["clicks"]) == 4,
          f"n={len(got_result['clicks'])}")
    check("result grid starts at 0 ms", abs(got_result["clicks"][0]) < 1e-6)
    check("result: silence -> no onsets", got_result["onsets"] == [])
    check("result echoes config",
          got_result["bars"] == 1 and got_result["countIn"] == 1
          and got_result["beatsPerBar"] == 3)
state = rpc({"type": "hello"}, "state")
check("metronome auto-stops after run", state["params"]["metroOn"] is False)
check("run inactive after result", state.get("pickRunActive") is False)
# cancel path: manual metronome stop also cancels (it is the run's clock)
state = rpc({"type": "startPickRun", "bars": 16, "countIn": 2}, "state")
check("second run starts", state.get("pickRunActive") is True)
state = rpc({"type": "cancelPickRun"}, "state")
check("cancel stops run + metronome",
      state.get("pickRunActive") is False and state["params"]["metroOn"] is False)
state = rpc({"type": "startPickRun", "bars": 16, "countIn": 2}, "state")
state = rpc({"type": "setParam", "id": "metroOn", "value": 0}, "state")
check("stopping metronome cancels run", state.get("pickRunActive") is False)
# restore
rpc({"type": "setParam", "id": "metroBpm", "value": prev_bpm}, "state")
rpc({"type": "setParam", "id": "metroBeats", "value": prev_beats}, "state")
rpc({"type": "setParam", "id": "pickOn", "value": 0}, "state")

ws.close()

# 7. non-local origin must pair before anything (P4f). It may connect, but
# gets needPair and cannot reach state without the code — no data leaks.
# (Full pairing flow is covered by pairing_test.py.)
try:
    bad = websocket.create_connection("ws://127.0.0.1:43717", timeout=5,
                                      origin="https://evil.example.com")
    opened = json.loads(bad.recv())  # server greets unpaired origins with needPair
    bad.send(json.dumps({"type": "hello"}))
    hello_resp = json.loads(bad.recv())
    check("unpaired origin gets needPair, never state",
          opened.get("type") == "needPair" and hello_resp.get("type") != "state",
          f"open={opened.get('type')} hello={hello_resp.get('type')}")
    bad.close()
except Exception as e:
    check("unpaired origin gets needPair, never state", False, type(e).__name__)

print("\n" + ("ALL PASS" if not FAILURES else f"FAILURES: {FAILURES}"))
