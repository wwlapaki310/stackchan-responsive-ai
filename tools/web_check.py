#!/usr/bin/env python3
"""On-device check of the LAN control endpoints (cancel, late-reply rejection, supersede).

Usage: python tools/web_check.py <device-ip> [path/to/secrets.h]

Reads WEB_TOKEN from the git-ignored secrets.h and never prints it. It sends real requests
through the device to the cloud API (3 synthetic questions), so it uses API quota. It
does not prove behaviour under packet loss, Wi-Fi loss or API errors.
"""
import http.client
import json
import re
import sys
import threading
import time

ip = sys.argv[1]
secrets = sys.argv[2] if len(sys.argv) > 2 else "firmware/answer_demo/secrets.h"
token = re.search(r'#define WEB_TOKEN "([0-9a-f]+)"', open(secrets, encoding="utf-8").read()).group(1)


def call(method, path, body=None):
    conn = http.client.HTTPConnection(ip, 80, timeout=15)
    start = time.perf_counter()
    headers = {"Content-Type": "application/x-www-form-urlencoded"} if body else {}
    conn.request(method, f"{path}?t={token}", body=body, headers=headers)
    resp = conn.getresponse()
    data = resp.read().decode("utf-8", "replace")
    conn.close()
    return resp.status, data, (time.perf_counter() - start) * 1000


def status():
    j = json.loads(call("GET", "/status")[1])
    return j["state"], j["ok"], j["rejected"], bool(j["answer"])


def check(label, cond):
    print(("PASS " if cond else "FAIL ") + label)
    return cond


results = []
state, ok0, rej0, _ = status()
print("start:", state, "ok", ok0, "rejected", rej0)
anon = http.client.HTTPConnection(ip, 80, timeout=5)
anon.request("GET", "/status")
results.append(check("no token -> 403", anon.getresponse().status == 403))
anon.close()

# 1. cancel shortly after ask: the late reply must be rejected, not shown
for delay in (0.15, 0.4):
    time.sleep(2.2)  # device rate limit between asks
    _, ok_before, rej_before, _ = status()
    t = threading.Thread(target=call, args=("POST", "/ask", "i=2"))
    t.start()
    time.sleep(delay)
    cancel = call("POST", "/cancel")
    t.join()
    time.sleep(4)
    state, ok_after, rej_after, has_answer = status()
    results.append(check(f"cancel after {delay}s: cancelled, late reply rejected, no answer shown",
                         state == "cancelled" and rej_after == rej_before + 1 and ok_after == ok_before
                         and not has_answer))

# 2. supersede: ask A, cancel, ask B -> only B accepted
time.sleep(2.2)
_, ok_before, rej_before, _ = status()
call("POST", "/ask", "i=0")
time.sleep(0.1)
call("POST", "/cancel")
time.sleep(2.2)
call("POST", "/ask", "i=2")
time.sleep(3.5)
state, ok_after, rej_after, has_answer = status()
results.append(check("supersede: old reply rejected, only the new reply accepted",
                     rej_after == rej_before + 1 and ok_after == ok_before + 1 and has_answer))

# 3. request latency while idle
times = [call("POST", "/touch")[2] for _ in range(5)]
results.append(check(f"POST latency max {max(times):.0f} ms < 500 ms", max(times) < 500))

print("ALL PASS" if all(results) else "SOME FAILED")
sys.exit(0 if all(results) else 1)
