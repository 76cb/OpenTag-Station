#!/usr/bin/env python3
"""Read-only health check of a running OpenTag Station over your network.

Run it on a computer on the same network as the station:

    python station_check.py                       # station at opentag-station.local
    python station_check.py --host 192.168.3.34   # or by IP address
    python station_check.py --token YOUR_TOKEN    # if you set an access token

It needs only Python 3.8+ (no extra packages). It never writes or erases a tag,
never changes settings and never saves weights. It does send harmless requests:
spool-list pages (the same as browsing Inventory) and one deliberately invalid
request to check that changes need the access token. Close the station's web
page in your browser first, so the live-update test has free connection slots.

The report is printed and also saved as station-check.txt next to this script.
"""
import argparse
import base64
import json
import os
import socket
import sys
import time
import urllib.error
import urllib.request
import uuid

LINES = []


def say(text=""):
    print(text)
    LINES.append(text)


def request(base, method, path, token=None, body=None, timeout=10):
    data = None if body is None else json.dumps(body).encode()
    req = urllib.request.Request(base + path, data=data, method=method)
    req.add_header("Accept", "application/json")
    if body is not None:
        req.add_header("Content-Type", "application/json")
        req.add_header("Idempotency-Key", str(uuid.uuid4()))
    if token:
        req.add_header("Authorization", "Bearer " + token)
    started = time.monotonic()
    try:
        with urllib.request.urlopen(req, timeout=timeout) as response:
            raw = response.read()
            status = response.status
    except urllib.error.HTTPError as error:
        raw = error.read()
        status = error.code
    except Exception as error:  # noqa: BLE001 - report every failure
        return None, None, f"{type(error).__name__}: {error}", time.monotonic() - started
    try:
        parsed = json.loads(raw.decode() or "null")
    except ValueError:
        parsed = None
    return status, parsed, raw.decode(errors="replace"), time.monotonic() - started


def pick(document, *path, default=None):
    for key in path:
        if isinstance(document, dict) and key in document:
            document = document[key]
        else:
            return default
    return document


def section(title):
    say()
    say("== " + title)


def check_reads(base, token):
    section("Station status (read only)")
    results = {}
    for path in ("/api/v1/health", "/api/v1/device", "/api/v1/status", "/api/v1/network",
                 "/api/v1/scale", "/api/v1/nfc", "/api/v1/spool", "/api/v1/printers",
                 "/api/v1/update", "/api/v1/diagnostics"):
        status, parsed, raw, seconds = request(base, "GET", path, token)
        results[path] = parsed if isinstance(parsed, (dict, list)) else None
        shown = raw if parsed is None else json.dumps(parsed, separators=(",", ":"))
        say(f"{path}: {status} in {seconds * 1000:.0f} ms")
        say("  " + (shown or "")[:1500])
    return results


def check_auth(base, token):
    section("Changes need the access token")
    # An invalid body: refused with 401 when a token is set and missing,
    # otherwise with 400 for the body. Nothing is changed either way.
    status, parsed, raw, _ = request(base, "POST", "/api/v1/network/setup-mode",
                                     None, {"stop": False})
    say(f"POST /network/setup-mode without token (invalid body): {status} {raw[:200]}")
    if status == 401:
        say("  RESULT: an access token is set and requests without it are refused. OK")
        return True
    if status == 400:
        say("  RESULT: no access token is set. Anyone on your network can change the station.")
        return False
    say("  RESULT: unexpected answer; please send this report.")
    return None


def wait_operation(base, token, operation_id, limit_seconds=20):
    deadline = time.monotonic() + limit_seconds
    while time.monotonic() < deadline:
        status, parsed, _, _ = request(base, "GET", f"/api/v1/operations/{operation_id}", token)
        state = pick(parsed, "data", "state")
        if status == 200 and state in ("succeeded", "failed", "cancelled"):
            return state, pick(parsed, "data", "error", "message")
        time.sleep(0.25)
    return "timeout", None


def check_catalog_burst(base, token, count, auth_required):
    section(f"Browsing the spool list {count} times in a row (rc.23 fix)")
    if auth_required and not token:
        say("  SKIPPED: an access token is set; run again with --token to include this test.")
        return
    accepted = refused = failed = 0
    first_problem = None
    for index in range(count):
        status, parsed, raw, _ = request(base, "POST", "/api/v1/tag-writer", token,
                                         {"action": "catalog", "entity": "spool", "offset": 0})
        if status in (200, 202):
            accepted += 1
            operation_id = pick(parsed, "data", "operation_id")
            if operation_id:
                state, error = wait_operation(base, token, operation_id)
                if state != "succeeded":
                    failed += 1
                    first_problem = first_problem or f"request {index + 1}: {state} {error or ''}"
        else:
            refused += 1
            first_problem = first_problem or f"request {index + 1}: {status} {raw[:200]}"
    say(f"  accepted {accepted}, refused {refused}, finished with an error {failed}")
    if first_problem:
        say("  first problem: " + first_problem)
    say("  RESULT: " + ("OK" if refused == 0 else "FAIL - requests were refused"))


def websocket_handshake(host, port, timeout=5):
    """Opens /api/v1/events as a real WebSocket. Returns (ok, first line, socket)."""
    sock = socket.create_connection((host, port), timeout=timeout)
    key = base64.b64encode(os.urandom(16)).decode()
    sock.sendall((
        f"GET /api/v1/events HTTP/1.1\r\nHost: {host}\r\nUpgrade: websocket\r\n"
        f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    ).encode())
    try:
        answer = sock.recv(512).decode(errors="replace")
    except socket.timeout:
        answer = "(no answer within 5 s)"
    first = answer.splitlines()[0] if answer else "(connection closed)"
    return first.startswith("HTTP/1.1 101"), first, sock


def check_events(host, port):
    section("Live updates (/api/v1/events)")
    # PW-2: does a plain request (not a WebSocket) take one of the two
    # live-update slots and keep it while the connection stays open?
    plain = socket.create_connection((host, port), timeout=5)
    plain.sendall(f"GET /api/v1/events HTTP/1.1\r\nHost: {host}\r\n\r\n".encode())
    try:
        reply = plain.recv(512).decode(errors="replace")
        plain_first = reply.splitlines()[0] if reply else "(connection closed)"
    except socket.timeout:
        plain_first = "(no answer within 5 s; connection held open)"
    say(f"  plain GET (like curl): {plain_first}")
    sockets = []
    results = []
    for attempt in (1, 2):
        try:
            ok, first, sock = websocket_handshake(host, port)
            sockets.append(sock)
            results.append(ok)
            say(f"  WebSocket {attempt} while the plain request is still open: {first}")
        except OSError as error:
            results.append(False)
            say(f"  WebSocket {attempt}: {type(error).__name__}: {error}")
    for sock in sockets + [plain]:
        try:
            sock.close()
        except OSError:
            pass
    if all(results):
        say("  RESULT: OK - a plain request does not use up a live-update slot")
    else:
        say("  RESULT: a live-update connection was refused. If the station's web page was open "
            "in a browser, close it and run again; otherwise the plain request took a slot (PW-2).")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="opentag-station.local")
    parser.add_argument("--port", type=int, default=80)
    parser.add_argument("--token", default=None, help="the station's access token, if you set one")
    parser.add_argument("--burst", type=int, default=40, help="spool-list requests to send (default 40)")
    args = parser.parse_args()
    base = f"http://{args.host}:{args.port}"
    say(f"OpenTag Station check of {base} at {time.strftime('%Y-%m-%d %H:%M:%S')}")
    status, _, raw, _ = request(base, "GET", "/api/v1/health", args.token, timeout=5)
    if status is None:
        say(f"Cannot reach the station: {raw}")
        say("Check the address (try --host with the IP shown on the touchscreen Settings page).")
        return 1
    reads = check_reads(base, args.token)
    device = reads.get("/api/v1/device")
    version = pick(device, "data", "build", "version")
    sha = pick(device, "data", "build", "git_sha")
    say()
    say(f"Firmware: {version} ({sha})")
    auth_required = check_auth(base, args.token)
    check_catalog_burst(base, args.token, args.burst, auth_required is True)
    check_events(args.host, args.port)
    output = os.path.join(os.path.dirname(os.path.abspath(__file__)), "station-check.txt")
    with open(output, "w", encoding="utf-8") as handle:
        handle.write("\n".join(LINES) + "\n")
    say()
    say(f"Saved to {output}. Send this file (or paste the text) back.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
