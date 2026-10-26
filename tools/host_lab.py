#!/usr/bin/env python3
"""No-dependency host tools. Synthetic replay is NOT an MCU timing benchmark."""
import argparse
import http.client
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = Path(__file__).resolve().parents[1]
SCENARIO = """start 0
face 100 80 0.6 1
touch 150
tick 550
cancel 600
start 700
reply 800 1
face 900 850 -0.4 1
reply 1000 2
finish 1100
start 1200
tick 16200
reply 16201 3
"""


def compile_cpp(source, output):
    compiler = shlex.split(os.environ.get("CXX", "g++"))
    subprocess.run(compiler + ["-std=c++17", "-Wall", "-Wextra", "-Werror",
                   "-pedantic", "-I" + str(ROOT / "include"),
                   str(ROOT / source), "-o", str(output)], check=True)


def replay(scenario=SCENARIO):
    with tempfile.TemporaryDirectory(prefix="stackchan-") as directory:
        executable = Path(directory) / ("replay.exe" if os.name == "nt" else "replay")
        compile_cpp("tools/replay.cpp", executable)
        result = subprocess.run([str(executable)], input=scenario, text=True,
                                capture_output=True, check=True)
        return [json.loads(line) for line in result.stdout.splitlines()]


class MockHandler(BaseHTTPRequestHandler):
    """Test endpoint only. No forwarding, API keys, conversation storage or TLS."""
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def send_json(self, status, value):
        payload = json.dumps(value, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Connection", "close")
        self.end_headers()
        try:
            self.wfile.write(payload)
        except (BrokenPipeError, ConnectionResetError):
            pass  # Client cancellation is an expected test case.
        self.close_connection = True

    def do_GET(self):
        self.send_json(200 if self.path == "/health" else 404,
                       {"mock": True, "status": "ok"} if self.path == "/health"
                       else {"error": "not_found"})

    def do_POST(self):
        if self.path != "/v1/chat/completions":
            self.send_json(404, {"error": "not_found"})
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 65536:
                self.send_json(413, {"error": "body_size"})
                return
            self.connection.settimeout(5)
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError("body must be an object")
            delay = body.get("mock_delay_ms", 0)
            if isinstance(delay, bool) or not isinstance(delay, int) or not 0 <= delay <= 20000:
                raise ValueError("mock_delay_ms must be an integer in [0,20000]")
            status = body.get("mock_status", 200)
            if type(status) is not int or status not in (200, 429, 500, 503):
                raise ValueError("unsupported mock_status")
            if body.get("stream", False):
                raise ValueError("streaming is not implemented by this mock")
        except (ValueError, TypeError, OSError):
            self.send_json(400, {"error": "invalid_request"})
            return
        time.sleep(delay / 1000)
        if status != 200:
            self.send_json(status, {"error": {"message": "synthetic failure", "type": "mock"}})
            return
        content = json.dumps({"text": "これは模擬回答です。", "expression": "neutral",
                              "gesture": "nod"}, ensure_ascii=False)
        self.send_json(200, {"id": "mock-response", "object": "chat.completion",
                            "model": "mock-only", "mock": True,
                            "choices": [{"index": 0, "message": {"role": "assistant",
                             "content": content}, "finish_reason": "stop"}]})


def request(port, body, path="/v1/chat/completions"):
    connection = http.client.HTTPConnection("127.0.0.1", port, timeout=3)
    try:
        connection.request("POST", path, body=json.dumps(body).encode(),
                           headers={"Content-Type": "application/json"})
        response = connection.getresponse()
        return response.status, json.loads(response.read())
    finally:
        connection.close()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def check():
    with tempfile.TemporaryDirectory(prefix="stackchan-test-") as directory:
        executable = Path(directory) / ("host.exe" if os.name == "nt" else "host")
        compile_cpp("tests/host.cpp", executable)
        subprocess.run([str(executable)], check=True)
    records = replay()
    require(len(records) == 13, "replay event count")
    require(records[2]["face"] == "happy" and records[2]["track_head"], "parallel output")
    require(records[3]["face"] == "thinking", "touch expiration")
    require(not records[6]["accepted"] and records[6]["state"] == "waiting", "stale reply")
    require(records[8]["accepted"] and records[8]["state"] == "answering", "handoff")
    require(records[-2]["state"] == "error" and not records[-1]["accepted"], "timeout")
    require(all(r["clock_domain"] == "synthetic_ms" for r in records), "clock labeling")
    server = ThreadingHTTPServer(("127.0.0.1", 0), MockHandler)
    server.daemon_threads = True
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        port = server.server_address[1]
        status, result = request(port, {"mock_delay_ms": 10})
        require(status == 200 and result["mock"], "mock success")
        content = json.loads(result["choices"][0]["message"]["content"])
        require(content["gesture"] == "nod", "mock result contract")
        require(request(port, {"mock_status": 503})[0] == 503, "failure injection")
        require(request(port, {"mock_delay_ms": -1})[0] == 400, "invalid delay")
        require(request(port, {"stream": True})[0] == 400, "unsupported streaming")
        require(request(port, {}, "/missing")[0] == 404, "wrong endpoint")
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=2)
    print("PASS: synthetic replay and loopback mock HTTP integration")
    print("Not measured: MCU latency, physical motion, face inference, real model API.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("check", help="compile and run all current host checks")
    play = sub.add_parser("replay", help="emit synthetic policy JSONL")
    play.add_argument("--scenario", type=Path, help="event text file; default is built-in")
    play.add_argument("--output", type=Path, help="write log; refuses overwriting existing file")
    serve = sub.add_parser("serve", help="run local test endpoint (not a production provider)")
    serve.add_argument("--host", default="127.0.0.1")
    serve.add_argument("--port", default=8765, type=int)
    args = parser.parse_args()
    if args.command == "check":
        check()
    elif args.command == "replay":
        records = replay(args.scenario.read_text(encoding="utf-8") if args.scenario else SCENARIO)
        text = "\n".join(json.dumps(r, ensure_ascii=False) for r in records) + "\n"
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            with args.output.open("x", encoding="utf-8") as target:
                target.write(text)
        else:
            print(text, end="")
    else:
        server = ThreadingHTTPServer((args.host, args.port), MockHandler)
        server.daemon_threads = True
        print(f"MOCK ONLY http://{args.host}:{server.server_address[1]}/v1/chat/completions")
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            server.server_close()


if __name__ == "__main__":
    main()
