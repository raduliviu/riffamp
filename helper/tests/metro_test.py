"""Metronome verification: params round-trip + audible output via meters."""
import json
import time

import websocket

ws = websocket.create_connection("ws://127.0.0.1:43717", timeout=10,
                                 origin="http://127.0.0.1:43718")


def rpc(msg):
    ws.send(json.dumps(msg))
    while True:
        r = json.loads(ws.recv())
        if r.get("type") != "meters":
            return r


def out_peak(seconds):
    peak = 0.0
    t0 = time.time()
    while time.time() - t0 < seconds:
        m = json.loads(ws.recv())
        if m.get("type") == "meters":
            peak = max(peak, m["out"])
    return peak


checks = []
s = rpc({"type": "hello"})
checks.append(("metro params in state", all(
    k in s["params"] for k in ("metroOn", "metroBpm", "metroBeats", "metroVol"))))

s = rpc({"type": "setParam", "id": "metroBpm", "value": 90})
checks.append(("bpm set", s["params"]["metroBpm"] == 90))
s = rpc({"type": "setParam", "id": "metroBpm", "value": 999})
checks.append(("bpm clamped", s["params"]["metroBpm"] == 300))
s = rpc({"type": "setParam", "id": "metroBpm", "value": 120})

quiet = out_peak(1.5)
s = rpc({"type": "setParam", "id": "metroOn", "value": 1})
checks.append(("metroOn set", s["params"]["metroOn"] is True))
loud = out_peak(2.0)
s = rpc({"type": "setParam", "id": "metroOn", "value": 0})
time.sleep(0.3)
_ = out_peak(0.5)
quiet2 = out_peak(1.0)

checks.append(("clicks audible on OUT meter", loud > 0.2, f"peak {loud:.3f}"))
checks.append(("silent before", quiet < 0.05, f"peak {quiet:.3f}"))
checks.append(("silent after stop", quiet2 < 0.05, f"peak {quiet2:.3f}"))

ws.close()
fails = 0
for c in checks:
    ok = c[1]
    detail = f"  [{c[2]}]" if len(c) > 2 else ""
    print(("PASS " if ok else "FAIL ") + c[0] + detail)
    fails += 0 if ok else 1
print("ALL PASS" if fails == 0 else f"{fails} FAILURES")
