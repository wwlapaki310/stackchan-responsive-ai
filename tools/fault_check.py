#!/usr/bin/env python3
"""On-device failure-path check over the LAN: bad API key, unreachable host, hung request.
Uses the test-only fault injection (/opt?n=fault&v=) and polls /status. Uses real API quota
for the recovery request. Usage: python tools/fault_check.py <device-ip> [secrets.h]
Records state transitions and the status text; it does not judge the user-visible face."""
import re
import sys
import time
import urllib.parse
import urllib.request

ip = sys.argv[1]
secrets = sys.argv[2] if len(sys.argv) > 2 else "firmware/answer_demo/secrets.h"
token = re.search(r'WEB_TOKEN "([0-9a-f]+)"', open(secrets, encoding="utf-8").read()).group(1)
base = f"http://{ip}"


def call(path, data=None, method=None):
    url = f"{base}{path}{'&' if '?' in path else '?'}t={token}"
    req = urllib.request.Request(url, data=urllib.parse.urlencode(data).encode() if data else None, method=method)
    with urllib.request.urlopen(req, timeout=10) as r:
        return r.read().decode()


def status():
    import json
    return json.loads(call("/status"))


def run(name, fault, wait_s):
    call("/opt?n=fault&v=%d" % fault, {"x": 1}, "POST")
    time.sleep(2.2)
    t0 = time.time()
    call("/ask", {"i": 0}, "POST")
    seq, last = [], None
    while time.time() - t0 < wait_s:
        s = status()
        cur = (s["state"], s["status"])
        if cur != last:
            seq.append((round(time.time() - t0, 1), s["state"], s["status"]))
            last = cur
        time.sleep(0.25)
    call("/opt?n=fault&v=0", {"x": 1}, "POST")
    print(f"[{name}] fault={fault}")
    for t, st, tx in seq:
        print(f"   t={t:5.1f}s state={st} status={tx!r}")
    return seq


results = {}
results["bad key"] = run("bad key", 1, 8)
results["unreachable"] = run("unreachable host", 2, 20)
results["hang"] = run("hang 16 s (core timeout 15 s)", 3, 24)
time.sleep(4)                                   # the hung request must finish before the next one
before = status()["ok"]
call("/ask", {"i": 1}, "POST")
for _ in range(40):
    time.sleep(0.25)
    if status()["ok"] > before:
        break
after = status()
print(f"[recovery] ok before={before} after={after['ok']} state={after['state']}")
fail = []
if not any(st == "error" for _, st, _ in results["bad key"]): fail.append("bad key did not reach error")
if not any(st == "error" for _, st, _ in results["unreachable"]): fail.append("unreachable did not reach error")
if not any(tx == "時間切れ" for _, _, tx in results["hang"]): fail.append("hang did not time out")
if after["ok"] <= before: fail.append("no recovery")
print("PASS" if not fail else "FAIL: " + "; ".join(fail))
sys.exit(1 if fail else 0)
