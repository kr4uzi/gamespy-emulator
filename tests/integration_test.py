"""Exercise startup, HTTP validation, UDP parsing and shutdown on loopback."""
import http.client
import json
import os
from pathlib import Path
import signal
import socket
import subprocess
import sys
import tempfile
import time

executable = str(Path(sys.argv[1]).resolve())

def check(condition, message):
    if not condition:
        raise AssertionError(message)

with tempfile.TemporaryDirectory(prefix="gamespy-test-") as directory:
    work = Path(directory)
    (work / "http").mkdir()
    (work / "http" / "index.html").write_text("integration test index")
    help_run = subprocess.run([executable, "--help"], cwd=work, capture_output=True, timeout=10)
    check(help_run.returncode == 0 and b"Allowed options" in help_run.stdout, "--help must exit successfully")
    for argument in ("-admin-port=65536", "-admin-port=nope", "-gamedb=missing.json"):
        invalid = subprocess.run([executable, argument], cwd=work, capture_output=True, timeout=10)
        check(invalid.returncode != 0, f"startup error must fail: {argument}")

    with socket.socket(socket.AF_INET6, socket.SOCK_STREAM) as probe:
        probe.bind(("::1", 0))
        admin_port = probe.getsockname()[1]
    process = subprocess.Popen([executable, f"-admin-port={admin_port}"], cwd=work,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    clients = []
    try:
        deadline = time.monotonic() + 15
        while True:
            if process.poll() is not None:
                raise AssertionError(f"server exited during startup: {process.communicate()[0].decode(errors='replace')}")
            try:
                connection = socket.create_connection(("::1", admin_port), timeout=1)
                connection.close()
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)

        def request(path, method="GET", headers=None):
            connection = http.client.HTTPConnection("::1", admin_port, timeout=5)
            try:
                connection.request(method, path, headers=headers or {})
                response = connection.getresponse()
                data = response.read()
                check(response.getheader("Content-Length") is not None, "HTTP response must be framed")
                return response.status, data
            finally:
                connection.close()

        check(request("/") == (200, b"integration test index"), "serve packaged web UI")
        status, body = request("/api/games")
        check(status == 200 and any(game["name"] == "gmtest" for game in json.loads(body)), "list games")
        for path in (
            "/api/servers?game=gmtest&limit=",
            "/api/servers?game=gmtest&limit=4294967296",
            "/api/servers?game=gmtest&skip=bad",
            "/api/servers?game=missing",
            "/api/server?game=gmtest&ip=127.0.0.1&port=65536",
            "/api/server?game=gmtest&ip=not-an-address&port=1234",
            "/api/%zz",
        ):
            check(request(path)[0] == 400, f"reject invalid request: {path}")
        check(request("/../players.sqlite3")[0] == 404, "block web-root traversal")
        check(request("/api/servers?game=gmtest")[0] == 200, "server survives bad requests")
        mutation = "/api/server?game=gmtest&ip=127.0.0.1&port=12345&hostname=fixture"
        for headers in ({"Origin": "http://untrusted.example"}, {"Origin": "null"}, {"Sec-Fetch-Site": "cross-site"}):
            check(request(mutation, "POST", headers)[0] == 403, "reject cross-origin admin mutation")
        check(request(mutation, "POST", {"Origin": f"http://[::1]:{admin_port}"})[0] == 200, "allow same-origin UI mutation")
        status, body = request("/api/servers?game=gmtest&fields=hostname")
        check(status == 200 and json.loads(body)[0]["hostname"] == "fixture", "server data survives request cleanup")

        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            udp.settimeout(5)
            for packet in (b"", b"\x09\0\0\0\0", b"\x03\0\0\0\0broken"):
                udp.sendto(packet, ("127.0.0.1", 27900))
            udp.sendto(b"\x09\0\0\0\0gmtest\0", ("127.0.0.1", 27900))
            response, _ = udp.recvfrom(128)
            check(response == b"\xfe\xfd\x09\0\0\0\0", "valid availability request after malformed UDP")

        # Leave TCP operations pending while shutting down, including an incomplete HTTP request.
        for port in (29900, 29901, 28910, 29920):
            clients.append(socket.create_connection(("127.0.0.1", port), timeout=5))
        clients.append(socket.create_connection(("::1", admin_port), timeout=5))
        clients[-1].sendall(b"GET / HTTP/1.1\r\n")
        if os.name != "nt":
            process.send_signal(signal.SIGTERM)
            output, _ = process.communicate(timeout=10)
            check(process.returncode == 0, f"graceful shutdown failed: {output.decode(errors='replace')}")
        else:
            # Windows terminate() is forcible; cancellation itself is covered by session_test.
            process.terminate()
            process.communicate(timeout=10)
    except BaseException:
        if process.poll() is None:
            process.terminate()
        output, _ = process.communicate(timeout=10)
        sys.stderr.write(output.decode(errors="replace"))
        raise
    finally:
        for client in clients:
            client.close()
        if process.poll() is None:
            process.kill()
            process.wait()
print("integration regressions passed")
