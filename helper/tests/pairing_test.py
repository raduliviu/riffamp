"""Pairing test (P4f): local origins are trusted; non-local origins must pair
with the code the helper prints, and a paired origin persists across reconnect.

Run the helper first, capturing stdout to a file, then pass the code:
    ./helper/build/webamp-helper --assets assets >/tmp/h.log 2>&1 &
    PAIR_CODE=$(grep -o 'Pairing code.*: [0-9]*' /tmp/h.log | grep -o '[0-9]*$')
    python3 pairing_test.py "$PAIR_CODE"
"""
import json
import sys

import websocket

CODE = sys.argv[1] if len(sys.argv) > 1 else ""
HOSTED = "https://www.webamp.com"
URL = "ws://127.0.0.1:43717"
FAILURES = []


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (f"  [{detail}]" if detail else ""))
    if not cond:
        FAILURES.append(name)


def conn(origin):
    return websocket.create_connection(URL, timeout=10, origin=origin)


def recv(ws):
    while True:
        m = json.loads(ws.recv())
        if m.get("type") == "meters":
            continue
        return m


# 1. Local origin is trusted immediately (no pairing).
w = conn("http://localhost:5173")
w.send(json.dumps({"type": "hello"}))
check("local origin -> state (no pairing)", recv(w).get("type") == "state")
w.close()

# 2. Non-local origin gets needPair on open, and is refused real commands.
w = conn(HOSTED)
check("hosted origin -> needPair on open", recv(w).get("type") == "needPair")
w.send(json.dumps({"type": "hello"}))
check("hosted hello before pairing -> needPair", recv(w).get("type") == "needPair")

# 3. Wrong code fails (and does not grant access).
w.send(json.dumps({"type": "pair", "code": "000000"}))
r = recv(w)
check("wrong code -> pairFailed", r.get("type") == "pairFailed", str(r))

# 4. Correct code -> state (now trusted).
w.send(json.dumps({"type": "pair", "code": CODE}))
r = recv(w)
check("correct code -> state", r.get("type") == "state", str(r.get("type")))
w.send(json.dumps({"type": "hello"}))
check("hosted hello after pairing -> state", recv(w).get("type") == "state")
w.close()

# 5. Paired origin persists: a fresh connection is trusted without pairing.
w = conn(HOSTED)
w.send(json.dumps({"type": "hello"}))
check("paired origin reconnect -> state (persisted)", recv(w).get("type") == "state")
w.close()

print("\n" + ("ALL PASS" if not FAILURES else f"FAILURES: {FAILURES}"))
sys.exit(1 if FAILURES else 0)
