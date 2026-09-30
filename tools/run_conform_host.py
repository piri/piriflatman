#!/usr/bin/env python3
"""Run an installed official ConformU against the C++ host mock (never hardware)."""
import argparse
import pathlib
import socket
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--conformu", required=True, type=pathlib.Path)
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "artifacts/conform-host")
    args = parser.parse_args()
    binary, output = args.conformu.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        udp = sock.getsockname()[1]
    server = subprocess.Popen([str(ROOT / ".pio/build/native/program"), "--serve", str(port), str(udp)])
    failed = False
    try:
        for _ in range(100):
            if server.poll() is not None:
                raise RuntimeError("Host server exited")
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=0.1):
                    break
            except OSError:
                time.sleep(0.02)
        else:
            raise RuntimeError("Host server not ready")
        uri = f"http://127.0.0.1:{port}/api/v1/covercalibrator/0"
        for command in ("conformance", "alpacaprotocol"):
            with (output / f"{command}-console.txt").open("w") as console:
                result = subprocess.run([str(binary), command, uri,
                    "--logfilename", str(output / f"{command}.log"),
                    "--resultsfile", str(output / f"{command}.json")], stdout=console, stderr=subprocess.STDOUT, timeout=180)
            print(f"{command}: exit {result.returncode}; evidence in {output}")
            failed |= result.returncode != 0
    finally:
        server.terminate()
        try:
            server.wait(timeout=3)
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
    raise SystemExit(int(failed))


if __name__ == "__main__":
    main()
