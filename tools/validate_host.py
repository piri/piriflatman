#!/usr/bin/env python3
"""Run core tests and real loopback HTTP/UDP checks against the C++ host build."""
import pathlib
import socket
import subprocess
import sys
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROGRAM = ROOT / ".pio/build/native/program"


def available_port(kind):
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def main():
    subprocess.run([str(PROGRAM), "--test"], check=True)
    http, udp = available_port(socket.SOCK_STREAM), available_port(socket.SOCK_DGRAM)
    server = subprocess.Popen([str(PROGRAM), "--serve", str(http), str(udp)])
    try:
        for _ in range(100):
            if server.poll() is not None:
                raise RuntimeError("Host server exited before readiness")
            try:
                with socket.create_connection(("127.0.0.1", http), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.02)
        else:
            raise RuntimeError("Host server did not start")
        subprocess.run([sys.executable, str(ROOT / "tools/smoke_test.py"), "--host", "127.0.0.1", "--port", str(http), "--discovery-port", str(udp), "--exercise"], check=True)
        # Invalid discovery packets produce no response.
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.settimeout(0.15)
            for packet in (b"", b"alpacadiscovery", b"alpacadiscovery2", b"ALPACADISCOVERY1", b"alpacadiscovery1" + b"x" * 49):
                sock.sendto(packet, ("127.0.0.1", udp))
                try:
                    sock.recvfrom(1024)
                except socket.timeout:
                    pass
                else:
                    raise AssertionError(f"Accepted invalid discovery packet {packet!r}")
        # Real 100-continue handshake and fragmented body.
        with socket.create_connection(("127.0.0.1", http), timeout=3) as sock:
            sock.sendall(b"PUT /api/v1/covercalibrator/0/connected HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: 15\r\nExpect: 100-continue\r\n\r\n")
            assert sock.recv(4096) == b"HTTP/1.1 100 Continue\r\n\r\n"
            sock.sendall(b"Connected=")
            time.sleep(0.02)
            sock.sendall(b"false")
            response = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
            assert b'"ErrorNumber":0' in response and response.startswith(b"HTTP/1.1 200")
        # A slow/incomplete client cannot starve another client's requests.
        with socket.create_connection(("127.0.0.1", http), timeout=3) as slow:
            slow.sendall(b"GET / HTTP/1.1\r\n")
            with socket.create_connection(("127.0.0.1", http), timeout=3) as fast:
                fast.sendall(b"GET /api/v1/covercalibrator/0/name HTTP/1.1\r\nHost: localhost\r\n\r\n")
                assert b"200 OK" in fast.recv(4096)
            slow.settimeout(7)
            assert slow.recv(4096) == b"", "Incomplete client not expired"
        print("PASS malformed discovery, fragmented HTTP, 100-continue, concurrent slow-client and timeout checks")
    finally:
        server.terminate()
        try:
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()


if __name__ == "__main__":
    main()
