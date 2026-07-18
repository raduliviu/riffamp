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
check("engine info", state["engine"]["buffer"] == 64 and state["engine"]["sampleRate"] == 48000)

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

# 6. panic
state = rpc({"type": "panic"}, "state")
check("panic mutes", state["params"]["mute"] is True)
state = rpc({"type": "setParam", "id": "mute", "value": 0}, "state")
check("unmute", state["params"]["mute"] is False)

ws.close()

# 7. bad origin rejected
try:
    bad = websocket.create_connection("ws://127.0.0.1:43717", timeout=5,
                                      origin="https://evil.example.com")
    bad.send(json.dumps({"type": "hello"}))
    resp = bad.recv()
    # A server-initiated close surfaces as an empty read; any state reply = leak.
    check("evil origin rejected", resp == "", f"got: {resp[:60]}")
except Exception as e:
    check("evil origin rejected", True, type(e).__name__)

print("\n" + ("ALL PASS" if not FAILURES else f"FAILURES: {FAILURES}"))
